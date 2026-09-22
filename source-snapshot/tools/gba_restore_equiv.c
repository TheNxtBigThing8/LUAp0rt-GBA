/* ===========================================================================
 * tools/gba_restore_equiv.c -- THE OFFLINE SAVE-RESTORE HARNESS (M12C)
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and #includes
 * ALL FOUR shipping translation units VERBATIM:
 *
 *     adapters/gba/gba_save.c        (FROZEN M12B -- the WRITER)
 *     adapters/gba/gba_savehdr.c     (M12C -- the header READER)
 *     adapters/gba/gba_restore.c     (M12C -- the gpSP side)
 *     adapters/gba/gba_restorefile.c (M12C -- the two-pass ladder)
 *
 * It therefore tests the SHIPPING adapters rather than a paraphrase of them,
 * and it drives the REAL gba_restorefile_with() ladder against REAL files --
 * every rejection, the rollback and the passivity guarantee are exercised by
 * the same source that runs on the console.
 *
 * RUN THIS BEFORE SPENDING ANY CONSOLE TIME. At M12B the equivalent harness
 * protected against a WRONG FILE ON DISK. At M12C the stakes are higher in one
 * specific way: M12B could only ever damage a file, whereas M12C WRITES INTO A
 * LIVE EMULATOR. A validation bug here does not fail -- it silently loads the
 * wrong bytes into a running cartridge's save memory, and the operator's first
 * symptom is a corrupted game profile.
 *
 * ---- WHY THE HARNESS CAN INCLUDE THE gpSP-SIDE FILES ----
 *
 * gba_save.c and gba_restore.c both open with #include "common.h". This file
 * pre-defines that header's own guard (COMMON_H, gpsp/common.h:20-21) BEFORE
 * including the production .c files, so common.h's body -- and every header it
 * chains to -- expands to NOTHING. The harness then supplies the handful of
 * types, constants and globals the adapters actually use.
 *
 * The preprocessor must still FIND common.h before it can skip it, which is why
 * the build rule passes -I gpsp. That is a BUILD RULE change only: no shipping
 * source is modified to suit the harness, nothing is copied out of gpsp/, and
 * NO TEST-ONLY #ifdef EXISTS IN ANY PRODUCTION FILE. gba_restore_equiv appears
 * in NO object list.
 *
 * ---- WHY M12C's RUNTIME HALF *CAN* BE TESTED AND M12B's COULD NOT ----
 *
 * tools/gba_save_equiv.c:29-35 records that adapters/gba/gba_savefile.c CANNOT
 * be compiled by a harness at all, because it includes runtime/savedata.h ->
 * runtime/core.h, whose `typedef unsigned long u64` collides with the
 * `unsigned long long` this file must supply for gpSP.
 *
 * M12C's gba_restorefile.c INCLUDES NEITHER. It needs no savedata layer (the
 * container is already mounted read-only -- see gba_restorefile.h), and
 * runtime/libc/stdio.h chains to no u64. So the whole M12C stack compiles here,
 * and the two-pass ladder is tested end to end rather than by proxy.
 *
 * NO REAL SAVEDATA MOUNT IS INVOLVED. The harness writes crafted files into the
 * working directory and hands their names to gba_restorefile_with(), which is
 * the same function gba_restorefile_run() calls after building the production
 * names. The tested path IS the production path.
 *
 * ---- WHAT IS ACTUALLY BEING PROVED ----
 *
 *   1.  THE ROUND TRIP. A header built by the FROZEN M12B WRITER parses back
 *       through the M12C READER with every field identical. This is the
 *       mandatory case: the two files describe the same 32-byte layout in two
 *       places, and this is the only thing that keeps them honest.
 *   2.  THE SIZE TABLES AGREE with the frozen writer's, class by class -- and
 *       do not echo gpSP's UNIT COUNTS (EEPROM_8_KBYTE is 16, not 8192).
 *   3.  A VALID Tony-Hawk-shaped EEPROM512 save restores BYTE-EXACTLY.
 *   4.  EVERY ONE OF FIFTEEN CORRUPTIONS IS REJECTED, each with its own code.
 *   5.  PASSIVITY: every rejected input leaves all 131,072 bytes untouched.
 *   6.  VALIDATE MODE NEVER MUTATES, even on a perfectly valid file.
 *   7.  THE ARM GATE: an unarmed adapter refuses to apply, and the ladder
 *       ROLLS THE ARRAY BACK TO 0xFF rather than leaving it half written.
 *   8.  THE RESTORED BASELINE describes the restored state, not the blank one.
 *   9.  THE FNV AGREES across all four independent implementations.
 *  10.  ***** THE ROM-BOUND EEPROM FAMILY RULE, IN BOTH DIRECTIONS. *****
 *       An EEPROM save bound to THIS EXACT ROM is accepted against a live class
 *       of UNKNOWN -- and SRAM, FLASH, a stored UNKNOWN, a wrong ROM, a stale
 *       payload and a bad geometry are ALL still refused under that same live
 *       UNKNOWN. The legacy gba_savehdr_compatible() is proved unchanged over
 *       every one of the 36 class pairs, and NOT ONE gpSP classification global
 *       is seeded by any accepted restore.
 *  11.  ***** THE DATABASE-FLASH128 EVIDENCE RULE (M15). ***** A FLASH128 save
 *       bound to THIS EXACT ROM is accepted against a live UNKNOWN **only**
 *       when gpSP's own gba_over.h database has asserted a 128 KB flash part
 *       and left the type unset. The BYTE-IDENTICAL header on a cartridge
 *       without that evidence is still REFUSED, FLASH64 is still refused with
 *       or without it, the legacy entry points are proved bit-identical over
 *       all 36 pairs x both binding states, and the full
 *       COMMIT -> RELAUNCH -> RESTORE round trip is driven through the FROZEN
 *       writer and the shipping reader.
 *  12.  ***** THE FLASH64 SESSION-REUSE RULE (M16-2), IN BOTH BUILDS. *****
 *       A FLASH64 save bound to THIS EXACT ROM is accepted against a live
 *       UNKNOWN -- and the emulator's save type is ACTIVATED -- ONLY when this
 *       file is compiled with LUAPORT_SESSION_REUSE. See [10f].
 *
 * ---- THIS HARNESS IS COMPILED TWICE, AND THE TWO RUNS MEAN DIFFERENT THINGS
 *
 * `make m12c-equiv` builds it WITHOUT LUAPORT_SESSION_REUSE. That is the
 * configuration M13C, M12C, M12B and M11 are built in, and its verdicts are the
 * HISTORICAL ones, unchanged: FLASH64 under a live UNKNOWN is REFUSED, and
 * NOTHING seeds any gpSP classification global on any path.
 *
 * `make m16c-save-equiv` builds THE SAME SOURCE with -DLUAPORT_SESSION_REUSE,
 * which is how M16C compiles the three shared adapters. Only there is the
 * FLASH64 rule present.
 *
 * ***** THE TWO RUNS SHARE ONE FILE ON PURPOSE. ***** Every case that is NOT
 * configuration-dependent is written once and asserted in both, so "the
 * correction did not disturb EEPROM, SRAM or FLASH128" is a measurement taken
 * in both builds rather than a claim made about one. The handful of cases whose
 * verdict genuinely differs are bracketed, and each bracket says why.
 *
 * ***** NO PRODUCTION LOGIC IS REIMPLEMENTED HERE. ***** The shipping .c files
 * are #included verbatim, so both runs exercise the code that ships. The
 * #ifdefs in THIS FILE select EXPECTATIONS; they do not supply behaviour, and
 * no test-only #ifdef exists in any production source.
 *
 * ***** EVERY FIXTURE IS SYNTHETIC. ***** Cartridges and payloads are generated
 * here from fixed seeds. No commercial save data is read, embedded, committed
 * or distributed by this harness.
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

/* ---- neutralise gpsp/common.h ------------------------------------------- */
#define COMMON_H

/* ---- the types gpSP would have supplied ---------------------------------
 * NOTE u64 IS `unsigned long long` HERE, matching gpsp/common.h:100 and NOT
 * runtime/core.h:32. */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;

/* ---- gpsp/gba_memory.h's constants, transcribed ------------------------- */
#define BACKUP_SRAM       0
#define BACKUP_FLASH      1
#define BACKUP_EEPROM     2
#define BACKUP_UNKN       3

#define FLASH_SIZE_64KB   1
#define FLASH_SIZE_128KB  2

#define EEPROM_512_BYTE   1
#define EEPROM_8_KBYTE   16

/* ---- the gpSP globals the adapters read --------------------------------- */
u32  gamepak_size         = 0;
u32  gamepak_buffer_count = 0;
u32  backup_type          = BACKUP_UNKN;
u32  flash_bank_cnt       = FLASH_SIZE_64KB;
u32  eeprom_size          = EEPROM_512_BYTE;

/* ***** THE RESET SEED, DEFINED SO IT CAN BE PROVED UNWRITTEN. *****
 *
 * gpSP's init_memory() copies this into backup_type on EVERY reset
 * (gba_memory.c). NO ADAPTER IN THIS TREE MAY EVER WRITE IT: a class derived
 * from a FILE must not survive a reset, because after the reset the file's
 * authority has not been re-established and gpSP would be booting a cartridge
 * pre-classified by something it never observed.
 *
 * Nothing in production references it today, and the FLASH64 activation added
 * under LUAPORT_SESSION_REUSE deliberately does not either. It is defined HERE
 * purely so the assertions below can measure that -- an invariant nobody can
 * check is an invariant nobody is keeping. It is initialised once and asserted
 * to be BACKUP_UNKN after every accepted restore, in BOTH configurations. */
u32  backup_type_reset    = BACKUP_UNKN;
u8   gamepak_backup[1024 * 128];

u8  *gamepak_buffers[32];
const unsigned gamepak_buffer_blocksize = 1024 * 1024;

/* ---- THE PRODUCTION ADAPTERS, INCLUDED VERBATIM ------------------------- */
#include "gba_save.c"          /* FROZEN M12B writer -- for the ROUND TRIP  */
#include "gba_savehdr.c"
#include "gba_restore.c"
#include "gba_restorefile.c"

/* THE SRAM UPPER-HALF OBSERVER, INCLUDED THE SAME WAY AND FOR THE SAME REASON.
 *
 * It opens with #include "common.h" exactly as gba_save.c and gba_restore.c do,
 * so the COMMON_H guard defined above makes it expand to nothing while the
 * preprocessor still finds the file. It needs only gamepak_backup[], which is
 * the real 131,072-byte array defined at line 116 -- so sizeof(gamepak_backup)
 * inside the observer's bounds check means here exactly what it means in the
 * shipping image.
 *
 * ***** THE OBSERVER IS TESTED FOR PASSIVITY, NOT MERELY FOR ARITHMETIC. *****
 * Section 15 byte-compares all 131,072 bytes across every sample call. An
 * observer that perturbed the array it measures would corrupt the very run it
 * was added to explain, and `const` in the source is a claim that only a test
 * over the ACTUAL BYTES can settle. */
#include "gba_sramobs.c"

/* ========================================================================= */

static int failures = 0;
static int checks   = 0;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void ck_u(unsigned int got, unsigned int want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s -- got %u, want %u\n", what, got, want);
    }
}

static void ck_i(int got, int want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s -- got %d, want %d\n", what, got, want);
    }
}

/* AN INDEPENDENT FNV-1a, written from the specification rather than copied
 * from any adapter. There are now FOUR implementations of this function in the
 * tree (gba_save.c, gba_savefile.c, gba_savehdr.c, gba_restore.c) and they must
 * all agree: if they ever diverge, a save written by M12B would be rejected by
 * M12C with a "payload hash mismatch" that pointed at the data instead of at
 * the code. */
static unsigned int ref_fnv(const unsigned char *p, unsigned int n)
{
    unsigned int h = 2166136261u;
    unsigned int i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned int)p[i];
        h *= 16777619u;
    }
    return h;
}

/* A fake 2 MB cartridge, DELIBERATELY LARGER THAN ONE BUFFER so the identity
 * hash's block-size bound is exercised exactly as it is in M12B's harness. */
static unsigned char rom_image[2 * 1024 * 1024];

/* The crafted files. Written into the working directory; the build rule runs
 * the harness from the repository root and removes them afterwards. */
#define T_SAV "m12c_equiv_test.sav"
#define T_HDR "m12c_equiv_test.hdr"

/* ---- RESET EVERY PIECE OF MODULE STATE ---------------------------------
 *
 * All four adapters keep file-scope state, and gba_save_latch() is deliberately
 * idempotent-by-refusal, so a harness testing more than one scenario MUST clear
 * it. It can, because #include'ing the .c files puts these statics in this
 * translation unit. */
static void reset_all(void)
{
    unsigned int i;

    gs_latched      = 0;
    gs_rom_hash     = 0;
    gs_hashed_bytes = 0;
    gs_sanitised    = 0;
    gs_lat_backup   = 0;
    gs_lat_bankcnt  = 0;
    gs_lat_eepsize  = 0;
    gs_lat_class    = GBA_SAVE_UNKNOWN;
    gs_lat_region   = 0;
    gs_base_hash    = 0;
    gs_exit_hash    = 0;
    gs_exit_taken   = 0;
    gs_samples      = 0;
    gs_active       = 0;
    gs_last_sample  = 0;
    for (i = 0; i < GBA_SAVE_ID_MAX; i++) gs_id[i] = '\0';

    gr_armed      = 0;
    gr_applied    = 0;
    gr_rollbacks  = 0;
    gr_base_kind  = GBA_RESTORE_BASE_NONE;
    gr_base_hash  = 0;
    gr_exit_hash  = 0;
    gr_exit_taken = 0;

    rf_reset();

    for (i = 0; i < GBA_RESTORE_BACKUP_BYTES; i++)
        gamepak_backup[i] = 0xFF;
}

/* Bring up a Tony-Hawk-shaped cartridge: code "ATHE" at 0xAC, EEPROM detected
 * at gpSP's DEFAULT 512-byte size -- which is exactly the hardware result M11
 * and M12B both reported. */
static void setup_cart(u32 backup, u32 bankcnt, u32 eepsize)
{
    unsigned int i;

    for (i = 0; i < sizeof rom_image; i++)
        rom_image[i] = (unsigned char)(i * 7u + 3u);

    rom_image[0xAC] = 'A';
    rom_image[0xAD] = 'T';
    rom_image[0xAE] = 'H';
    rom_image[0xAF] = 'E';

    gamepak_buffers[0]   = rom_image;
    gamepak_buffer_count = 2;
    gamepak_size         = 2 * 1024 * 1024;

    backup_type    = backup;
    flash_bank_cnt = bankcnt;
    eeprom_size    = eepsize;

    reset_all();
    gba_save_latch();
}

/* ***** A SECOND (AND THIRD) CARTRIDGE IDENTITY. *****
 *
 * setup_cart() above always builds the SAME synthetic ROM: the fill pattern is
 * fixed and the code at 0xAC is always "ATHE". That is exactly right for every
 * pre-existing case, which tests POLICY rather than IDENTITY -- but it means a
 * fixture can never prove that the ladder actually consulted the cartridge in
 * front of it. With one identity, "accepted" and "accepted because the ROM
 * happened to match" are indistinguishable.
 *
 * THIS BUILDS A DISTINCT CARTRIDGE: a distinct 2 MB body from a per-identity
 * LCG seed, and a distinct four-character code. Two such cartridges produce two
 * DIFFERENT gba_save_rom_hash() values, which is what lets the cases below
 * offer identity A's save to cartridge B and require a refusal.
 *
 * IT CHANGES NOTHING FOR ANY EXISTING CASE. setup_cart() is untouched and
 * re-fills rom_image[] completely on every call, so a scenario that runs after
 * one of these is handed exactly the cartridge it always was. */
static void setup_cart_code(u32 backup, u32 bankcnt, u32 eepsize,
                            const char *code, unsigned int seed)
{
    unsigned int i;
    unsigned int s = seed;

    for (i = 0; i < sizeof rom_image; i++) {
        s = s * 1103515245u + 12345u;
        rom_image[i] = (unsigned char)(s >> 16);
    }

    rom_image[0xAC] = (unsigned char)code[0];
    rom_image[0xAD] = (unsigned char)code[1];
    rom_image[0xAE] = (unsigned char)code[2];
    rom_image[0xAF] = (unsigned char)code[3];

    gamepak_buffers[0]   = rom_image;
    gamepak_buffer_count = 2;
    gamepak_size         = 2 * 1024 * 1024;

    backup_type    = backup;
    flash_bank_cnt = bankcnt;
    eeprom_size    = eepsize;

    reset_all();
    gba_save_latch();
}

/* ---- crafting files ----------------------------------------------------- */

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

/* A HAND-BUILT header, so every field can be corrupted independently. It is
 * deliberately NOT produced by gba_save_header_build() -- that writer is
 * exercised separately by the ROUND TRIP case, and using it here would make it
 * impossible to craft the invalid inputs this harness exists to reject. */
static void mk_hdr(unsigned char *h, unsigned int version, unsigned int cls,
                   unsigned int region, unsigned int declared,
                   unsigned int romfnv, unsigned int payfnv,
                   unsigned int conf, int magic_ok, unsigned int reserved0)
{
    unsigned int i;

    for (i = 0; i < GBA_SAVEHDR_BYTES; i++) h[i] = 0;

    h[0] = magic_ok ? 'L' : 'X';
    h[1] = 'G';
    h[2] = 'S';
    h[3] = '1';

    put16(&h[GBA_SAVEHDR_O_VERSION],  version);
    put16(&h[GBA_SAVEHDR_O_CLASS],    cls);
    put32(&h[GBA_SAVEHDR_O_REGION],   region);
    put32(&h[GBA_SAVEHDR_O_DECLARED], declared);
    put32(&h[GBA_SAVEHDR_O_ROMFNV],   romfnv);
    put32(&h[GBA_SAVEHDR_O_PAYFNV],   payfnv);
    put16(&h[GBA_SAVEHDR_O_CONF],     conf);

    h[GBA_SAVEHDR_O_RESERVED] = (unsigned char)reserved0;
}

static int write_file(const char *name, const unsigned char *data,
                      unsigned int n)
{
    FILE *f = fopen(name, "wb");
    size_t w;
    if (!f) return 0;
    w = fwrite(data, 1, (size_t)n, f);
    fclose(f);
    return (w == (size_t)n);
}

static void remove_files(void)
{
    remove(T_SAV);
    remove(T_HDR);
}

/* The reference payload: 8192 bytes shaped like a real EEPROM save -- a
 * populated head and a 0xFF tail, which is what a 512-byte chip's 8192-byte
 * window actually looks like. */
static unsigned char payload[8192];

static void mk_payload(void)
{
    unsigned int i;
    for (i = 0; i < sizeof payload; i++)
        payload[i] = (i < 466u) ? (unsigned char)(i * 13u + 5u) : 0xFF;
}

/* THE SECOND REFERENCE PAYLOAD: 32,768 bytes shaped like a real SRAM save --
 * THE METROID FUSION CASE. A full 32 KB region is the only size an SRAM header
 * may declare (gba_savehdr_region_of(CLS_SRAM) == 32768), so the EEPROM-shaped
 * 8192-byte payload above cannot be reused for it: the .sav would be rejected
 * at the SIZE gate long before the compatibility gate under test is reached.
 *
 * A DIFFERENT FILL PATTERN FROM payload[] ON PURPOSE. If the two ever hashed
 * alike, a byte-exactness assertion could pass while restoring the wrong
 * buffer. The tail is 0xFF because that is what an unwritten SRAM region
 * genuinely looks like after m4_rom_backup_init(). */
static unsigned char sram_payload[32768];

static void mk_sram_payload(void)
{
    unsigned int i;
    for (i = 0; i < sizeof sram_payload; i++)
        sram_payload[i] = (i < 20000u) ? (unsigned char)(i * 31u + 17u) : 0xFF;
}

/* THE THIRD REFERENCE PAYLOAD: 131,072 bytes shaped like a real FLASH128 save --
 * THE SUPER MARIO ADVANCE 4 CASE. 131072 is the only region a FLASH128 header
 * may declare (gba_savehdr_region_of(CLS_FLASH128) == 131072), so neither
 * smaller payload above can stand in for it: the .sav would be rejected at the
 * SIZE gate long before the compatibility gate under test is reached.
 *
 * ***** IT IS THE FULL ARRAY, WHICH MAKES ONE ASSERTION IMPOSSIBLE. ***** A
 * 128 KB region fills gamepak_backup[] exactly, so there is NO tail above the
 * region to check for 0xFF -- unlike the 8 KB and 32 KB cases. What replaces
 * that assertion is a whole-array hash comparison, which is strictly stronger.
 *
 * BOTH BANKS ARE POPULATED, AND DIFFERENTLY. Bytes 65536..131071 are the upper
 * flash bank -- the half reachable only after the game issues the 0xB0 bank
 * switch. Filling it with a DISTINCT pattern is what lets the byte-exactness
 * check below prove the upper bank survived the round trip rather than merely
 * that the first 64 KB did. A fill that repeated every 64 KB would pass a
 * bank-confusion bug silently. */
static unsigned char flash_payload[131072];

static void mk_flash_payload(void)
{
    unsigned int i;
    for (i = 0; i < sizeof flash_payload; i++) {
        if (i < 65536u)
            flash_payload[i] = (unsigned char)(i * 11u + 23u);   /* bank 0 */
        else
            flash_payload[i] = (unsigned char)(i * 29u + 131u);  /* bank 1 */
    }
}

/* THE FOURTH AND FIFTH REFERENCE PAYLOADS: 65,536 bytes each, shaped like real
 * FLASH64 saves -- THE MOTHER 3 (A3UJ) AND THE SIMS 2 (B46E) CASES.
 *
 * 65536 is the only region a FLASH64 header may declare
 * (gba_savehdr_region_of(CLS_FLASH64) == 65536), so none of the three payloads
 * above can stand in for one: the .sav would be rejected at the SIZE gate long
 * before the compatibility gate under test is reached.
 *
 * ***** THESE ARE SYNTHETIC FIXTURES. ***** Not one byte comes from a
 * commercial save. Both are generated here by a plain LCG so the harness is
 * self-contained and reproducible, and NOTHING derived from a real cartridge's
 * save data is read, committed or distributed by this file.
 *
 * ***** TWO OF THEM, WITH DIFFERENT PATTERNS, BECAUSE THERE ARE TWO IDENTITIES.
 * ***** The FLASH64 cases below are run against two DIFFERENT synthetic ROMs
 * with two DIFFERENT payloads. A single fixture could pass while the ladder
 * silently ignored the identity it was handed; two cannot. If the two payloads
 * ever hashed alike, a byte-exactness assertion could pass while the restore
 * had loaded the wrong buffer, so the seeds are deliberately distinct. */
static unsigned char flash64_payload_a[65536];
static unsigned char flash64_payload_b[65536];

static void mk_flash64_payload(unsigned char *p, unsigned int n,
                               unsigned int seed)
{
    unsigned int i;
    unsigned int s = seed;

    for (i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        /* A POPULATED BODY AND A 0xFF TAIL, which is what a real 64 KB flash
         * part looks like once a game has written part of it. */
        p[i] = (i < 48000u) ? (unsigned char)(s >> 16) : 0xFF;
    }
}

/* Is the whole backup array still the untouched 0xFF idle state? THE PASSIVITY
 * ASSERTION, used after every rejection. */
static int backup_is_blank(void)
{
    unsigned int i;
    for (i = 0; i < GBA_RESTORE_BACKUP_BYTES; i++)
        if (gamepak_backup[i] != 0xFF)
            return 0;
    return 1;
}

/* Run one REJECTION scenario end to end and assert both the code AND that
 * nothing was touched. Every rejection case funnels through here so the
 * passivity assertion cannot be forgotten for one of them. */
static void expect_reject(const char *what, const unsigned char *hdr,
                          unsigned int hdrn,
                          const unsigned char *sav, unsigned int savn,
                          int want_rc)
{
    int rc;

    /* ARMED ON PURPOSE. A rejection must hold even when the adapter is fully
     * permitted to write -- proving the refusal comes from the VALIDATION and
     * not from the arm gate happening to be closed. */
    gba_restore_arm();

    if (hdr) ck(write_file(T_HDR, hdr, hdrn), "write hdr");
    if (sav) ck(write_file(T_SAV, sav, savn), "write sav");

    rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

    ck_i(rc, want_rc, what);
    ck(backup_is_blank(), "PASSIVITY: a rejected save left the backup untouched");
    ck_u(gba_restore_applied(), 0u, "a rejected save applied zero bytes");

    remove_files();
}

/* ---- THE OBSERVER'S PASSIVITY, ENFORCED AT EVERY SINGLE CALL SITE -------
 *
 * m12c_sramobs_sample() is the ONLY thing in M12C that touches gamepak_backup[]
 * while the emulator is live. gba_restore_apply() is the sole WRITER and runs
 * before execution; the observer runs beside a running game. If it perturbed
 * one byte it would corrupt the very session it was added to explain, and the
 * corruption would look exactly like the bug under investigation.
 *
 * `const u8 *` in gba_sramobs.c is a claim. THIS IS THE PROOF -- a full
 * 131,072-byte comparison across every sample, not just across the 65,536 the
 * observer is supposed to look at. Reading past the window would be as much of
 * a defect as writing inside it, and only a whole-array compare catches a
 * stray write that landed in the part nobody is watching. */
static unsigned char obs_snap[GBA_RESTORE_BACKUP_BYTES];

static void obs_sample_passive(unsigned int slot)
{
    unsigned int i;
    int same = 1;

    memcpy(obs_snap, gamepak_backup, GBA_RESTORE_BACKUP_BYTES);

    m12c_sramobs_sample(slot);

    for (i = 0; i < GBA_RESTORE_BACKUP_BYTES; i++)
        if (gamepak_backup[i] != obs_snap[i]) { same = 0; break; }

    ck(same, "PASSIVITY: the observer modified NO byte of gamepak_backup");
}

/* ========================================================================= */

int main(void)
{
    unsigned char hdr[GBA_SAVEHDR_BYTES + 8];
    unsigned int  romfnv, payfnv, sramfnv, flashfnv;
    unsigned int  i;
    int rc;

    printf("gba_restore_equiv -- M12C offline restore harness\n");
    printf("  testing the SHIPPING adapters, included verbatim\n");
#ifdef LUAPORT_SESSION_REUSE
    printf("  BUILD CONFIGURATION: LUAPORT_SESSION_REUSE **DEFINED**"
           " -- the M16C save gate\n\n");
#else
    printf("  BUILD CONFIGURATION: LUAPORT_SESSION_REUSE absent"
           " -- the frozen-milestone gate\n\n");
#endif

    mk_payload();
    payfnv = ref_fnv(payload, (unsigned int)sizeof payload);

    mk_sram_payload();
    sramfnv = ref_fnv(sram_payload, (unsigned int)sizeof sram_payload);

    mk_flash_payload();
    flashfnv = ref_fnv(flash_payload, (unsigned int)sizeof flash_payload);

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    romfnv = gba_save_rom_hash();

    printf("  cartridge: id '%s', rom fnv 0x%08X, payload fnv 0x%08X\n\n",
           gs_id, romfnv, payfnv);

    /* ===================================================================
     * 1. THE FNV AGREES ACROSS ALL FOUR IMPLEMENTATIONS
     * =================================================================== */
    printf("[1] FNV agreement across every implementation\n");
    ck_u(gba_savehdr_fnv_add(GBA_SAVEHDR_FNV_OFFSET, payload,
                             (unsigned int)sizeof payload),
         payfnv, "gba_savehdr_fnv_add agrees with the reference");
    ck_u(gs_fnv(payload, (unsigned int)sizeof payload), payfnv,
         "the FROZEN writer's gs_fnv agrees with the reference");
    ck_u(gr_fnv(payload, (unsigned int)sizeof payload), payfnv,
         "gba_restore.c's gr_fnv agrees with the reference");
    ck_u(GBA_SAVEHDR_FNV_OFFSET, 2166136261u, "the FNV offset basis is correct");
    ck_u(GBA_SAVEHDR_FNV_PRIME, 16777619u, "the FNV prime is correct");

    /* ===================================================================
     * 2. THE SIZE TABLES AGREE WITH THE FROZEN WRITER, CLASS BY CLASS
     *
     * If these ever drift, M12C rejects every file M12B writes -- or worse,
     * accepts one with the wrong length and restores the wrong byte count.
     * =================================================================== */
    printf("[2] the reader's size tables match the FROZEN writer's\n");
    for (i = 0; i <= GBA_SAVEHDR_CLS_MAX; i++) {
        ck_u(gba_savehdr_region_of(i), gs_region_of(i),
             "region_of matches the frozen writer");
        ck_u(gba_savehdr_declared_of(i), gs_declared_of(i),
             "declared_of matches the frozen writer");
    }
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_EEPROM512), 8192u,
         "EEPROM512 persists 8192, NOT 512");
    ck_u(gba_savehdr_declared_of(GBA_SAVEHDR_CLS_EEPROM512), 512u,
         "EEPROM512 declares 512");
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_EEPROM8K), 8192u,
         "EEPROM8K persists 8192");
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_SRAM), 32768u, "SRAM 32768");
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_FLASH64), 65536u, "FLASH64");
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_FLASH128), 131072u, "FLASH128");
    ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_UNKNOWN), 0u,
         "UNKNOWN has no region -- it is never restorable");

    /* THE UNIT-COUNT TRAP. Neither table may ever return a gpSP unit count. */
    ck(gba_savehdr_region_of(GBA_SAVEHDR_CLS_EEPROM8K) != EEPROM_8_KBYTE,
       "the region is NOT eeprom_size's unit count (16)");
    ck(gba_savehdr_region_of(GBA_SAVEHDR_CLS_FLASH128) != FLASH_SIZE_128KB,
       "the region is NOT flash_bank_cnt's unit count (2)");

    /* Every region is inside the array. */
    for (i = 0; i <= GBA_SAVEHDR_CLS_MAX; i++)
        ck(gba_savehdr_region_of(i) <= GBA_SAVEHDR_BACKUP_BYTES,
           "no region exceeds gamepak_backup[]");

    /* ===================================================================
     * 3. ***** THE ROUND TRIP -- THE MANDATORY CASE *****
     *
     * The 32-byte layout is described in TWO places: the frozen writer
     * (gba_save.c) and the M12C reader (gba_savehdr.c). They are deliberately
     * not sharing a header, so this is the ONLY thing that keeps them from
     * drifting apart. A drift would make M12C reject every real save.
     * =================================================================== */
    printf("[3] ROUND TRIP: the FROZEN writer's output parses back identically\n");
    {
        unsigned char built[GBA_SAVE_HDR_BYTES];
        struct gba_savehdr parsed;

        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);

        ck_u(gba_save_header_build(built, (unsigned int)sizeof built, payfnv),
             GBA_SAVE_HDR_BYTES, "the frozen writer produced 32 bytes");

        ck_i(gba_savehdr_parse(built, GBA_SAVE_HDR_BYTES, &parsed),
             GBA_SAVEHDR_OK, "the M12C reader ACCEPTS the frozen writer's header");

        ck_u(parsed.version,  1u,                        "round trip: version");
        ck_u(parsed.cls,      GBA_SAVE_EEPROM512,        "round trip: class");
        ck_u(parsed.region,   8192u,                     "round trip: region");
        ck_u(parsed.declared, 512u,                      "round trip: declared");
        ck_u(parsed.rom_fnv,  gba_save_rom_hash(),       "round trip: ROM fnv");
        ck_u(parsed.pay_fnv,  payfnv,                    "round trip: payload fnv");
        ck_u(parsed.confidence, gba_save_confidence(),   "round trip: confidence");

        /* The offsets the two files agree on, asserted rather than assumed. */
        ck_u(GBA_SAVEHDR_BYTES,      GBA_SAVE_HDR_BYTES,     "hdr size agrees");
        ck_u(GBA_SAVEHDR_O_VERSION,  GBA_SAVE_HDR_O_VERSION, "version offset");
        ck_u(GBA_SAVEHDR_O_CLASS,    GBA_SAVE_HDR_O_CLASS,   "class offset");
        ck_u(GBA_SAVEHDR_O_REGION,   GBA_SAVE_HDR_O_REGION,  "region offset");
        ck_u(GBA_SAVEHDR_O_DECLARED, GBA_SAVE_HDR_O_DECLARED,"declared offset");
        ck_u(GBA_SAVEHDR_O_ROMFNV,   GBA_SAVE_HDR_O_ROMFNV,  "romfnv offset");
        ck_u(GBA_SAVEHDR_O_PAYFNV,   GBA_SAVE_HDR_O_PAYFNV,  "payfnv offset");
        ck_u(GBA_SAVEHDR_O_CONF,     GBA_SAVE_HDR_O_CONF,    "conf offset");
        ck_u(GBA_SAVEHDR_O_RESERVED, GBA_SAVE_HDR_O_RESERVED,"reserved offset");

        /* The class numbering is shared across M11/M12B/M12C on purpose. */
        ck_u(GBA_SAVEHDR_CLS_EEPROM512, GBA_SAVE_EEPROM512, "class 4 agrees");
        ck_u(GBA_SAVEHDR_CLS_FLASH128,  GBA_SAVE_FLASH128,  "class 3 agrees");
    }

    /* ===================================================================
     * 4. CLASS COMPATIBILITY -- BY FAMILY, NOT BY EXACT CLASS
     * =================================================================== */
    printf("[4] class compatibility is by FAMILY\n");
    ck(gba_savehdr_compatible(GBA_SAVEHDR_CLS_EEPROM512,
                              GBA_SAVEHDR_CLS_EEPROM512),
       "EEPROM512 header into a live EEPROM512 -- the Tony Hawk case");
    ck(gba_savehdr_compatible(GBA_SAVEHDR_CLS_EEPROM8K,
                              GBA_SAVEHDR_CLS_EEPROM512),
       "EEPROM8K header into a live EEPROM512 -- reset re-defaults the size");
    ck(gba_savehdr_compatible(GBA_SAVEHDR_CLS_FLASH128,
                              GBA_SAVEHDR_CLS_FLASH64),
       "FLASH128 header into a live FLASH64 -- the bank switch has not run yet");
    ck(gba_savehdr_compatible(GBA_SAVEHDR_CLS_SRAM, GBA_SAVEHDR_CLS_SRAM),
       "SRAM into SRAM");
    ck(!gba_savehdr_compatible(GBA_SAVEHDR_CLS_EEPROM512,
                               GBA_SAVEHDR_CLS_SRAM),
       "EEPROM into SRAM is REFUSED");
    ck(!gba_savehdr_compatible(GBA_SAVEHDR_CLS_SRAM,
                               GBA_SAVEHDR_CLS_FLASH64),
       "SRAM into FLASH is REFUSED");
    ck(!gba_savehdr_compatible(GBA_SAVEHDR_CLS_EEPROM512,
                               GBA_SAVEHDR_CLS_UNKNOWN),
       "a LIVE UNKNOWN is REFUSED unconditionally");
    ck(!gba_savehdr_compatible(GBA_SAVEHDR_CLS_UNKNOWN,
                               GBA_SAVEHDR_CLS_EEPROM512),
       "a STORED UNKNOWN is REFUSED unconditionally");

    /* ===================================================================
     * 4b. ***** THE ROM-BOUND FORM -- THE M12C POLICY CORRECTION *****
     *
     * THE OLD API IS PROVED UNCHANGED FIRST, EXHAUSTIVELY. Every one of the
     * 36 (header, live) class pairs must give gba_savehdr_compatible() the
     * same answer as the un-bound gba_savehdr_compatible_bound(h, l, 0).
     * That is what makes the delegation a refactor rather than a behaviour
     * change, and it is why the eight assertions ABOVE keep their original
     * verdicts.
     * =================================================================== */
    printf("[4b] the ROM-BOUND form: EEPROM and SRAM only, and ONLY under a live UNKNOWN\n");
    {
        unsigned int hc, lc;

        /* ---- THE OLD API IS BIT-FOR-BIT THE UN-BOUND FORM -------------- */
        for (hc = 0; hc <= GBA_SAVEHDR_CLS_MAX; hc++)
            for (lc = 0; lc <= GBA_SAVEHDR_CLS_MAX; lc++)
                ck_i(gba_savehdr_compatible(hc, lc),
                     gba_savehdr_compatible_bound(hc, lc, 0),
                     "the legacy API equals the UN-bound form for every pair");

        /* ---- WITH rom_bound, ONLY THE LIVE-UNKNOWN CELLS MAY DIFFER ----
         *
         * For every pair whose LIVE class is a real family, binding the ROM
         * must change NOTHING. This is what proves the carve-out cannot
         * relax a genuine family mismatch. */
        for (hc = 0; hc <= GBA_SAVEHDR_CLS_MAX; hc++)
            for (lc = 1; lc <= GBA_SAVEHDR_CLS_MAX; lc++)
                ck_i(gba_savehdr_compatible_bound(hc, lc, 1),
                     gba_savehdr_compatible_bound(hc, lc, 0),
                     "rom_bound changes NOTHING when the live class is known");

        /* ---- THE CELLS THAT CHANGED ------------------------------------
         *
         * TWO FAMILIES, THREE CELLS. The EEPROM pair was M12C's original
         * correction; the SRAM cell is the Metroid Fusion correction and is
         * the ONLY assertion in this harness whose verdict was deliberately
         * INVERTED from the previous baseline. See section 10 case (9) for
         * the full rationale and the hardware evidence behind it. */
        ck(gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_EEPROM512,
                                        GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "***** EEPROM512 header + live UNKNOWN + ROM BOUND is ACCEPTED *****");
        ck(gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_EEPROM8K,
                                        GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "EEPROM8K header + live UNKNOWN + ROM BOUND is ACCEPTED");
        ck(gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                        GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "***** SRAM header + live UNKNOWN + ROM BOUND is ACCEPTED *****");

        /* ---- AND EVERYTHING AROUND IT THAT DID NOT --------------------- */
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_EEPROM512,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 0),
           "EEPROM + live UNKNOWN WITHOUT the ROM binding is still REFUSED");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 0),
           "***** SRAM + live UNKNOWN WITHOUT the ROM binding is still REFUSED *****");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_FLASH64,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "***** FLASH64 + live UNKNOWN is REFUSED EVEN WHEN ROM-BOUND *****");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_FLASH128,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "***** FLASH128 + live UNKNOWN is REFUSED EVEN WHEN ROM-BOUND *****");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_UNKNOWN,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "a STORED UNKNOWN is refused even bound -- binding supplies no class");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_UNKNOWN,
                                         GBA_SAVEHDR_CLS_EEPROM512, 1),
           "a STORED UNKNOWN is refused against a live EEPROM even bound");

        /* A REAL MISMATCH STAYS A MISMATCH, bound or not. */
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_EEPROM512,
                                         GBA_SAVEHDR_CLS_SRAM, 1),
           "EEPROM header + live SRAM is REFUSED even when ROM-BOUND");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_EEPROM512,
                                         GBA_SAVEHDR_CLS_FLASH64, 1),
           "EEPROM header + live FLASH is REFUSED even when ROM-BOUND");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                         GBA_SAVEHDR_CLS_FLASH128, 1),
           "SRAM header + live FLASH is REFUSED even when ROM-BOUND");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_FLASH64,
                                         GBA_SAVEHDR_CLS_SRAM, 1),
           "FLASH header + live SRAM is REFUSED even when ROM-BOUND");

        /* ---- THE SRAM HEADER AGAINST EVERY OTHER *KNOWN* LIVE FAMILY ---
         *
         * WIDENING THE LIVE-UNKNOWN CELL MUST NOT HAVE WIDENED ANY OTHER.
         * Together with the FLASH128 case just above, these enumerate EVERY
         * known live family an SRAM header can meet: FLASH64, FLASH128,
         * EEPROM512 and EEPROM8K are all genuine mismatches the ROM binding is
         * explicitly NOT allowed to excuse, and SRAM-into-SRAM is the one that
         * was always legitimate and must stay so. */
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                         GBA_SAVEHDR_CLS_FLASH64, 1),
           "SRAM header + live FLASH64 is REFUSED even when ROM-BOUND");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                         GBA_SAVEHDR_CLS_EEPROM512, 1),
           "SRAM header + live EEPROM512 is REFUSED even when ROM-BOUND");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                         GBA_SAVEHDR_CLS_EEPROM8K, 1),
           "SRAM header + live EEPROM8K is REFUSED even when ROM-BOUND");
        ck(gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_SRAM,
                                        GBA_SAVEHDR_CLS_SRAM, 1),
           "SRAM header + live SRAM is still ACCEPTED when ROM-BOUND");

        /* ---- ***** THE FLASH128 REFUSAL IS *NOT* RELAXED HERE ***** -----
         *
         * THE TWO ASSERTIONS ABOVE AT FLASH64/FLASH128 ARE DELIBERATELY LEFT
         * INVERTED-FREE. The M15 correction did NOT widen this function: it
         * added a SEPARATE entry point that requires positive evidence. So
         * gba_savehdr_compatible_bound(FLASH128, UNKNOWN, 1) must STILL be 0,
         * and the case above proves it. Re-stated here as the scope pin. */
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_FLASH128,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "***** THE LEGACY BOUND FORM STILL REFUSES FLASH128 + UNKNOWN *****");
    }

    /* ===================================================================
     * 4c. ***** THE EVIDENCE-TAKING FORM -- THE M15 POLICY CORRECTION *****
     *
     * gba_savehdr_compatible_ev() is the only function that can admit a
     * FLASH128 header under a live UNKNOWN, and it does so ONLY when handed
     * GBA_SAVEHDR_EV_DB_FLASH128 -- gpSP's own database assertion of a 128 KB
     * flash part whose TYPE it then failed to record (gba_memory.c:1727).
     *
     * THE DELEGATION IS PROVED A REFACTOR FIRST, EXHAUSTIVELY, exactly as the
     * ROM-bound form was at [4b]. Only then are the new cells examined.
     * =================================================================== */
    printf("[4c] the EVIDENCE form: FLASH128 + live UNKNOWN, and ONLY with\n");
    printf("     gpSP's own database evidence\n");
    {
        unsigned int hc, lc;
        int rb;

        /* ---- WITH NO EVIDENCE IT *IS* THE ROM-BOUND FORM, BIT FOR BIT ---
         *
         * All 36 pairs, both binding states, 72 comparisons. This is what
         * makes gba_savehdr_compatible_bound()'s new one-line body a
         * REFACTOR rather than a behaviour change -- and therefore what
         * keeps every pre-existing verdict in this file honest. */
        for (hc = 0; hc <= GBA_SAVEHDR_CLS_MAX; hc++)
            for (lc = 0; lc <= GBA_SAVEHDR_CLS_MAX; lc++)
                for (rb = 0; rb <= 1; rb++)
                    ck_i(gba_savehdr_compatible_bound(hc, lc, rb),
                         gba_savehdr_compatible_ev(hc, lc, rb,
                                                   GBA_SAVEHDR_EV_NONE),
                         "EV_NONE reproduces the ROM-BOUND form for every pair");

        /* ---- EVIDENCE CHANGES NOTHING WHEN THE LIVE CLASS IS KNOWN ------
         *
         * The evidence is consulted ONLY inside the live-UNKNOWN branch, so a
         * cartridge gpSP has actually classified must be completely unaffected
         * by it. This is the assertion that stops the new bit from ever
         * excusing a real family mismatch. */
        for (hc = 0; hc <= GBA_SAVEHDR_CLS_MAX; hc++)
            for (lc = 1; lc <= GBA_SAVEHDR_CLS_MAX; lc++)
                for (rb = 0; rb <= 1; rb++)
                    ck_i(gba_savehdr_compatible_ev(hc, lc, rb,
                                                   GBA_SAVEHDR_EV_DB_FLASH128),
                         gba_savehdr_compatible_bound(hc, lc, rb),
                         "evidence changes NOTHING when the live class is known");

        /* ---- THE ONE NEW CELL ------------------------------------------ */
        ck(gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH128,
                                     GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                     GBA_SAVEHDR_EV_DB_FLASH128),
           "***** FLASH128 + live UNKNOWN + ROM BOUND + DB EVIDENCE is "
           "ACCEPTED *****");

        /* ---- AND EVERY NEIGHBOURING CELL THAT IS *NOT* ------------------
         *
         * THREE INDEPENDENT REQUIREMENTS, EACH DROPPED IN TURN. If any one of
         * these passes, the widening is broader than it was authorised to be. */
        ck(!gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH128,
                                      GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                      GBA_SAVEHDR_EV_NONE),
           "***** FLASH128 + live UNKNOWN WITHOUT EVIDENCE is still REFUSED *****");
        ck(!gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH128,
                                      GBA_SAVEHDR_CLS_UNKNOWN, 0,
                                      GBA_SAVEHDR_EV_DB_FLASH128),
           "***** FLASH128 + evidence but NOT ROM-BOUND is still REFUSED *****");
        ck(!gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH64,
                                      GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                      GBA_SAVEHDR_EV_DB_FLASH128),
           "***** FLASH64 + live UNKNOWN is REFUSED EVEN WITH EVIDENCE *****");

        /* THE EVIDENCE ASSERTS A 128 KB PART. A 64 KB header is a DIFFERENT
         * claim from the one gpSP's database made, which is why the cell is
         * granted to CLS_FLASH128 alone and not to the FLASH family. */
        ck_u(gba_savehdr_family(GBA_SAVEHDR_CLS_FLASH64),
             gba_savehdr_family(GBA_SAVEHDR_CLS_FLASH128),
             "FLASH64 and FLASH128 really are the SAME family -- so the "
             "FLASH64 refusal above is a CLASS rule, not a family one");

        /* ---- A STORED UNKNOWN IS REFUSED BEFORE EVIDENCE IS EVEN READ --- */
        ck(!gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_UNKNOWN,
                                      GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                      GBA_SAVEHDR_EV_DB_FLASH128),
           "a STORED UNKNOWN is refused even with evidence and binding");

        /* ---- AN UNKNOWN EVIDENCE BIT GRANTS NOTHING --------------------- */
        ck(!gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH128,
                                      GBA_SAVEHDR_CLS_UNKNOWN, 1, 0xFFFFFFFEu),
           "an evidence mask WITHOUT the DB_FLASH128 bit grants nothing");
        ck(gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_FLASH128,
                                     GBA_SAVEHDR_CLS_UNKNOWN, 1, 0xFFFFFFFFu),
           "a mask that DOES carry the bit still grants it");

        /* ---- THE EEPROM AND SRAM CARVE-OUTS ARE UNAFFECTED -------------- */
        ck(gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_EEPROM512,
                                     GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                     GBA_SAVEHDR_EV_NONE),
           "the EEPROM carve-out still needs no evidence");
        ck(gba_savehdr_compatible_ev(GBA_SAVEHDR_CLS_SRAM,
                                     GBA_SAVEHDR_CLS_UNKNOWN, 1,
                                     GBA_SAVEHDR_EV_NONE),
           "the SRAM carve-out still needs no evidence");
    }

    /* ===================================================================
     * 5. THE VALID CASE -- A BYTE-EXACT RESTORE
     * =================================================================== */
    printf("[5] a valid Tony-Hawk-shaped EEPROM512 save restores byte-exactly\n");
    {
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        ck(write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES), "write hdr");
        ck(write_file(T_SAV, payload, (unsigned int)sizeof payload), "write sav");

        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK, "a valid save is ACCEPTED");
        ck_u(gba_restore_applied(), 8192u, "exactly 8192 bytes were applied");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_SAVBYTES), 8192u,
             "the .sav held exactly 8192 bytes");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_CALCFNV), payfnv,
             "PASS 1 computed the header's payload hash");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_APPLYFNV), payfnv,
             "PASS 2 re-hashed to the same value while copying");
        ck_u(gba_restore_region_fnv(8192u), payfnv,
             "the ARRAY itself hashes to the payload hash");

        /* BYTE FOR BYTE. */
        {
            int same = 1;
            for (i = 0; i < 8192u; i++)
                if (gamepak_backup[i] != payload[i]) { same = 0; break; }
            ck(same, "every restored byte matches the file EXACTLY");
        }

        /* THE TAIL BEYOND THE REGION IS UNTOUCHED. */
        {
            int tail_ok = 1;
            for (i = 8192u; i < GBA_RESTORE_BACKUP_BYTES; i++)
                if (gamepak_backup[i] != 0xFF) { tail_ok = 0; break; }
            ck(tail_ok, "nothing above the region was written");
        }

        /* THE LATCH RECORDED EVERY STAGE. */
        {
            unsigned int L = gba_restorefile_latch();
            ck((L & GBA_RESTOREFILE_L_HDRVALID) != 0u, "latch: header valid");
            ck((L & GBA_RESTOREFILE_L_ROMOK)    != 0u, "latch: ROM matched");
            ck((L & GBA_RESTOREFILE_L_CLASSOK)  != 0u, "latch: class compatible");
            ck((L & GBA_RESTOREFILE_L_SIZEOK)   != 0u, "latch: size exact");
            ck((L & GBA_RESTOREFILE_L_PAYOK)    != 0u, "latch: PASS 1 hash");
            ck((L & GBA_RESTOREFILE_L_APPLIED)  != 0u, "latch: PASS 2 applied");
            ck((L & GBA_RESTOREFILE_L_VERIFIED) != 0u, "latch: array verified");
            ck((L & GBA_RESTOREFILE_L_ROLLBACK) == 0u, "latch: NO rollback");
        }

        /* ---- THE RESTORED BASELINE ---------------------------------
         * It must describe the RESTORED state. If it described the blank
         * one, the run would immediately report that the game had modified
         * a save it had merely loaded. */
        gba_restore_baseline(GBA_RESTORE_BASE_RESTORED);
        ck_u(gba_restore_baseline_kind(), GBA_RESTORE_BASE_RESTORED,
             "the baseline kind is RESTORED");
        ck_u(gba_restore_baseline_hash(),
             ref_fnv(gamepak_backup, GBA_RESTORE_BACKUP_BYTES),
             "the baseline hash describes the RESTORED array");
        gba_restore_finish();
        ck_u(gba_restore_modified(), 0u,
             "immediately after a restore, nothing is reported as modified");

        /* And a real change IS detected. */
        gamepak_backup[10] = (unsigned char)(gamepak_backup[10] ^ 0xFFu);
        gba_restore_finish();
        ck_u(gba_restore_modified(), 1u, "a single changed byte IS detected");

        remove_files();
    }

    /* ===================================================================
     * 6. VALIDATE MODE NEVER MUTATES, EVEN ON A PERFECT FILE
     *
     * This is the property the whole STAGE 0 control run rests on.
     * =================================================================== */
    printf("[6] VALIDATE mode validates fully and mutates NOTHING\n");
    {
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);

        /* ARMED, and it STILL must not write -- because VALIDATE mode never
         * reaches PASS 2. Arming here proves the mode gate is doing the work
         * rather than the arm gate. */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);

        ck_i(rc, GBA_RESTOREFILE_OK, "VALIDATE accepts a valid save");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "VALIDATE still proved the payload hash");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_APPLIED) == 0u,
           "VALIDATE never reached the apply");
        ck_u(gba_restore_applied(), 0u, "VALIDATE applied zero bytes");
        ck(backup_is_blank(),
           "***** VALIDATE LEFT ALL 131072 BYTES UNTOUCHED *****");

        remove_files();
    }

    /* ===================================================================
     * 7. ***** THE ARM GATE AND THE ROLLBACK *****
     *
     * RESTORE mode against an UNARMED adapter: PASS 1 succeeds, then the very
     * first gba_restore_apply() refuses, and the ladder must ROLL THE WHOLE
     * ARRAY BACK TO 0xFF rather than leave it partly written.
     *
     * This is a REACHABLE production path, not a contrivance: it is exactly
     * what would happen if a future edit to the fixture reached the restore
     * without arming. The array must survive that.
     * =================================================================== */
    printf("[7] an unarmed RESTORE fails closed and ROLLS BACK to 0xFF\n");
    {
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);

        /* Deliberately NOT armed. */
        ck_u(gba_restore_armed(), 0u, "the adapter starts disarmed");
        ck_u(gba_restore_apply(payload, 0u, 16u, 8192u), 0u,
             "a disarmed apply writes NOTHING and reports 0");
        ck(backup_is_blank(), "the disarmed apply really wrote nothing");

        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_EAPPLY, "an unarmed RESTORE reports EAPPLY");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "PASS 1 had succeeded before PASS 2 refused");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_ROLLBACK) != 0u,
           "the rollback was latched");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_VERIFIED) == 0u,
           "the array was NOT reported verified");
        ck(backup_is_blank(),
           "***** THE ARRAY IS ALL 0xFF -- NOT PARTLY WRITTEN *****");
        ck_u(gba_restore_applied(), 0u, "the applied counter was reset");
        ck_u(gba_restore_read(GBA_RESTORE_RD_ROLLBACKS), 1u,
             "exactly one rollback was recorded");

        remove_files();
    }

    /* The rollback restores the IDLE state, not zeros -- gpSP's own
     * normalize_blank_backup_for_detected_type() exists to turn 0x00 into 0xFF,
     * so rolling back to zeros would leave a state gpSP would rewrite. */
    printf("[8] the rollback writes 0xFF, never 0x00\n");
    {
        reset_all();
        gba_restore_arm();
        for (i = 0; i < 100u; i++) gamepak_backup[i] = 0x00;
        gba_restore_rollback();
        ck(backup_is_blank(), "rollback fills with 0xFF over the FULL array");
        ck_u(gamepak_backup[GBA_RESTORE_BACKUP_BYTES - 1], 0xFFu,
             "including the very last byte");
    }

    /* ===================================================================
     * 9. THE FIFTEEN REJECTIONS
     *
     * Every one is driven through expect_reject(), which asserts BOTH the
     * distinct code AND that all 131,072 bytes were left untouched.
     * =================================================================== */
    printf("[9] every corruption is rejected, with its own code, passively\n");

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    romfnv = gba_save_rom_hash();

    /* -- no save at all: a FIRST RUN, and not an error in any sense -------- */
    remove_files();
    gba_restore_arm();
    rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
    ck_i(rc, GBA_RESTOREFILE_ENOSAVE, "neither file present -> ENOSAVE");
    ck(backup_is_blank(), "PASSIVITY: ENOSAVE left the backup untouched");

    /* -- orphan .sav: the bytes exist but were NEVER COMMITTED ------------- */
    reset_all(); gba_save_latch();
    write_file(T_SAV, payload, (unsigned int)sizeof payload);
    gba_restore_arm();
    rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
    ck_i(rc, GBA_RESTOREFILE_EORPHANSAV, ".sav without .hdr -> EORPHANSAV");
    ck(backup_is_blank(), "PASSIVITY: an orphan .sav restored nothing");
    remove_files();

    /* -- orphan .hdr ------------------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
    gba_restore_arm();
    rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
    ck_i(rc, GBA_RESTOREFILE_EORPHANHDR, ".hdr without .sav -> EORPHANHDR");
    ck(backup_is_blank(), "PASSIVITY: an orphan .hdr restored nothing");
    remove_files();

    /* -- BAD MAGIC --------------------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 0 /* magic broken */, 0u);
    expect_reject("bad magic -> EMAGIC", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EMAGIC);

    /* -- VERSION 0 --------------------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 0u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("version 0 -> EVERSION", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EVERSION);

    /* -- VERSION 2: a FUTURE format this reader must not guess at ---------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 2u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("version 2 -> EVERSION", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EVERSION);

    /* -- RESERVED BYTE NON-ZERO -------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0x5Au);
    expect_reject("reserved non-zero -> ERESERVED", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_ERESERVED);

    /* -- CLASS UNKNOWN ----------------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_UNKNOWN, 0u, 0u,
           romfnv, payfnv, 0u, 1, 0u);
    expect_reject("class UNKNOWN -> ECLASS", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_ECLASS);

    /* -- CLASS OUT OF RANGE ------------------------------------------------ */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, 6u, 8192u, 512u, romfnv, payfnv, 1u, 1, 0u);
    expect_reject("class 6 -> ECLASS", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_ECLASS);

    /* -- EEPROM512 CLAIMING A 512-BYTE REGION ------------------------------
     * The exact shape a well-meaning "fix" to the size policy would produce.
     * It would restore 512 bytes into a window the game addresses 8192 of. */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 512u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("EEPROM512 with region 512 -> EGEOM", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EGEOM);

    /* -- AN OVERSIZED REGION ----------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH128, 131073u, 131072u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("region 131073 -> EGEOM", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EGEOM);

    /* -- A WRONG DECLARED SIZE --------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 8192u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("EEPROM512 declaring 8192 -> EGEOM", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EGEOM);

    /* -- A 31-BYTE HEADER --------------------------------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("31-byte header -> EHDRSIZE", hdr, GBA_SAVEHDR_BYTES - 1u,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EHDRSIZE);

    /* -- A 33-BYTE HEADER: a different file that starts correctly ---------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    hdr[GBA_SAVEHDR_BYTES] = 0x99;
    expect_reject("33-byte header -> EHDRSIZE", hdr, GBA_SAVEHDR_BYTES + 1u,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EHDRSIZE);

    /* -- THE SAVE BELONGS TO ANOTHER CARTRIDGE ----------------------------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv ^ 0xDEADBEEFu, payfnv, 1u, 1, 0u);
    expect_reject("wrong ROM hash -> EROMID", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EROMID);

    /* -- A SHORT .sav ------------------------------------------------------ */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("8191-byte .sav -> ESHORTREAD", hdr, GBA_SAVEHDR_BYTES,
                  payload, 8191u, GBA_RESTOREFILE_ESHORTREAD);

    /* -- A LONG .sav ------------------------------------------------------- */
    reset_all(); gba_save_latch();
    {
        static unsigned char longsav[8193];
        memcpy(longsav, payload, sizeof payload);
        longsav[8192] = 0x77;
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("8193-byte .sav -> ESAVSIZE", hdr, GBA_SAVEHDR_BYTES,
                      longsav, 8193u, GBA_RESTOREFILE_ESAVSIZE);
    }

    /* -- A STALE HEADER: the hazard gba_savefile.c:181-203 predicted -------- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
           romfnv, payfnv ^ 0x00000001u, 1u, 1, 0u);
    expect_reject("payload hash mismatch -> EPAYFNV", hdr, GBA_SAVEHDR_BYTES,
                  payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EPAYFNV);

    /* -- A FAMILY MISMATCH: an SRAM save offered to an EEPROM cartridge ---- */
    reset_all(); gba_save_latch();
    mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 32768u, 32768u,
           romfnv, payfnv, 1u, 1, 0u);
    expect_reject("SRAM header, live EEPROM -> EINCOMPAT", hdr,
                  GBA_SAVEHDR_BYTES, payload, (unsigned int)sizeof payload,
                  GBA_RESTOREFILE_EINCOMPAT);

    /* ===================================================================
     * 10. ***** THE LIVE UNKNOWN CARTRIDGE -- THE M12C CORRECTION *****
     *
     * ##### THIS SECTION'S HEADLINE EXPECTATION IS DELIBERATELY INVERTED
     * ##### FROM THE PRE-FIX BASELINE, AND IT IS THE ONLY ONE THAT IS.
     *
     * The previous harness asserted "a live UNKNOWN cartridge refuses every
     * save". That assertion ENCODED THE POLICY THIS MILESTONE CORRECTED, so
     * leaving it in place would have frozen the defect. Hardware showed the
     * cost of it: Tony Hawk (ATHE) returned CLASS INCOMPATIBLE for a fully
     * validated, exactly-ROM-matched EEPROM512 save, and would have done so
     * forever, because gpSP's only route to BACKUP_EEPROM on that cartridge is
     * write_eeprom() (gba_memory.c:542) -- which the game reaches strictly
     * AFTER execute_arm(), i.e. strictly after the restore point.
     *
     * ##### THE SAME THING HAS NOW HAPPENED A SECOND TIME, FOR SRAM.
     *
     * Case (9) below asserted "SRAM + live UNKNOWN is STILL REFUSED". That
     * assertion ALSO encoded a defect, and hardware ALSO paid for it: Metroid
     * Fusion (AMTE, absent from gba_over.h, so detection runs the signature
     * sweep and finds nothing) showed SAVE UNKNOWN / UNKNOWN and rejected its
     * own freshly written, exactly-ROM-matched 32,768-byte save with CLASS
     * INCOMPATIBLE (GBA_RESTOREFILE_EINCOMPAT, -12) on every relaunch, losing
     * all progress. Case (9) is now an ACCEPT, and it is the ONLY OTHER
     * INVERTED VERDICT in this file.
     *
     * SRAM's justification is STRONGER than EEPROM's, not weaker. UNKN
     * collapses to BACKUP_SRAM inside read_backup() (:465) and the byte is
     * served through the SRAM path in the SAME CALL (:468) -- so an SRAM
     * header under a live UNKNOWN is the PREDICTED outcome, not a mismatch.
     *
     * EVERYTHING AROUND IT IS UNCHANGED AND IS RE-ASSERTED BELOW rather than
     * assumed: the ROM gate, the payload gate, the geometry gate, both real
     * family mismatches, and -- CRUCIALLY -- the FLASH live-UNKNOWN refusals,
     * which are what stop this from being a general widening. FLASH is
     * condemned by the very collapse that exonerates SRAM: UNKN does not
     * collapse to BACKUP_FLASH.
     * =================================================================== */
    printf("[10] a live UNKNOWN cartridge: EEPROM and SRAM are ACCEPTED when\n");
    printf("     ROM-bound, and EVERYTHING else is still refused\n");
    {
        /* ---- (1) EEPROM512 + live UNKNOWN + exact ROM -> ELIGIBLE ------
         *
         * THE EXACT TONY HAWK HARDWARE CASE. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();

        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "BACKUP_UNKN still resolves to class UNKNOWN -- unchanged");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);

        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** EEPROM512 + live UNKNOWN + exact ROM is now ACCEPTED *****");
        ck_u(gba_restore_applied(), 8192u, "all 8192 bytes were applied");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "***** THE FAMTRUST LATCH RECORDS THAT THE FAMILY WAS TRUSTED *****");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_CLASSOK) != 0u,
           "the class gate was passed");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_FAMTRUST), 1u,
             "the FAMTRUST selector reports 1");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_LIVECLASS),
             GBA_RESTORE_CLS_UNKNOWN,
             "the reported LIVE CLASS is STILL UNKNOWN -- no detection is claimed");

        /* ***** NOT ONE CLASSIFICATION GLOBAL WAS SEEDED. *****
         * This is the single most important assertion in the milestone. The
         * game must classify EEPROM from its OWN bus traffic; seeding it here
         * would pre-empt the very observation STAGE 1 exists to make. */
        ck_u(backup_type,    (u32)BACKUP_UNKN,   "backup_type was NOT seeded");
        ck_u(eeprom_size,    (u32)EEPROM_512_BYTE, "eeprom_size was NOT seeded");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB, "flash_bank_cnt NOT seeded");

        /* ---- (11) THE APPLY IS BYTE-EXACT UNDER A LIVE UNKNOWN --------- */
        {
            int same = 1;
            for (i = 0; i < 8192u; i++)
                if (gamepak_backup[i] != payload[i]) { same = 0; break; }
            ck(same, "every restored byte matches the file EXACTLY");
        }
        ck_u(gba_restore_region_fnv(8192u), payfnv,
             "the ARRAY hashes to the payload hash");
        {
            int tail_ok = 1;
            for (i = 8192u; i < GBA_RESTORE_BACKUP_BYTES; i++)
                if (gamepak_backup[i] != 0xFF) { tail_ok = 0; break; }
            ck(tail_ok, "nothing above the region was written");
        }

        /* ---- (12) A POST-RESTORE UNKNOWN -> EEPROM TRANSITION ----------
         *
         * SIMULATES WHAT THE GAME ITSELF DOES ONCE IT ISSUES EEPROM TRAFFIC.
         * gpSP would set backup_type inside write_eeprom(); nothing in that
         * path rewrites the array, so the restored bytes must survive the
         * reclassification COMPLETELY UNCHANGED. If they did not, a restore
         * would be silently undone the instant the game touched its save. */
        {
            unsigned int before = gba_restore_region_fnv(8192u);
            int same = 1;

            backup_type = BACKUP_EEPROM;          /* what :542 would do      */
            ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_EEPROM512,
                 "the live class now reports EEPROM512, as gpSP would");
            ck_u(gba_restore_region_fnv(8192u), before,
                 "***** THE RESTORED BYTES ARE BIT-IDENTICAL AFTER THE "
                 "TRANSITION *****");
            for (i = 0; i < 8192u; i++)
                if (gamepak_backup[i] != payload[i]) { same = 0; break; }
            ck(same, "and still byte-for-byte the file, class change or not");

            /* The 512 -> 8 KB size discovery is INTRA-FAMILY and prefix-safe:
             * same base address, same 8192-byte persisted window. */
            eeprom_size = EEPROM_8_KBYTE;
            ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_EEPROM8K,
                 "and again after the 8 KB size discovery");
            ck_u(gba_restore_region_fnv(8192u), before,
                 "the bytes survive the intra-family size change too");
        }
        remove_files();

        /* ---- (2) EEPROM8K header + live UNKNOWN + exact ROM -> ELIGIBLE - */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM8K, 8192u, 8192u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "EEPROM8K + live UNKNOWN + exact ROM is ACCEPTED");
        ck_u(gba_restore_applied(), 8192u, "8192 bytes applied");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "FAMTRUST latched for EEPROM8K too");
        remove_files();

        /* ---- (10) VALIDATE-ONLY UNDER A LIVE UNKNOWN: ELIGIBLE, NO WRITE
         *
         * ***** THE STAGE 0 CONTRACT, UNDER THE NEW RULE. ***** The save is
         * now eligible where it was previously refused -- and STAGE 0 must
         * STILL not write a single byte. Armed on purpose, so the passivity
         * comes from the MODE and not from the arm gate. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "VALIDATE accepts EEPROM + live UNKNOWN + exact ROM");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "VALIDATE latches FAMTRUST too -- STAGE 0 reports the same trust");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "VALIDATE still proved the payload hash");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_APPLIED) == 0u,
           "VALIDATE never reached the apply");
        ck_u(gba_restore_applied(), 0u, "VALIDATE applied zero bytes");
        ck(backup_is_blank(),
           "***** ELIGIBLE BUT ZERO MUTATION -- ALL 131072 BYTES UNTOUCHED *****");
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "VALIDATE seeded no classification global either");
        remove_files();

        /* ---- (3) EEPROM + live UNKNOWN + ROM MISMATCH -> EROMID ---------
         *
         * ***** ROM VALIDATION IS NOT WEAKENED BY THE CARVE-OUT. ***** The
         * whole trust argument rests on this gate, so it is asserted under
         * the live UNKNOWN specifically, not merely under a live EEPROM. */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv ^ 0xDEADBEEFu, payfnv, 1u, 1, 0u);
        expect_reject("live UNKNOWN + wrong ROM -> EROMID",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EROMID);

        /* ---- (4) EEPROM + live UNKNOWN + BAD PAYLOAD FNV -> EPAYFNV ----- */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv ^ 0x00000001u, 1u, 1, 0u);
        expect_reject("live UNKNOWN + stale payload -> EPAYFNV",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EPAYFNV);

        /* ---- (5) EEPROM + live UNKNOWN + BAD GEOMETRY -> EGEOM ---------- */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 512u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("live UNKNOWN + region 512 -> EGEOM",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EGEOM);

        /* ---- (8) FLASH header + live UNKNOWN + exact ROM -> EINCOMPAT ---
         *
         * ***** THE SCOPE PIN. ***** A flash game's first backup read enters
         * read_backup(), which collapses UNKN to BACKUP_SRAM at :465 and
         * serves the byte through the SRAM path at :468 -- so the first
         * observation would be made under a class nobody measured. Probably
         * safe is not proven, and this case makes any future widening a
         * CONSCIOUS ACT rather than an accident. */
        /* THE BANK COUNT IS PINNED EXPLICITLY RATHER THAN INHERITED. These two
         * cases are now ABOUT the bank count, so leaving it to whatever the
         * previous scenario happened to set would make them prove nothing. A
         * 64 KB bank count means NO database assertion exists. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        ck_u(gba_restore_db_flash128(), 0u,
             "a 64 KB bank count is NOT a database FLASH128 assertion");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv, payfnv, 1u, 1, 0u);
#ifndef LUAPORT_SESSION_REUSE
        /* ***** THE FROZEN-MILESTONE VERDICT, UNCHANGED. ***** Built without
         * the session-reuse define -- which is how M13C, M12C, M12B and M11 are
         * built -- FLASH64 under a live UNKNOWN is refused at the CLASS gate,
         * exactly as it always has been. `make m12c-equiv` runs this branch and
         * its meaning is deliberately identical to the historical one. */
        expect_reject("***** FLASH64 + live UNKNOWN is STILL REFUSED *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EINCOMPAT);
#else
        /* ***** UNDER SESSION REUSE THE CLASS GATE NO LONGER DECIDES THIS CASE,
         * ***** SO THE NEXT GATE DOWN MUST -- AND IT DOES.
         *
         * THE HEADER IS UNCHANGED AND SO IS THE .sav. Only the configuration
         * differs. The eligibility observation now holds (UNKN + 64 KB
         * geometry), so the header's class is admitted -- and this file is then
         * refused ESHORTREAD because an 8,192-byte .sav cannot satisfy a
         * 65,536-byte region.
         *
         * ***** THIS IS NOT A WEAKER ASSERTION THAN THE ONE ABOVE, IT IS A
         * DIFFERENT ONE. ***** It proves the size ladder is still load-bearing
         * after the class gate opens. A FLASH64 save that is genuinely valid is
         * accepted in [10f]; a FLASH64 header with the wrong bytes behind it is
         * still refused here, passively, with the array untouched. */
        expect_reject("FLASH64 + live UNKNOWN: the class gate admits it under "
                      "session reuse, and the SIZE gate still refuses an "
                      "8192-byte .sav against a 65536-byte region",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_ESHORTREAD);
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** A REFUSED FLASH64 SAVE ACTIVATED NOTHING *****");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "and it did not touch the reset seed either");
#endif

        /* ***** FLASH128 WITHOUT THE DATABASE EVIDENCE IS STILL REFUSED. *****
         * This is the NEGATIVE CONTROL for the M15 correction and it must never
         * be deleted: it is the only thing standing between a narrow,
         * evidence-backed carve-out and a blanket FLASH widening. The header is
         * byte-identical to the one accepted in [10c] below -- ONLY the
         * cartridge's bank count differs. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH128, 131072u, 131072u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("***** FLASH128 + live UNKNOWN WITHOUT DB EVIDENCE is "
                      "STILL REFUSED *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EINCOMPAT);

        /* ---- (9) SRAM header + live UNKNOWN + exact ROM -> ACCEPTED -----
         *
         * ##### THE SECOND DELIBERATELY INVERTED VERDICT. #####
         *
         * THE EXACT METROID FUSION HARDWARE CASE, end to end: 32,768 bytes,
         * class SRAM, region and declared both 32768, exact ROM hash, live
         * class UNKNOWN. This is byte-for-byte the file the console refused.
         *
         * The old expectation here reasoned that an SRAM class MAY ITSELF BE
         * the silent UNKN->SRAM collapse rather than a detection
         * (gba_save.c:217-219) -- which is TRUE, and is exactly why SRAM's
         * confidence can never exceed PROVISIONAL. It does not justify the
         * refusal, because the restore is SYMMETRIC: the bytes were written
         * through the SRAM path and are read back through the SRAM path, on a
         * cartridge whose ROM hash has already been proved identical. And the
         * collapse is not a hazard here, it is the MECHANISM -- read_backup()
         * sets BACKUP_SRAM at :465 and returns gamepak_backup[] at :468 in the
         * same call, so the game's first backup read is already served the
         * restored bytes.
         *
         * VALIDATE IS ASSERTED FIRST, so the Stage 0 contract is proved for
         * SRAM exactly as it is for EEPROM: eligible, and ZERO mutation. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "the Metroid case really is a live UNKNOWN");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 32768u, 32768u,
               romfnv, sramfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, sram_payload, (unsigned int)sizeof sram_payload);

        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "VALIDATE accepts SRAM + live UNKNOWN + exact ROM");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "VALIDATE latches FAMTRUST for the SRAM carve-out too");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "VALIDATE proved the 32768-byte payload hash");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_APPLIED) == 0u,
           "VALIDATE never reached the apply");
        ck_u(gba_restore_applied(), 0u, "VALIDATE applied zero bytes");
        ck(backup_is_blank(),
           "***** SRAM ELIGIBLE BUT ZERO MUTATION -- 131072 BYTES UNTOUCHED *****");

        /* ---- AND NOW THE REAL RESTORE ---------------------------------- */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** SRAM + live UNKNOWN + exact ROM is now ACCEPTED *****");
        ck_u(gba_restore_applied(), 32768u,
             "***** EXACTLY 32768 BYTES WERE APPLIED *****");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "the FAMTRUST latch records that the SRAM family was trusted");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_CLASSOK) != 0u,
           "the class gate was passed");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_ROMOK) != 0u,
           "the ROM gate was passed -- the binding is what authorised this");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_FAMTRUST), 1u,
             "the FAMTRUST selector reports 1");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_LIVECLASS),
             GBA_RESTORE_CLS_UNKNOWN,
             "the reported LIVE CLASS is STILL UNKNOWN -- no detection claimed");

        /* ***** NOT ONE CLASSIFICATION GLOBAL WAS SEEDED. *****
         * The whole point is that gpSP still classifies naturally, from the
         * game's own bus traffic, AFTER the restore. If the restore seeded
         * BACKUP_SRAM it would be manufacturing the very observation the
         * hardware run exists to make. */
        ck_u(backup_type,    (u32)BACKUP_UNKN,     "backup_type was NOT seeded");
        ck_u(eeprom_size,    (u32)EEPROM_512_BYTE, "eeprom_size was NOT seeded");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB, "flash_bank_cnt NOT seeded");

        /* ---- BYTE-EXACTNESS, AND THE TAIL ABOVE THE REGION -------------- */
        {
            int same = 1;
            for (i = 0; i < 32768u; i++)
                if (gamepak_backup[i] != sram_payload[i]) { same = 0; break; }
            ck(same, "every one of the 32768 restored bytes matches EXACTLY");
        }
        ck_u(gba_restore_region_fnv(32768u), sramfnv,
             "the ARRAY hashes to the 32768-byte payload hash");
        {
            int tail_ok = 1;
            for (i = 32768u; i < GBA_RESTORE_BACKUP_BYTES; i++)
                if (gamepak_backup[i] != 0xFF) { tail_ok = 0; break; }
            ck(tail_ok,
               "bytes 32768..131071 are still 0xFF -- nothing above the region");
        }
        remove_files();

        /* ---- (9b) THE SAME FILE WITHOUT THE ROM BINDING IS STILL REFUSED
         *
         * The binding is not decoration. A wrong ROM hash must still reject
         * the identical SRAM file, under the live UNKNOWN specifically. */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 32768u, 32768u,
               romfnv ^ 0xDEADBEEFu, sramfnv, 1u, 1, 0u);
        expect_reject("SRAM + live UNKNOWN + WRONG ROM -> EROMID",
                      hdr, GBA_SAVEHDR_BYTES,
                      sram_payload, (unsigned int)sizeof sram_payload,
                      GBA_RESTOREFILE_EROMID);

        /* ---- (9c) A STALE SRAM PAYLOAD IS STILL REFUSED ----------------- */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 32768u, 32768u,
               romfnv, sramfnv ^ 0x00000001u, 1u, 1, 0u);
        expect_reject("SRAM + live UNKNOWN + stale payload -> EPAYFNV",
                      hdr, GBA_SAVEHDR_BYTES,
                      sram_payload, (unsigned int)sizeof sram_payload,
                      GBA_RESTOREFILE_EPAYFNV);

        /* ---- (9d) AND A BAD SRAM GEOMETRY IS STILL REFUSED --------------
         *
         * 32768 is the ONLY region an SRAM header may declare. The carve-out
         * authorises a FAMILY, never a size. */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 8192u, 32768u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("SRAM + live UNKNOWN + region 8192 -> EGEOM",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EGEOM);

        /* ---- A STORED UNKNOWN IS STILL REFUSED UNDER A LIVE UNKNOWN ----- */
        reset_all(); gba_save_latch();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_UNKNOWN, 0u, 0u,
               romfnv, payfnv, 0u, 1, 0u);
        expect_reject("stored UNKNOWN + live UNKNOWN -> ECLASS",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_ECLASS);
    }

    /* ===================================================================
     * 10c. ***** THE DATABASE-FLASH128 CARTRIDGE -- THE M15 CORRECTION *****
     *
     * ##### THE THIRD AND LAST DELIBERATELY INVERTED VERDICT IN THIS FILE.
     *
     * Tony Hawk forced the EEPROM carve-out. Metroid Fusion forced the SRAM
     * carve-out. SUPER MARIO ADVANCE 4 (AX4E) HAS NOW FORCED THE THIRD, and the
     * failure was identical in every respect: a 131,072-byte save, written by
     * this very build, exactly ROM-matched, refused CLASS INCOMPATIBLE
     * (GBA_RESTOREFILE_EINCOMPAT, -12) on the very next launch.
     *
     * WHAT MAKES THIS CASE DIFFERENT FROM THE OTHER TWO -- AND WHY IT IS NOT A
     * BLANKET FLASH WIDENING. AX4E is PRESENT in gpSP's gba_over.h database with
     * FLAGS_FLASH_128KB. That handler (gba_memory.c:1727) sets flash_bank_cnt
     * and the Sanyo device id and then FALLS THROUGH WITHOUT SETTING A TYPE --
     * only FLAGS_EEPROM sets one (:1740). So gpSP had ALREADY ASSERTED a 128 KB
     * flash part and merely failed to record it. That assertion is the evidence,
     * and the cartridge state below is EXACTLY the one the console was in:
     *
     *     backup_type    == BACKUP_UNKN        (no type was ever recorded)
     *     flash_bank_cnt == FLASH_SIZE_128KB   (the database DID say 128 KB)
     *
     * THE NEGATIVE CONTROL IS IN [10] ABOVE and uses a BYTE-IDENTICAL header --
     * only the bank count differs -- and is still refused. The two cases
     * together are what make this a narrow, evidence-backed cell rather than a
     * general relaxation.
     * =================================================================== */
    printf("[10c] the DATABASE-FLASH128 cartridge: ACCEPTED with evidence,\n");
    printf("      and the byte-identical header WITHOUT it is refused in [10]\n");
    {
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();

        /* ---- THE CARTRIDGE REALLY IS IN THE HARDWARE'S STATE ------------ */
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "the Mario case really is a live UNKNOWN");
        ck_u(gba_save_class(), GBA_SAVE_UNKNOWN,
             "***** AND THE LAUNCHER WOULD PRINT 'SAVE UNKNOWN' -- the picker "
             "reads this same live value *****");
        ck_u(gba_restore_db_flash128(), 1u,
             "***** THE DATABASE FLASH128 EVIDENCE IS PRESENT *****");
        ck_u(gba_restore_read(GBA_RESTORE_RD_DBFLASH128), 1u,
             "the selector reports the evidence too");
        ck_u(gba_restore_read(GBA_RESTORE_RD_BANKCNT),
             (unsigned int)FLASH_SIZE_128KB,
             "the raw bank count is the UNIT COUNT 2, not a byte size");
        ck_u(gba_restore_read(GBA_RESTORE_RD_BACKUP_TYPE),
             (unsigned int)BACKUP_UNKN, "and the raw type is still UNKN");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH128, 131072u, 131072u,
               romfnv, flashfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, flash_payload, (unsigned int)sizeof flash_payload);

        /* ---- VALIDATE FIRST: ELIGIBLE, AND ZERO MUTATION ---------------
         * The STAGE 0 contract, proved for the FLASH carve-out exactly as it
         * was for EEPROM and SRAM. Armed on purpose. */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "VALIDATE accepts FLASH128 + live UNKNOWN + evidence");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_DBFLASH) != 0u,
           "VALIDATE latches DBFLASH too");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "VALIDATE proved the 131072-byte payload hash");
        ck_u(gba_restore_applied(), 0u, "VALIDATE applied zero bytes");
        ck(backup_is_blank(),
           "***** FLASH ELIGIBLE BUT ZERO MUTATION -- 131072 BYTES UNTOUCHED *****");

        /* ---- AND NOW THE REAL RESTORE ---------------------------------- */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** FLASH128 + live UNKNOWN + exact ROM + DB EVIDENCE is now "
             "ACCEPTED *****");
        ck_u(gba_restore_applied(), 131072u,
             "***** EXACTLY 131072 BYTES WERE APPLIED -- the hardware's "
             "'SAVE UPDATED 131072 bytes' figure *****");

        /* ---- THE LATCH SAYS *WHICH* RULE ADMITTED IT -------------------- */
        {
            unsigned int L = gba_restorefile_latch();
            ck((L & GBA_RESTOREFILE_L_ROMOK)    != 0u,
               "the ROM gate was passed -- the binding is load bearing here");
            ck((L & GBA_RESTOREFILE_L_CLASSOK)  != 0u, "the class gate passed");
            ck((L & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
               "FAMTRUST is set -- the live class really was UNKNOWN");
            ck((L & GBA_RESTOREFILE_L_DBFLASH)  != 0u,
               "***** DBFLASH IS SET -- the DATABASE evidence is what admitted "
               "this save, and the log says so *****");
            ck((L & GBA_RESTOREFILE_L_VERIFIED) != 0u, "the array verified");
            ck((L & GBA_RESTOREFILE_L_ROLLBACK) == 0u, "no rollback");
        }
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_DBTRUST), 1u,
             "the DBTRUST selector reports 1");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_DBFLASH128), 1u,
             "the DBFLASH128 selector reports the observed evidence");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_LIVECLASS),
             GBA_RESTORE_CLS_UNKNOWN,
             "***** THE REPORTED LIVE CLASS IS STILL UNKNOWN -- NO DETECTION "
             "IS CLAIMED *****");

        /* ***** NOT ONE CLASSIFICATION GLOBAL WAS SEEDED. *****
         * The whole argument depends on this. If the restore had written
         * BACKUP_FLASH it would be MANUFACTURING the class it then reported,
         * and the acceptance would be circular. */
        ck_u(backup_type,    (u32)BACKUP_UNKN,
             "***** backup_type was NOT seeded -- still UNKN after the restore *****");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_128KB,
             "flash_bank_cnt was NOT written (it was 2 before and after)");
        ck_u(eeprom_size,    (u32)EEPROM_512_BYTE, "eeprom_size was NOT seeded");

        /* ---- BYTE-EXACTNESS ACROSS *BOTH* FLASH BANKS ------------------- */
        {
            int same = 1;
            for (i = 0; i < 131072u; i++)
                if (gamepak_backup[i] != flash_payload[i]) { same = 0; break; }
            ck(same, "every one of the 131072 restored bytes matches EXACTLY");
        }
        ck_u(gba_restore_region_fnv(131072u), flashfnv,
             "the ARRAY hashes to the 131072-byte payload hash");

        /* THE UPPER BANK SPECIFICALLY. Bytes 65536.. are reachable only after
         * the game issues the 0xB0 bank switch, so a bank-confusion defect
         * would leave them wrong while the first 64 KB looked perfect. */
        ck_u((unsigned int)gamepak_backup[65536],
             (unsigned int)flash_payload[65536],
             "***** THE UPPER FLASH BANK'S FIRST BYTE SURVIVED *****");
        ck_u((unsigned int)gamepak_backup[131071],
             (unsigned int)flash_payload[131071],
             "and so did the very last byte of bank 1");
        ck(flash_payload[65536] != flash_payload[0],
           "the two banks really do hold different patterns -- so the two "
           "assertions above cannot both pass by coincidence");

        remove_files();

        /* ---- THE BINDING IS STILL MANDATORY UNDER THE NEW RULE ----------
         * Evidence does NOT replace ROM validation. Same cartridge state, same
         * payload, wrong hash -> still EROMID. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH128, 131072u, 131072u,
               romfnv ^ 0xDEADBEEFu, flashfnv, 1u, 1, 0u);
        expect_reject("***** FLASH128 + evidence + WRONG ROM -> EROMID *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash_payload, (unsigned int)sizeof flash_payload,
                      GBA_RESTOREFILE_EROMID);

        /* ---- A STALE FLASH PAYLOAD IS STILL REFUSED --------------------- */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH128, 131072u, 131072u,
               romfnv, flashfnv ^ 0x00000001u, 1u, 1, 0u);
        expect_reject("FLASH128 + evidence + stale payload -> EPAYFNV",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash_payload, (unsigned int)sizeof flash_payload,
                      GBA_RESTOREFILE_EPAYFNV);

        /* ---- AND A FLASH64 HEADER ON THE SAME CARTRIDGE IS REFUSED ------
         *
         * ***** THE SCOPE PIN FOR THE NEW CELL. ***** The evidence asserts a
         * 128 KB part. A 64 KB header is a DIFFERENT claim, so it gets nothing
         * from that evidence -- the carve-out is granted to CLS_FLASH128 ALONE
         * and never to the FLASH family. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        ck_u(gba_restore_db_flash128(), 1u, "the evidence IS present here");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("***** FLASH64 IS REFUSED EVEN ON A DB-FLASH128 "
                      "CARTRIDGE *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EINCOMPAT);

        /* ---- AN SRAM HEADER IS NOT ADMITTED BY FLASH EVIDENCE -----------
         * It is admitted by its OWN carve-out, which needs no evidence -- but
         * the geometry must still be right, so this proves the evidence did not
         * loosen anything about SRAM. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_SRAM, 8192u, 32768u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("SRAM + bad geometry is still EGEOM on a DB-FLASH128 cart",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EGEOM);
    }

    /* ===================================================================
     * 10d. ***** THE FULL COMMIT -> RELAUNCH -> RESTORE ROUND TRIP *****
     *
     * THE HARDWARE SEQUENCE, MODELLED END TO END, WITH THE FROZEN M12B WRITER
     * PRODUCING THE HEADER RATHER THAN mk_hdr(). This is the test the defect
     * report asked for, and it is the one that would have caught the bug before
     * a console was ever involved:
     *
     *   GAMEPLAY   live BACKUP_FLASH, bank count 2  -> the writer records
     *              class FLASH128 and a 131,072-byte region.
     *   RELAUNCH   gba_over.h is re-applied: bank count 2 again, but the type
     *              is UNKN again, because nothing ever recorded it.
     *   RESTORE    the same cartridge, the same file -> MUST BE ACCEPTED.
     *
     * ***** THE TWO PHASES READ THE SAME TWO gpSP GLOBALS AT DIFFERENT TIMES.
     * ***** That is the entire defect: commit saw the class AFTER the game had
     * established it, restore sees it BEFORE. Nothing here simulates a
     * workaround -- both halves run the real shipping code.
     * =================================================================== */
    printf("[10d] COMMIT -> RELAUNCH -> RESTORE round trip, frozen writer to\n");
    printf("      shipping reader\n");
    {
        unsigned char built[GBA_SAVE_HDR_BYTES];
        struct gba_savehdr parsed;
        unsigned int commit_cls, commit_region, commit_romfnv, commit_conf;

        /* ---- PHASE 1: GAMEPLAY AND COMMIT ------------------------------
         * write_backup() has promoted the cartridge to BACKUP_FLASH by now
         * (gba_memory.c:1148) and gba_over.h had already set the bank count. */
        setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        commit_romfnv = gba_save_rom_hash();

        ck_u(gba_save_class(), GBA_SAVE_FLASH128,
             "COMMIT: the live class is FLASH128");
        ck_u(gba_save_region_bytes(), 131072u,
             "***** COMMIT: the region is 131072 -- the figure the console "
             "reported as 'SAVE UPDATED 131072 bytes' *****");
        ck_u(gba_restore_db_flash128(), 0u,
             "COMMIT: with a REAL type recorded there is no database GAP, so "
             "the evidence flag is correctly CLEAR");

        ck_u(gba_save_header_build(built, (unsigned int)sizeof built, flashfnv),
             GBA_SAVE_HDR_BYTES, "COMMIT: the FROZEN writer produced 32 bytes");

        ck_i(gba_savehdr_parse(built, GBA_SAVE_HDR_BYTES, &parsed),
             GBA_SAVEHDR_OK,
             "COMMIT: the shipping reader accepts the frozen writer's header");
        ck_u(parsed.cls, GBA_SAVEHDR_CLS_FLASH128, "COMMIT: class FLASH128");
        ck_u(parsed.region, 131072u,               "COMMIT: region 131072");
        ck_u(parsed.declared, 131072u,             "COMMIT: declared 131072");
        ck_u(parsed.rom_fnv, commit_romfnv,        "COMMIT: the ROM identity");

        commit_cls    = parsed.cls;
        commit_region = parsed.region;
        commit_conf   = parsed.confidence;

        write_file(T_HDR, built, GBA_SAVE_HDR_BYTES);
        write_file(T_SAV, flash_payload, (unsigned int)sizeof flash_payload);

        /* ---- PHASE 2: RELAUNCH -----------------------------------------
         * A NEW PROCESS. load_gamepak() re-applies gba_over.h, which sets the
         * bank count and NOT the type; init_memory() then copies
         * backup_type_reset (still UNKN) into backup_type. The files on disk
         * survive; every scrap of in-memory classification does not. */
        setup_cart(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE);

        ck_u(gba_save_rom_hash(), commit_romfnv,
             "RELAUNCH: the cartridge identity is UNCHANGED -- same ROM");
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "***** RELAUNCH: the live class is UNKNOWN again -- the flash "
             "evidence lived only in RAM *****");
        ck_u(gba_save_class(), GBA_SAVE_UNKNOWN,
             "RELAUNCH: the launcher prints SAVE UNKNOWN, as it did on hardware");
        ck_u(gba_restore_db_flash128(), 1u,
             "RELAUNCH: and NOW the database gap is observable");

        /* ---- PHASE 3: RESTORE ------------------------------------------ */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** THE SAVE THIS BUILD COMMITTED IS ACCEPTED ON RELAUNCH *****");
        ck_u(gba_restore_applied(), commit_region,
             "***** EXACTLY THE COMMITTED REGION WAS RESTORED *****");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_CLASS), commit_cls,
             "the CLASS round-tripped: committed FLASH128, restored FLASH128");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_REGION), commit_region,
             "the REGION round-tripped: committed 131072, restored 131072");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_CONF), commit_conf,
             "the confidence the writer recorded survives, and still gates "
             "nothing");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_DBFLASH) != 0u,
           "the DATABASE evidence is recorded as the reason");

        /* THE BYTES THEMSELVES, WHICH IS THE ONLY THING THAT ACTUALLY
         * MATTERS TO THE PLAYER. */
        {
            int same = 1;
            for (i = 0; i < 131072u; i++)
                if (gamepak_backup[i] != flash_payload[i]) { same = 0; break; }
            ck(same,
               "***** EVERY BYTE OF THE COMMITTED SAVE CAME BACK EXACTLY *****");
        }
        ck_u(gba_restore_region_fnv(commit_region), flashfnv,
             "and the array hashes to the committed payload hash");

        /* ---- PHASE 4: THE GAME THEN CLASSIFIES ITSELF, NATURALLY --------
         *
         * write_backup() sets BACKUP_FLASH on the game's first flash command
         * (gba_memory.c:1148). Nothing on that path rewrites the array, so the
         * restored bytes must survive the reclassification untouched -- and the
         * cartridge must then report a REAL class rather than the carve-out. */
        {
            unsigned int before = gba_restore_region_fnv(131072u);

            backup_type = BACKUP_FLASH;        /* what :1148 would do */

            ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_FLASH128,
                 "the live class now reports FLASH128, as gpSP would");
            ck_u(gba_restore_db_flash128(), 0u,
                 "***** AND THE EVIDENCE FLAG GOES CLEAR -- once a REAL type "
                 "exists there is no database gap to report *****");
            ck_u(gba_restore_region_fnv(131072u), before,
                 "***** THE RESTORED BYTES ARE BIT-IDENTICAL AFTER THE "
                 "TRANSITION *****");
            ck(gba_savehdr_compatible(GBA_SAVEHDR_CLS_FLASH128,
                                      GBA_RESTORE_CLS_FLASH128),
               "and from here the ORDINARY family rule accepts it -- no "
               "carve-out, no evidence, no binding needed");
        }

        remove_files();
    }

    /* ===================================================================
     * 10e. (6) AND (7): AN EEPROM HEADER AGAINST A LIVE SRAM / FLASH
     *
     * THE CARVE-OUT MUST NOT HAVE LOOSENED A REAL FAMILY MISMATCH. These
     * cartridges have a live class, so the rule that applies is plain family
     * equality and the ROM binding is irrelevant to it.
     *
     * (RENUMBERED FROM 10b TO 10e SO THE PRINTED SECTION LABELS STAY IN
     * ASCENDING ORDER after 10c and 10d were added above. NOT ONE ASSERTION IN
     * THIS SECTION IS CHANGED.)
     * =================================================================== */
    printf("[10e] an EEPROM header is still refused by a live SRAM or FLASH\n");
    {
        setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_SRAM, "live class SRAM");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("EEPROM header + live SRAM -> EINCOMPAT",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EINCOMPAT);

        setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_FLASH128,
             "live class FLASH128");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        expect_reject("EEPROM header + live FLASH -> EINCOMPAT",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EINCOMPAT);

        /* AND THE FAMTRUST LATCH IS NOT SET ON A NORMAL FAMILY MATCH -- the
         * two verdicts must never print the same way. */
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        romfnv = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
        ck_i(rc, GBA_RESTOREFILE_OK, "a live EEPROM cartridge still accepts");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) == 0u,
           "***** FAMTRUST IS CLEAR ON A LIVE FAMILY MATCH *****");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_FAMTRUST), 0u,
             "the FAMTRUST selector reports 0 on a live family match");
        remove_files();
    }

    /* ===================================================================
     * 10f. ***** THE FLASH64 CARTRIDGE -- MOTHER 3 (A3UJ), THE SIMS 2 (B46E)
     * ===================================================================
     *
     * THE FOURTH INSTANCE OF ONE DEFECT, and the first whose correction has to
     * do more than authorise bytes.
     *
     * Tony Hawk forced the EEPROM carve-out, Metroid Fusion forced the SRAM
     * one, Super Mario Advance 4 forced the evidence-backed FLASH128 one. MOTHER
     * 3 (A3UJ) AND THE SIMS 2 (B46E) HAVE NOW FORCED A FOURTH, and the hardware
     * symptom was identical in every respect: a 65,536-byte save, written by
     * this very build, exactly ROM-matched, refused CLASS INCOMPATIBLE
     * (GBA_RESTOREFILE_EINCOMPAT, -12) on the very next launch.
     *
     * WHY THE LIVE CLASS IS UNKNOWN FOR THESE TWO, AND WHY NO DATABASE EVIDENCE
     * CAN EVER EXIST FOR THEM. FLAGS_FLASH_64KB DOES NOT EXIST IN gpsp/
     * gba_over.h AT ALL -- only FLAGS_FLASH_128KB and FLAGS_EEPROM set anything
     * -- so the FLASH128 route used in [10c] has no 64 KB counterpart for ANY
     * game. Neither cartridge has a database entry in any case, and the
     * signature sweep is bounded well below their 16 MB and 32 MB sizes. So
     * gpSP reaches the restore point holding no opinion, every time.
     *
     * ---------------------------------------------------------------------
     * ***** WHAT AUTHORISES THE CLASS, AND WHAT MERELY AUTHORISES THE TIMING
     * ---------------------------------------------------------------------
     *
     * THE AUTHORITY FOR THE RESTORED CLASS IS THE HEADER, AND NOTHING ELSE.
     * Specifically: a fully validated, ROM-bound LGS1 FLASH64 header produced by
     * this project's own frozen writer -- LGS1 magic, version 1, zero reserved
     * bytes, a legal FLASH64 geometry (region 65536, declared 65536), THIS
     * cartridge's ROM FNV, and a payload FNV over the exact bytes on disk. Every
     * one of those gates is proved below, and every one of them is still fatal.
     *
     * ##### THE LIVE-STATE OBSERVATION IS *NOT* INDEPENDENT PROOF THAT THE
     * ##### CARTRIDGE IS A 64 KB FLASH PART. IT MUST NEVER BE DESCRIBED AS SUCH.
     *
     *     backup_type    == BACKUP_UNKN
     *     flash_bank_cnt == FLASH_SIZE_64KB
     *
     * is ALSO gpSP's DEFAULT state for an unclassified cartridge -- this very
     * harness initialises its globals to exactly those two values, and so does
     * init_memory() on hardware. An EEPROM cartridge, an SRAM cartridge and a
     * genuine 64 KB flash cartridge are ALL in this state before the first
     * instruction executes. Read alone it says nothing whatsoever about the
     * part.
     *
     * WHAT IT DOES PROVE IS ELIGIBILITY: that gpSP has NOT already classified
     * this cartridge as something else, so narrowly scoped FLASH64 activation
     * cannot contradict an observation the emulator actually made. It is a
     * precondition on the emulator's state, not evidence about the hardware.
     * The assertion at the end of the shared block below encodes precisely that
     * distinction -- an EEPROM512 save restored on a cartridge in the IDENTICAL
     * live state must NOT activate anything, because the HEADER, not the state,
     * is what decides the class.
     * =================================================================== */
    printf("[10f] the FLASH64 cartridge: MOTHER 3 / THE SIMS 2\n");
#ifdef LUAPORT_SESSION_REUSE
    printf("      CONFIGURATION: LUAPORT_SESSION_REUSE IS DEFINED -- the\n");
    printf("      FLASH64 cases expect ACCEPTANCE AND ACTIVATION\n");
#else
    printf("      CONFIGURATION: LUAPORT_SESSION_REUSE is NOT defined -- the\n");
    printf("      FLASH64 cases expect the historical REFUSAL, unchanged\n");
#endif

/* EVERY GATE BELOW THE CLASS GATE IS REACHED ONLY IN ONE CONFIGURATION.
 *
 * The ladder runs parse -> ROM -> CLASS -> size -> payload hash. In the frozen
 * configuration a FLASH64 header is refused AT the class gate, so the size and
 * payload gates below it are never reached and every malformed FLASH64 input
 * reports EINCOMPAT. Under session reuse the class gate admits it and the lower
 * gates do the refusing, each with its own distinct code.
 *
 * BOTH ARE REFUSALS AND BOTH ARE ASSERTED. This macro states the expected code
 * per configuration so that ONE set of negative cases covers BOTH builds
 * honestly, rather than a weaker set covering only one. Gates ABOVE the class
 * gate -- geometry and the ROM binding -- are configuration-independent and are
 * written plainly, without it. */
#ifdef LUAPORT_SESSION_REUSE
#define F64_BELOW_CLASS(code) (code)
#else
#define F64_BELOW_CLASS(code) GBA_RESTOREFILE_EINCOMPAT
#endif
    {
        unsigned int f64fnv_a, f64fnv_b, romfnv_a, romfnv_b;

        mk_flash64_payload(flash64_payload_a,
                           (unsigned int)sizeof flash64_payload_a, 0x01234567u);
        mk_flash64_payload(flash64_payload_b,
                           (unsigned int)sizeof flash64_payload_b, 0x89ABCDEFu);
        f64fnv_a = ref_fnv(flash64_payload_a,
                           (unsigned int)sizeof flash64_payload_a);
        f64fnv_b = ref_fnv(flash64_payload_b,
                           (unsigned int)sizeof flash64_payload_b);

        ck(f64fnv_a != f64fnv_b,
           "the two FLASH64 fixtures really do differ -- so a byte-exactness "
           "assertion cannot pass by restoring the wrong buffer");

        /* ---- THE GEOMETRY IS THE ONE THE WRITER AND READER BOTH AGREE ON -- */
        ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_FLASH64), 65536u,
             "FLASH64 persists exactly 65536 bytes");
        ck_u(gba_savehdr_declared_of(GBA_SAVEHDR_CLS_FLASH64), 65536u,
             "FLASH64 declares exactly 65536 bytes");
        ck_u(GBA_SAVEHDR_CLS_FLASH64, 2u, "FLASH64 is class 2");

        /* ---- THE CARTRIDGE STATE, AND WHAT IT DOES NOT PROVE -------------- */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        romfnv_a = gba_save_rom_hash();

        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "the MOTHER 3 case really is a live UNKNOWN");
        ck_u(gba_save_class(), GBA_SAVE_UNKNOWN,
             "and the launcher would print SAVE UNKNOWN, as it did on hardware");
        ck_u(gba_restore_read(GBA_RESTORE_RD_BACKUP_TYPE),
             (unsigned int)BACKUP_UNKN, "the raw type is UNKN");
        ck_u(gba_restore_read(GBA_RESTORE_RD_BANKCNT),
             (unsigned int)FLASH_SIZE_64KB,
             "the raw bank count is the UNIT COUNT 1, not a byte size");
        ck_u(gba_restore_db_flash128(), 0u,
             "***** THERE IS NO DATABASE EVIDENCE HERE AND NONE IS USED -- the "
             "FLASH128 bit asserts a 128 KB part and must never admit this "
             "save *****");
        ck(romfnv_a != romfnv,
           "the A3UJ fixture is a DIFFERENT cartridge from the ATHE one");

        /* ---- ***** THE HEADER DECIDES THE CLASS, NOT THE LIVE STATE *****
         *
         * THE SAME CARTRIDGE STATE, A DIFFERENT HEADER, A DIFFERENT OUTCOME.
         * backup_type UNKN + flash_bank_cnt 64 KB is precisely the state the
         * FLASH64 cases below run in -- and here it admits an EEPROM512 save
         * through the EEPROM carve-out and activates NOTHING. If the live state
         * were treated as evidence of a flash part, this restore would be
         * wrong; it is not, so it is not.
         *
         * IT RUNS IN BOTH CONFIGURATIONS and its verdict is identical in both,
         * which is what makes it a control rather than a variant. */
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM512, 8192u, 512u,
               romfnv_a, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "an EEPROM512 save on a UNKN + 64 KB cartridge is accepted by the "
             "EEPROM carve-out");
        ck_u(gba_restore_applied(), 8192u, "8192 bytes, not 65536");
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** AND IT ACTIVATED NOTHING -- the identical live state proves "
             "no class, so only the HEADER can decide one *****");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "the reset seed is untouched by it too");
        remove_files();

        /* ---- NEGATIVES ABOVE THE CLASS GATE: IDENTICAL IN BOTH BUILDS ----
         *
         * Geometry and the ROM binding are checked BEFORE the class gate, so
         * these verdicts do not depend on the configuration at all and are
         * written without the macro. THE BINDING IS THE WHOLE BASIS OF THE
         * TRUST ARGUMENT, so it is asserted here specifically, under a live
         * UNKNOWN, with a legal FLASH64 header. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a ^ 0xDEADBEEFu, f64fnv_a, 1u, 1, 0u);
        expect_reject("***** FLASH64 + live UNKNOWN + WRONG ROM -> EROMID *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EROMID);
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "a ROM-refused FLASH64 save activated nothing");

        /* 65536 is the ONLY region a FLASH64 header may declare. The rule
         * authorises a CLASS, never a size. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 32768u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        expect_reject("FLASH64 + live UNKNOWN + region 32768 -> EGEOM",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EGEOM);

        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 32768u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        expect_reject("FLASH64 declaring 32768 -> EGEOM",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EGEOM);

        /* ---- NEGATIVES BELOW THE CLASS GATE: REFUSED IN BOTH BUILDS, with
         * the code each configuration can actually produce. ---------------- */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        expect_reject("FLASH64 + a 65535-byte .sav is REFUSED",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a, 65535u,
                      F64_BELOW_CLASS(GBA_RESTOREFILE_ESHORTREAD));
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** A TRUNCATED FLASH64 SAVE ACTIVATED NOTHING *****");

        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        {
            static unsigned char longsav[65537];
            memcpy(longsav, flash64_payload_a, sizeof flash64_payload_a);
            longsav[65536] = 0x77;
            mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
                   romfnv_a, f64fnv_a, 1u, 1, 0u);
            expect_reject("FLASH64 + a 65537-byte .sav is REFUSED",
                          hdr, GBA_SAVEHDR_BYTES, longsav, 65537u,
                          F64_BELOW_CLASS(GBA_RESTOREFILE_ESAVSIZE));
        }
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "an oversized FLASH64 save activated nothing");

        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a ^ 0x00000001u, 1u, 1, 0u);
        expect_reject("FLASH64 + a STALE payload hash is REFUSED",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      F64_BELOW_CLASS(GBA_RESTOREFILE_EPAYFNV));
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** A STALE FLASH64 SAVE ACTIVATED NOTHING *****");

        /* ***** IDENTITY A's SAVE OFFERED TO CARTRIDGE B. *****
         *
         * The header is PERFECTLY VALID -- correct magic, version, geometry and
         * payload hash -- and belongs to a different cartridge. This is the case
         * a single-identity fixture cannot express, and it is refused in BOTH
         * configurations because the ROM gate sits above the class gate. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "B46E", 0x5EEDB46Eu);
        romfnv_b = gba_save_rom_hash();
        ck(romfnv_b != romfnv_a,
           "the two FLASH64 cartridges really do have different identities");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        expect_reject("***** IDENTITY A's VALID FLASH64 SAVE IS REFUSED BY "
                      "CARTRIDGE B -> EROMID *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EROMID);
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** ANOTHER CARTRIDGE'S SAVE ACTIVATED NOTHING *****");

        /* ---- A GENUINE FAMILY MISMATCH IS STILL A MISMATCH, BOTH BUILDS ---
         *
         * These cartridges have a LIVE class, so the eligibility precondition
         * fails on its FIRST term and the ordinary family rule decides. Nothing
         * about the FLASH64 case may widen these. */
        setup_cart_code(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_SRAM, "live class SRAM");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               gba_save_rom_hash(), f64fnv_a, 1u, 1, 0u);
        expect_reject("FLASH64 header + live SRAM -> EINCOMPAT",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EINCOMPAT);

        setup_cart_code(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_EEPROM512,
             "live class EEPROM512");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               gba_save_rom_hash(), f64fnv_a, 1u, 1, 0u);
        expect_reject("FLASH64 header + live EEPROM512 -> EINCOMPAT",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EINCOMPAT);

        /* ***** THE ELIGIBILITY PRECONDITION'S SECOND TERM, DROPPED. *****
         *
         * backup_type is UNKN and the ROM binding is exact -- only the GEOMETRY
         * differs, because gba_over.h asserted a 128 KB part for this fixture.
         * A FLASH64 header is refused here in BOTH configurations, which is what
         * proves the live-state observation is a real precondition and not a
         * rubber stamp. ([10c] pins the same thing from the FLASH128 side.) */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_128KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "still a live UNKNOWN -- only the bank count changed");
        ck_u(gba_restore_db_flash128(), 1u,
             "and this fixture DOES carry the 128 KB database assertion");
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               gba_save_rom_hash(), f64fnv_a, 1u, 1, 0u);
        expect_reject("***** FLASH64 + live UNKNOWN + 128 KB GEOMETRY IS "
                      "REFUSED IN BOTH CONFIGURATIONS *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EINCOMPAT);
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** AN INELIGIBLE CARTRIDGE ACTIVATED NOTHING *****");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_128KB,
             "and its bank count was not rewritten to suit the header");

#ifndef LUAPORT_SESSION_REUSE
        /* ===============================================================
         * THE FROZEN-MILESTONE VERDICT: A PERFECTLY VALID FLASH64 SAVE IS
         * STILL REFUSED.
         *
         * ***** THIS IS NOT THE DEFECT BEING PRESERVED, IT IS THE GUARD BEING
         * PROVED. ***** M13C, M12C, M12B and M11 are built WITHOUT
         * LUAPORT_SESSION_REUSE and must keep byte-identical behaviour. This
         * case asserts that the FLASH64 rule is genuinely absent from that
         * configuration -- if the production change were ever added UNGUARDED,
         * this assertion fails and `make m12c-equiv` goes red.
         * =============================================================== */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        expect_reject("***** A FULLY VALID FLASH64 SAVE IS REFUSED WHEN "
                      "LUAPORT_SESSION_REUSE IS ABSENT *****",
                      hdr, GBA_SAVEHDR_BYTES,
                      flash64_payload_a,
                      (unsigned int)sizeof flash64_payload_a,
                      GBA_RESTOREFILE_EINCOMPAT);
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** AND THE FROZEN CONFIGURATION ACTIVATES NOTHING, EVER *****");
        ck(!gba_savehdr_compatible_bound(GBA_SAVEHDR_CLS_FLASH64,
                                         GBA_SAVEHDR_CLS_UNKNOWN, 1),
           "the bound form still refuses FLASH64 + UNKNOWN here");
        (void)romfnv_b;
        (void)f64fnv_b;
#else
        /* ===============================================================
         * ***** THE CORRECTION, PROVED END TO END, ON TWO IDENTITIES *****
         *
         * Everything above ran in both configurations. Everything below runs
         * only under LUAPORT_SESSION_REUSE, and it is the entire behavioural
         * difference between the two builds.
         * =============================================================== */

        /* ---- IDENTITY A: VALIDATE FIRST -- ELIGIBLE, AND ZERO MUTATION ---
         *
         * ***** THE STAGE 0 CONTRACT, PROVED FOR FLASH64 EXACTLY AS IT WAS FOR
         * EEPROM, SRAM AND FLASH128. ***** Armed on purpose, so the passivity
         * comes from the MODE and not from the arm gate. MODE_VALIDATE is
         * documented as provably passive; activating an emulator global inside
         * it would break that guarantee, so the assertion is explicit. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        romfnv_a = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, flash64_payload_a,
                   (unsigned int)sizeof flash64_payload_a);

        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "VALIDATE accepts FLASH64 + live UNKNOWN + exact ROM");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "VALIDATE proved the 65536-byte payload hash");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_APPLIED) == 0u,
           "VALIDATE never reached the apply");
        ck_u(gba_restore_applied(), 0u, "VALIDATE applied zero bytes");
        ck(backup_is_blank(),
           "***** FLASH64 ELIGIBLE BUT ZERO MUTATION -- 131072 BYTES "
           "UNTOUCHED *****");
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** MODE_VALIDATE REMAINS PROVABLY PASSIVE -- NO ACTIVATION "
             "HAPPENED *****");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "VALIDATE did not touch the reset seed");
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "and the live class is still UNKNOWN after a validate");

        /* ---- IDENTITY A: THE REAL RESTORE ----------------------------- */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** A VALID ROM-BOUND FLASH64 SAVE IS ACCEPTED UNDER A LIVE "
             "UNKNOWN *****");
        ck_u(gba_restore_applied(), 65536u,
             "***** EXACTLY 65536 BYTES WERE RESTORED *****");

        /* ---- BYTE-EXACTNESS, AND THE TAIL ABOVE THE REGION ------------- */
        {
            int same = 1;
            for (i = 0; i < 65536u; i++)
                if (gamepak_backup[i] != flash64_payload_a[i]) {
                    same = 0;
                    break;
                }
            ck(same, "***** EVERY ONE OF THE 65536 RESTORED BYTES MATCHES "
                     "EXACTLY *****");
        }
        ck_u(gba_restore_region_fnv(65536u), f64fnv_a,
             "the ARRAY hashes to the 65536-byte payload hash");
        {
            int tail_ok = 1;
            for (i = 65536u; i < GBA_RESTORE_BACKUP_BYTES; i++)
                if (gamepak_backup[i] != 0xFF) { tail_ok = 0; break; }
            ck(tail_ok,
               "bytes 65536..131071 are still 0xFF -- a 64 KB part has no "
               "upper bank and nothing above the region was written");
        }

        /* ---- THE LADDER RECORDED EVERY STAGE --------------------------- */
        {
            unsigned int L = gba_restorefile_latch();
            ck((L & GBA_RESTOREFILE_L_HDRVALID) != 0u, "latch: header valid");
            ck((L & GBA_RESTOREFILE_L_ROMOK)    != 0u,
               "latch: the ROM gate passed -- the binding is load bearing");
            ck((L & GBA_RESTOREFILE_L_CLASSOK)  != 0u, "latch: class gate");
            ck((L & GBA_RESTOREFILE_L_SIZEOK)   != 0u, "latch: size exact");
            ck((L & GBA_RESTOREFILE_L_PAYOK)    != 0u, "latch: PASS 1 hash");
            ck((L & GBA_RESTOREFILE_L_APPLIED)  != 0u, "latch: PASS 2 applied");
            ck((L & GBA_RESTOREFILE_L_VERIFIED) != 0u, "latch: array verified");
            ck((L & GBA_RESTOREFILE_L_ROLLBACK) == 0u, "latch: NO rollback");
            ck((L & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
               "latch: FAMTRUST -- the live class really was UNKNOWN");

            /* ***** AND THE LOG MUST NOT CLAIM THE DATABASE ADMITTED IT.
             * ***** L_DBFLASH means "gba_over.h asserted a 128 KB flash part".
             * No such assertion exists for a FLASH64 cartridge -- indeed
             * FLAGS_FLASH_64KB does not exist at all -- so a set bit here would
             * be a LIE in the operator's log about why this save was allowed. */
            ck((L & GBA_RESTOREFILE_L_DBFLASH)  == 0u,
               "***** L_DBFLASH IS CLEAR -- no 128 KB database assertion is "
               "claimed for a FLASH64 restore *****");
        }
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_DBTRUST), 0u,
             "the DBTRUST selector reports 0 -- the database did not admit it");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_DBFLASH128), 0u,
             "and no 128 KB evidence was observed on this cartridge");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_FAMTRUST), 1u,
             "the FAMTRUST selector reports 1");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_CLASS),
             GBA_SAVEHDR_CLS_FLASH64, "the header's class was FLASH64");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_REGION), 65536u,
             "and its region was 65536");

        /* ---- ***** THE ACTIVATION, AND ITS EXACT SCOPE ***** -----------
         *
         * ONE STORE, AND ONLY ONE. backup_type moves from BACKUP_UNKN to
         * BACKUP_FLASH, which -- with flash_bank_cnt already at its 64 KB value
         * -- is EXACTLY the state a natively detected 64 KB flash cartridge
         * holds after init_memory(). Nothing is invented; the one global gpSP
         * failed to record is supplied, and only after the complete ladder has
         * proved the header that authorises it.
         *
         * EVERY OTHER GLOBAL MUST BE PROVED UNTOUCHED, INDIVIDUALLY. A fix that
         * "worked" by rewriting the geometry, the EEPROM size or the reset seed
         * would pass a byte-exactness test and be wrong in ways that surface
         * one reset later. */
        ck_u(backup_type, (u32)BACKUP_FLASH,
             "***** backup_type IS NOW BACKUP_FLASH -- THE ACTIVATION *****");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB,
             "***** flash_bank_cnt IS UNCHANGED -- the geometry was NOT "
             "written *****");
        ck_u(eeprom_size, (u32)EEPROM_512_BYTE,
             "***** eeprom_size IS UNCHANGED *****");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "***** backup_type_reset WAS NOT WRITTEN -- a class derived from a "
             "FILE must not survive the next reset *****");

        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_FLASH64,
             "***** THE LIVE CLASS NOW RESOLVES FLASH64 *****");
        ck_u(gba_restore_db_flash128(), 0u,
             "and with a real type recorded there is no database gap to report");

        /* ---- ***** THE WRITER AGREES -- THE 32 KB HAZARD IS CLOSED *****
         *
         * THIS IS THE ASSERTION THAT MAKES ACTIVATION NECESSARY RATHER THAN
         * MERELY TIDY. Had the save been admitted WITHOUT activating, the
         * cartridge's first backup read would collapse backup_type to
         * BACKUP_SRAM inside read_backup() -- permanently -- and a clean-and-
         * modified exit would then commit through gs_region_of(SRAM) = 32768: a
         * 32 KB SRAM-classed file written over a 64 KB FLASH64 save, accepted on
         * the next launch by the existing SRAM carve-out, silently destroying
         * the upper half with every gate reporting green.
         *
         * After the activation the writer resolves FLASH64 and 65536, so the
         * next commit is the same shape as the file that was just restored. */
        ck_u(gba_save_class(), GBA_SAVE_FLASH64,
             "***** THE FROZEN WRITER NOW RESOLVES FLASH64 *****");
        ck_u(gba_save_region_bytes(), 65536u,
             "***** AND A 65536-BYTE REGION -- NOT 32768 *****");
        {
            /* ZERO-INITIALISED ON PURPOSE. In the RED run -- before the
             * production change exists -- the class is still UNKNOWN here and
             * the frozen writer may legitimately refuse to build anything. The
             * parse below would then be reading whatever was on the stack, and
             * a test that fails for a DIFFERENT reason each run is worse than
             * useless. Zeroed, the refusal reports itself as EMAGIC, every
             * time. */
            unsigned char built[GBA_SAVE_HDR_BYTES] = { 0 };
            struct gba_savehdr parsed;

            ck_u(gba_save_header_build(built, (unsigned int)sizeof built,
                                       f64fnv_a),
                 GBA_SAVE_HDR_BYTES,
                 "the FROZEN writer builds a header from the activated state");
            ck_i(gba_savehdr_parse(built, GBA_SAVE_HDR_BYTES, &parsed),
                 GBA_SAVEHDR_OK, "and the shipping reader accepts it");
            ck_u(parsed.cls, GBA_SAVEHDR_CLS_FLASH64,
                 "***** THE NEXT COMMIT WOULD BE CLASS FLASH64 *****");
            ck_u(parsed.region, 65536u,
                 "***** AND REGION 65536 -- the round trip is stable *****");
            ck_u(parsed.declared, 65536u, "declared 65536");
            ck_u(parsed.rom_fnv, romfnv_a,
                 "and still bound to THIS cartridge");
        }
        remove_files();

        /* ---- IDENTITY B: THE SIMS 2, A SECOND CARTRIDGE AND PAYLOAD -----
         *
         * NOT A COPY OF THE CASE ABOVE. A different ROM body, a different code,
         * a different identity hash and a different 65,536-byte payload. One
         * fixture can pass because the harness happened to agree with itself;
         * two independently generated ones cannot. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "B46E", 0x5EEDB46Eu);
        romfnv_b = gba_save_rom_hash();
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "the SIMS 2 case is a live UNKNOWN too");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_b, f64fnv_b, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, flash64_payload_b,
                   (unsigned int)sizeof flash64_payload_b);

        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** THE SECOND FLASH64 IDENTITY IS ACCEPTED TOO *****");
        ck_u(gba_restore_applied(), 65536u, "exactly 65536 bytes were restored");
        {
            int same = 1;
            for (i = 0; i < 65536u; i++)
                if (gamepak_backup[i] != flash64_payload_b[i]) {
                    same = 0;
                    break;
                }
            ck(same,
               "***** AND IT RESTORED ITS OWN PAYLOAD, NOT IDENTITY A's *****");
        }
        ck_u(gba_restore_region_fnv(65536u), f64fnv_b,
             "the array hashes to identity B's payload hash");
        ck_u(backup_type, (u32)BACKUP_FLASH, "identity B activated too");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB, "geometry still untouched");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN, "reset seed still untouched");
        ck_u(gba_save_class(), GBA_SAVE_FLASH64,
             "and its writer resolves FLASH64 as well");
        remove_files();

        /* ---- ***** A ROLLBACK MUST NOT LEAVE ACTIVATION BEHIND ***** ----
         *
         * THE ORDERING REQUIREMENT, STATED AS A TEST. Activation happens only
         * after the complete ladder has succeeded -- after PASS 2 has applied
         * AND re-hashed the array. An unarmed RESTORE reaches PASS 2, is
         * refused by the arm gate, and rolls the whole array back to 0xFF. If
         * activation were performed any earlier, this cartridge would be left
         * classified FLASH64 with NO save behind it, and the next commit would
         * write a 65,536-byte file of blank flash over whatever was on disk.
         *
         * Deliberately NOT armed, exactly as [7] does it. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "A3UJ", 0x5EED0A3Au);
        romfnv_a = gba_save_rom_hash();
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_FLASH64, 65536u, 65536u,
               romfnv_a, f64fnv_a, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, flash64_payload_a,
                   (unsigned int)sizeof flash64_payload_a);

        ck_u(gba_restore_armed(), 0u, "the adapter starts disarmed");
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);

        ck_i(rc, GBA_RESTOREFILE_EAPPLY,
             "an unarmed FLASH64 RESTORE reports EAPPLY");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_PAYOK) != 0u,
           "PASS 1 had succeeded before PASS 2 refused");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_ROLLBACK) != 0u,
           "the rollback was latched");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_VERIFIED) == 0u,
           "the array was NOT reported verified");
        ck(backup_is_blank(),
           "the array is all 0xFF -- not partly written");
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** A ROLLED-BACK FLASH64 RESTORE LEFT NO ACTIVATION "
             "BEHIND *****");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "and no reset seed behind either");
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "***** THE CARTRIDGE IS STILL UNCLASSIFIED AFTER A FAILED "
             "RESTORE *****");
        remove_files();
#endif /* LUAPORT_SESSION_REUSE */
    }
#undef F64_BELOW_CLASS

    /* ===================================================================
     * 10g. ***** THE EEPROM8K REGRESSION CONTROL -- ZELDA ALttP + FOUR SWORDS
     * ===================================================================
     *
     * ##### THIS IS A CONTROL, NOT A DEFECT. #####
     *
     * AZLE is HARDWARE-CONFIRMED WORKING on this build: class 5, region 8192,
     * declared 8192, a valid payload FNV and an exact ROM binding, accepted and
     * restored under a live UNKNOWN by the EEPROM carve-out. Nothing about it is
     * broken and nothing about it is being changed.
     *
     * IT IS ASSERTED HERE PRECISELY BECAUSE IT WORKS. The FLASH64 correction
     * above touches the live-UNKNOWN branch that AZLE also passes through, and
     * "we did not break the working case" is a claim that needs a measurement
     * rather than an argument. Every assertion in this section has the SAME
     * verdict in BOTH configurations -- that identity is the whole point.
     *
     * THE FIXTURE IS SYNTHETIC. The cartridge is generated by setup_cart_code()
     * and the payload is this file's own 8,192-byte EEPROM-shaped buffer. No
     * commercial save data is read, embedded or committed.
     * =================================================================== */
    printf("[10g] the EEPROM8K CONTROL (Zelda ALttP + Four Swords, AZLE):\n");
    printf("      unchanged in BOTH configurations\n");
    {
        /* ---- THE CLASS NUMBERING AND GEOMETRY THE HARDWARE REPORTED ----- */
        ck_u(GBA_SAVEHDR_CLS_EEPROM8K, 5u, "AZLE's class is 5");
        ck_u(gba_savehdr_region_of(GBA_SAVEHDR_CLS_EEPROM8K), 8192u,
             "region 8192");
        ck_u(gba_savehdr_declared_of(GBA_SAVEHDR_CLS_EEPROM8K), 8192u,
             "declared 8192");

        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "AZLE", 0x5EEDA21Eu);
        romfnv = gba_save_rom_hash();

        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "AZLE reaches the restore point as a live UNKNOWN, as on hardware");

        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM8K, 8192u, 8192u,
               romfnv, payfnv, 1u, 1, 0u);
        write_file(T_HDR, hdr, GBA_SAVEHDR_BYTES);
        write_file(T_SAV, payload, (unsigned int)sizeof payload);

        /* VALIDATE: eligible, and zero mutation. */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_VALIDATE);
        ck_i(rc, GBA_RESTOREFILE_OK, "AZLE: VALIDATE accepts");
        ck_u(gba_restore_applied(), 0u, "AZLE: VALIDATE applied zero bytes");
        ck(backup_is_blank(), "AZLE: VALIDATE left all 131072 bytes untouched");

        /* RESTORE. */
        gba_restore_arm();
        rc = gba_restorefile_with(T_SAV, T_HDR, GBA_RESTOREFILE_MODE_RESTORE);
        ck_i(rc, GBA_RESTOREFILE_OK,
             "***** AZLE: A VALID EEPROM8K SAVE IS ACCEPTED, UNCHANGED *****");
        ck_u(gba_restore_applied(), 8192u, "AZLE: exactly 8192 bytes restored");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_CLASS),
             GBA_SAVEHDR_CLS_EEPROM8K, "AZLE: the class is 5");
        ck_u(gba_restorefile_read(GBA_RESTOREFILE_RD_REGION), 8192u,
             "AZLE: the region is 8192");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_FAMTRUST) != 0u,
           "AZLE: admitted by the EEPROM carve-out, as it always was");
        ck((gba_restorefile_latch() & GBA_RESTOREFILE_L_DBFLASH) == 0u,
           "AZLE: no flash evidence is claimed");
        {
            int same = 1;
            for (i = 0; i < 8192u; i++)
                if (gamepak_backup[i] != payload[i]) { same = 0; break; }
            ck(same, "AZLE: every one of the 8192 bytes matches exactly");
        }
        {
            int tail_ok = 1;
            for (i = 8192u; i < GBA_RESTORE_BACKUP_BYTES; i++)
                if (gamepak_backup[i] != 0xFF) { tail_ok = 0; break; }
            ck(tail_ok, "AZLE: nothing above the region was written");
        }

        /* ***** AND NOT ONE CLASSIFICATION GLOBAL WAS SEEDED. *****
         * THE CONTROL'S CENTRAL ASSERTION. FLASH64 activation is scoped to a
         * FLASH64 header; an EEPROM restore must go on seeding nothing at all,
         * exactly as it did before the correction existed. */
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** AZLE: backup_type WAS NOT SEEDED -- still UNKN *****");
        ck_u(eeprom_size, (u32)EEPROM_512_BYTE,
             "***** AZLE: eeprom_size WAS NOT SEEDED *****");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB,
             "AZLE: flash_bank_cnt was not seeded");
        ck_u(backup_type_reset, (u32)BACKUP_UNKN,
             "AZLE: the reset seed was not written");
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_UNKNOWN,
             "***** AZLE: THE LIVE CLASS IS STILL UNKNOWN AFTER THE RESTORE "
             "*****");
        remove_files();

        /* THE BINDING IS STILL MANDATORY FOR THE CONTROL TOO. */
        setup_cart_code(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE,
                        "AZLE", 0x5EEDA21Eu);
        mk_hdr(hdr, 1u, GBA_SAVEHDR_CLS_EEPROM8K, 8192u, 8192u,
               romfnv ^ 0xDEADBEEFu, payfnv, 1u, 1, 0u);
        expect_reject("AZLE: wrong ROM -> EROMID, unchanged",
                      hdr, GBA_SAVEHDR_BYTES,
                      payload, (unsigned int)sizeof payload,
                      GBA_RESTOREFILE_EROMID);

        /* AND THE EEPROM512 / SRAM CONTROLS ARE ALREADY ASSERTED IN [10] --
         * cases (1) and (9) both prove "backup_type was NOT seeded" after an
         * accepted restore, and both run in BOTH configurations. They are not
         * repeated here. */
    }

    /* ===================================================================
     * 11. THE LIVE CLASS RESOLUTION IS PASSIVE AND CORRECT
     *
     * It must resolve BACKUP_FLASH and BACKUP_EEPROM against their companion
     * UNIT COUNTS, and it must not disturb any gpSP global while doing so.
     * =================================================================== */
    printf("[11] the live class resolves against the UNIT COUNTS, passively\n");
    {
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_8_KBYTE);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_EEPROM8K, "EEPROM 8K");
        setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_FLASH128, "FLASH 128K");
        setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_FLASH64, "FLASH 64K");
        setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        ck_u(gba_restore_live_class(), GBA_RESTORE_CLS_SRAM, "SRAM");

        /* ***** THE ADAPTER MUTATES NO CLASSIFICATION GLOBAL. ***** */
        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
        {
            u32 bt = backup_type, bc = flash_bank_cnt, es = eeprom_size;
            (void)gba_restore_live_class();
            (void)gba_restore_read(GBA_RESTORE_RD_LIVECLASS);
            (void)gba_restore_nonff(8192u);
            (void)gba_restore_region_fnv(8192u);
            ck(backup_type == bt,    "backup_type was not written");
            ck(flash_bank_cnt == bc, "flash_bank_cnt was not written");
            ck(eeprom_size == es,    "eeprom_size was not written");
        }
    }

    /* ===================================================================
     * 12. THE APPLY IS DOUBLE-CLAMPED
     * =================================================================== */
    printf("[12] the apply is bounded by BOTH the region and the array\n");
    {
        reset_all();
        gba_restore_arm();

        ck_u(gba_restore_apply(payload, 8190u, 4096u, 8192u), 2u,
             "clamped to the remaining bytes of the region");
        ck_u(gba_restore_apply(payload, 8192u, 16u, 8192u), 0u,
             "an offset at the region end applies nothing");
        ck_u(gba_restore_apply(payload, 0u, 16u, 0u), 0u,
             "a zero region applies nothing");

        /* A region larger than the array must still not walk off the end. */
        reset_all();
        gba_restore_arm();
        ck_u(gba_restore_apply(payload, GBA_RESTORE_BACKUP_BYTES - 4u, 4096u,
                               GBA_RESTORE_BACKUP_BYTES + 4096u), 4u,
             "an oversized region is clamped to the ARRAY, not trusted");

        ck_u(gba_restore_apply(0, 0u, 16u, 8192u), 0u,
             "a NULL source applies nothing");
    }

    /* ===================================================================
     * 13. THE FILENAMES M12C READS ARE THE ONES M12B WROTE
     * =================================================================== */
    printf("[13] the read names come from the FROZEN M12B builders\n");
    {
        char sav[GBA_SAVE_NAME_MAX];
        char hn[GBA_SAVE_NAME_MAX];

        setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);

        ck(gba_save_name_sav(sav, (unsigned int)sizeof sav) != 0u,
           "the .sav name builds");
        ck(gba_save_name_hdr(hn, (unsigned int)sizeof hn) != 0u,
           "the .hdr name builds");
        ck(strncmp(sav, "/savedata0/gba/ATHE_", 20) == 0,
           "the .sav name is /savedata0/gba/ATHE_<HASH8>.sav");
        ck(strcmp(sav + strlen(sav) - 4, ".sav") == 0, "it ends in .sav");
        ck(strcmp(hn + strlen(hn) - 4, ".hdr") == 0, "it ends in .hdr");

        /* NO IDENTITY -> NO FILENAME -> the ladder refuses before touching
         * the filesystem at all. */
        reset_all();
        ck_i(gba_restorefile_run(GBA_RESTOREFILE_MODE_RESTORE),
             GBA_RESTOREFILE_ENOID,
             "an unlatched cartridge refuses with ENOID");
        ck(backup_is_blank(), "PASSIVITY: ENOID touched nothing");
    }

    /* ===================================================================
     * 14. EVERY REJECTION HAS A DISTINCT, NAMED SPELLING
     * =================================================================== */
    printf("[14] every code has its own operator-facing name\n");
    {
        int codes[17] = {
            GBA_RESTOREFILE_OK,         GBA_RESTOREFILE_ENOID,
            GBA_RESTOREFILE_ENOSAVE,    GBA_RESTOREFILE_EORPHANSAV,
            GBA_RESTOREFILE_EORPHANHDR, GBA_RESTOREFILE_EHDRSIZE,
            GBA_RESTOREFILE_EMAGIC,     GBA_RESTOREFILE_EVERSION,
            GBA_RESTOREFILE_ERESERVED,  GBA_RESTOREFILE_ECLASS,
            GBA_RESTOREFILE_EGEOM,      GBA_RESTOREFILE_EROMID,
            GBA_RESTOREFILE_EINCOMPAT,  GBA_RESTOREFILE_ESAVSIZE,
            GBA_RESTOREFILE_EPAYFNV,    GBA_RESTOREFILE_ESHORTREAD,
            GBA_RESTOREFILE_EAPPLY
        };
        int a, b;
        for (a = 0; a < 17; a++) {
            ck(gba_restorefile_name(codes[a]) != 0, "a name exists");
            ck(strcmp(gba_restorefile_name(codes[a]),
                      "UNKNOWN RESTORE RESULT") != 0,
               "the code is not falling through to the default");
            for (b = a + 1; b < 17; b++)
                ck(strcmp(gba_restorefile_name(codes[a]),
                          gba_restorefile_name(codes[b])) != 0,
                   "no two codes share a spelling");
        }
    }

    /* ===================================================================
     * 15. THE SRAM UPPER-HALF OBSERVER -- IT MEASURES, AND IT NEVER MUTATES
     * ===================================================================
     *
     * The observer exists to settle ONE question that no amount of source
     * reading can settle: does the running game write bytes into
     * gamepak_backup[32768..65535]?
     *
     * gpSP masks every SRAM access with `address & 0xFFFF` (gba_memory.c:509-519
     * on read, :1252 on write) and indexes gamepak_backup[] directly, with NO
     * `& 0x7FFF` anywhere on the path. So the emulator exposes a 65,536-byte
     * LINEAR window while M12C persists 32,768. If the game uses the upper half,
     * that half is lost on exit -- a save that works while playing and is gone
     * after a relaunch, which is the reported symptom exactly.
     *
     * ***** THESE CHECKS DO NOT TEST THE HYPOTHESIS. ***** They test the
     * INSTRUMENT. A broken observer would answer the question confidently and
     * wrongly, and the answer would then be used to justify changing the save
     * format -- so the instrument is calibrated here against known inputs before
     * it is ever pointed at a cartridge. */
    printf("[15] the SRAM upper-half observer -- calibration and passivity\n");
    {
        unsigned int lo_t1, hi_t1, bits, applied;

        /* ---- A. A BLANK ARRAY READS AS BLANK IN BOTH HALVES ---------- */
        reset_all();                        /* 131,072 bytes of 0xFF       */
        m12c_sramobs_reset();

        /* SEEDED EXPLICITLY, BECAUSE reset_all() DELIBERATELY DOES NOT TOUCH
           gpSP's CLASSIFICATION GLOBALS. Section 14 leaves them wherever its
           last scenario put them, so the "the observer never touched them"
           assertions at the end of this section would otherwise be comparing
           against an inherited value and would pass or fail for reasons that
           have nothing to do with the observer. */
        backup_type    = BACKUP_UNKN;
        eeprom_size    = EEPROM_512_BYTE;
        flash_bank_cnt = FLASH_SIZE_64KB;

        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES), 0u,
             "A: the observer starts with zero samples");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_BITS), 0u,
             "A: with no samples there are no interpretation bits");

        obs_sample_passive(GBA_SRAMOBS_T1);

        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF + GBA_SRAMOBS_T1), 0u,
             "A: a blank low half counts zero non-FF bytes");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T1), 0u,
             "A: a blank high half counts zero non-FF bytes");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_TAKEN + GBA_SRAMOBS_T1), 1u,
             "A: slot T1 records that it was taken");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES), 1u,
             "A: exactly one sample has been taken");

        /* TWO BLANK HALVES ARE THE SAME 32,768 BYTES, SO THEY MUST HASH THE
           SAME. If they do not, the two offsets are not the ones the header
           names and every later comparison is meaningless. */
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T1),
             m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T1),
             "A: both blank halves hash identically");

        /* ***** AND IT IS THE PROJECT'S FNV, NOT A PRIVATE ONE. ***** The low
           half's hash must be directly comparable with the payload hash M12B
           wrote into the .hdr and with gba_restore_region_fnv(). A private
           algorithm would produce numbers comparable only with themselves. */
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T1),
             ref_fnv(gamepak_backup, GBA_SRAMOBS_HALF_BYTES),
             "A: the low hash is the project's FNV-1a over [0,32768)");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T1),
             ref_fnv(gamepak_backup + GBA_SRAMOBS_HIGH_OFF,
                     GBA_SRAMOBS_HALF_BYTES),
             "A: the high hash is the project's FNV-1a over [32768,65536)");

        /* ---- D. IDENTICAL BYTES HASH IDENTICALLY --------------------- */
        lo_t1 = m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV  + GBA_SRAMOBS_T1);
        hi_t1 = m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T1);

        obs_sample_passive(GBA_SRAMOBS_T2);

        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T2), lo_t1,
             "D: an unchanged low half hashes the same at T2");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T2), hi_t1,
             "D: an unchanged high half hashes the same at T2");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_BITS), 0u,
             "D: T2 == T3 has not happened yet, so no change bit is set");

        /* ---- B. ONE LOW BYTE -- THE 'CASE B REFUTED' SHAPE ----------- */
        gamepak_backup[0x0123] = 0x5A;
        obs_sample_passive(GBA_SRAMOBS_T3);

        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);

        ck(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T3) != lo_t1,
           "B: a changed low byte changes the low hash");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF + GBA_SRAMOBS_T3), 1u,
             "B: exactly one non-FF byte in the low half");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T3), hi_t1,
             "B: a low-half write leaves the high half's hash ALONE");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T3), 0u,
             "B: the high half still counts zero non-FF bytes");

        ck((bits & GBA_SRAMOBS_B_LOW_CHANGED) != 0u,
           "B: BIT 3 reports the low half changed between T2 and T3");
        ck((bits & GBA_SRAMOBS_B_HIGH_CHANGED) == 0u,
           "B: BIT 2 does NOT fire on a low-half-only change");
        ck((bits & GBA_SRAMOBS_B_HIGH_NONFF_T3) == 0u,
           "B: BIT 0 is clear while the high half is blank");
        ck((bits & GBA_SRAMOBS_B_HIGH_NONFF_T1) == 0u,
           "B: BIT 1 is clear -- nothing populated the high half before T2");

        /* ---- C. ONE HIGH BYTE -- THE HYPOTHESIS SHAPE ---------------- */
        reset_all();
        m12c_sramobs_reset();

        obs_sample_passive(GBA_SRAMOBS_T1);
        obs_sample_passive(GBA_SRAMOBS_T2);

        gamepak_backup[GBA_SRAMOBS_HIGH_OFF + 0x0010] = 0x00;
        obs_sample_passive(GBA_SRAMOBS_T3);

        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);

        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T3),
             m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T2),
             "C: a high-half write leaves the low half's hash ALONE");
        ck(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T3) !=
           m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_T2),
           "C: a changed high byte changes the high hash");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T3), 1u,
             "C: exactly one non-FF byte in the high half");

        ck((bits & GBA_SRAMOBS_B_HIGH_CHANGED) != 0u,
           "C: BIT 2 fires -- this is the CASE B CONFIRMED shape");
        ck((bits & GBA_SRAMOBS_B_LOW_CHANGED) == 0u,
           "C: BIT 3 does not fire on a high-half-only change");
        ck((bits & GBA_SRAMOBS_B_HIGH_NONFF_T3) != 0u,
           "C: BIT 0 reports the high half is non-blank at T3");
        ck((bits & GBA_SRAMOBS_B_HIGH_NONFF_T1) == 0u,
           "C: BIT 1 stays clear -- the high half was blank before execution");

        /* ---- THE SPLIT IS EXACTLY AT 32768, NOT NEAR IT --------------
         *
         * AN OFF-BY-ONE HERE WOULD BE INVISIBLE AND FATAL. If the boundary sat
         * at 32767 or 32769 the observer would attribute a byte to the wrong
         * half, and a single misattributed byte is the whole difference between
         * "the game exceeded the persisted region" and "it did not". The two
         * bytes that straddle the line are therefore tested INDIVIDUALLY. */
        reset_all();
        m12c_sramobs_reset();
        obs_sample_passive(GBA_SRAMOBS_T2);

        gamepak_backup[GBA_SRAMOBS_HALF_BYTES - 1u] = 0x01;   /* 32767: LOW  */
        obs_sample_passive(GBA_SRAMOBS_T3);
        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF + GBA_SRAMOBS_T3), 1u,
             "BOUNDARY: byte 32767 belongs to the LOW half");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T3), 0u,
             "BOUNDARY: byte 32767 is NOT counted in the high half");
        ck((bits & GBA_SRAMOBS_B_HIGH_CHANGED) == 0u,
           "BOUNDARY: the last persisted byte does not raise BIT 2");

        reset_all();
        m12c_sramobs_reset();
        obs_sample_passive(GBA_SRAMOBS_T2);

        gamepak_backup[GBA_SRAMOBS_HALF_BYTES] = 0x01;        /* 32768: HIGH */
        obs_sample_passive(GBA_SRAMOBS_T3);
        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T3), 1u,
             "BOUNDARY: byte 32768 belongs to the HIGH half");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF + GBA_SRAMOBS_T3), 0u,
             "BOUNDARY: byte 32768 is NOT counted in the low half");
        ck((bits & GBA_SRAMOBS_B_HIGH_CHANGED) != 0u,
           "BOUNDARY: the FIRST UNPERSISTED BYTE raises BIT 2");

        /* ---- WHAT A REAL 32 KB SRAM RESTORE LEAVES BEHIND ------------
         *
         * ***** THE STRUCTURAL FACT THE WHOLE INVESTIGATION RESTS ON. *****
         * Driven through the SHIPPING gba_restore_apply(), not modelled: a
         * full, successful, in-policy SRAM restore populates the low half and
         * leaves the high half at 0xFF. Every byte gpSP can reach at
         * 0x0E008000-0x0E00FFFF is therefore blank when the game boots, and
         * BIT 1 is clear -- which is what makes a later BIT 2 attributable to
         * the GAME rather than to the restore. */
        reset_all();
        m12c_sramobs_reset();
        mk_sram_payload();

        gba_restore_arm();
        /* The 4th argument is the caller's DECLARED REGION -- the same value
         * the shipping path passes from the header (gba_restorefile.c:321),
         * and the same 32768 this fixture's SRAM headers declare at :1276.
         * Here n and region are deliberately identical: a full 32 KB SRAM save
         * restored into a region exactly that large, so neither clamp fires. */
        applied = gba_restore_apply(sram_payload, 0u,
                                    (unsigned int)sizeof sram_payload,
                                    (unsigned int)sizeof sram_payload);
        ck_u(applied, (unsigned int)sizeof sram_payload,
             "RESTORE: all 32768 SRAM bytes were applied");

        obs_sample_passive(GBA_SRAMOBS_T1);

        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T1),
             ref_fnv(sram_payload, (unsigned int)sizeof sram_payload),
             "RESTORE: the low half hashes to the restored payload");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_T1), 0u,
             "***** RESTORE: THE HIGH HALF IS ENTIRELY 0xFF AFTER A 32 KB "
             "SRAM RESTORE *****");

        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);
        ck((bits & GBA_SRAMOBS_B_HIGH_NONFF_T1) == 0u,
           "RESTORE: BIT 1 is CLEAR, so a later BIT 2 is the GAME's doing");

        /* ---- AN UNTAKEN T3 MUST NOT READ AS 'NO CHANGE' --------------
         *
         * A run that died before T3 has NOT measured the game. Reporting that
         * as "the upper half did not change" would refute the hypothesis on the
         * strength of a run that never tested it -- the single most dangerous
         * wrong answer this instrument could give. */
        reset_all();
        m12c_sramobs_reset();
        obs_sample_passive(GBA_SRAMOBS_T1);
        obs_sample_passive(GBA_SRAMOBS_T2);
        gamepak_backup[GBA_SRAMOBS_HIGH_OFF] = 0x7E;    /* never sampled */

        bits = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);
        ck((bits & GBA_SRAMOBS_B_HIGH_CHANGED) == 0u,
           "INCOMPLETE: with no T3 there is no change bit -- not a false 'no'");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_TAKEN + GBA_SRAMOBS_T3), 0u,
             "INCOMPLETE: T3 correctly reports itself untaken");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES), 2u,
             "INCOMPLETE: only two samples were taken");

        /* ---- AN OUT-OF-RANGE SLOT IS DROPPED, NOT FOLDED ------------- */
        obs_sample_passive(GBA_SRAMOBS_SLOTS);       /* 3 -- one past the end */
        obs_sample_passive(99u);
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES), 2u,
             "a bad slot records nothing rather than overwriting T3");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_TAKEN + GBA_SRAMOBS_T3), 0u,
             "a bad slot did not silently become T3");

        /* ---- AN UNKNOWN SELECTOR READS 0 ----------------------------- */
        ck_u(m12c_sramobs_read(9999u), 0u, "an unknown selector reads 0");

        /* ---- reset() REALLY CLEARS ----------------------------------- */
        m12c_sramobs_reset();
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES), 0u,
             "reset clears the sample count");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_BITS), 0u,
             "reset clears the interpretation bits");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T1), 0u,
             "reset clears a recorded hash");
        ck_u(m12c_sramobs_read(GBA_SRAMOBS_RD_TAKEN + GBA_SRAMOBS_T2), 0u,
             "reset clears the taken flags");

        /* ---- THE OBSERVER DID NOT DISTURB THE SAVE ADAPTERS ---------- */
        ck_u(backup_type, (u32)BACKUP_UNKN,
             "***** THE OBSERVER NEVER TOUCHED backup_type *****");
        ck_u(eeprom_size, (u32)EEPROM_512_BYTE,
             "the observer never touched eeprom_size");
        ck_u(flash_bank_cnt, (u32)FLASH_SIZE_64KB,
             "the observer never touched flash_bank_cnt");
    }

    remove_files();

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M12C EQUIV FAILED\n");
        return 1;
    }
    printf("M12C EQUIV OK\n");
    return 0;
}
