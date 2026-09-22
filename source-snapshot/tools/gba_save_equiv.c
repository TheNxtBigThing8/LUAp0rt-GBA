/* ===========================================================================
 * tools/gba_save_equiv.c -- THE OFFLINE SAVE-PERSISTENCE HARNESS (M12B)
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and
 * #includes adapters/gba/gba_save.c VERBATIM. It therefore tests the SHIPPING
 * adapter rather than a paraphrase of it.
 *
 * RUN THIS BEFORE SPENDING ANY CONSOLE TIME. Every property below is one that
 * would otherwise have to be diagnosed from a UDP log, on hardware, after a
 * relaunch -- and at M12B several of them are not merely silent but
 * DESTRUCTIVE: a wrong size, a wrong identity or a wrong header writes a file
 * that looks completely plausible and is wrong.
 *
 * ---- WHY A HARNESS IS POSSIBLE AT ALL ----
 *
 * gba_save.c opens with #include "common.h". This file pre-defines that
 * header's own guard (COMMON_H, gpsp/common.h:20-21) BEFORE including the
 * production .c, so common.h's body -- and every header it chains to --
 * expands to NOTHING. The harness then supplies the handful of types,
 * constants and globals the adapter actually uses.
 *
 * The preprocessor must still FIND common.h before it can skip it, which is
 * why the build rule passes -I gpsp. That is a BUILD RULE change only:
 * adapters/gba/gba_save.c is not modified to suit the harness, nothing is
 * copied out of gpsp/, and no test-only #ifdef exists in the production
 * source. gba_save_equiv appears in NO object list.
 *
 * ---- WHY ONLY THE gpSP-SIDE HALF IS TESTED HERE ----
 *
 * adapters/gba/gba_savefile.c CANNOT be compiled by this harness at all: it
 * includes runtime/savedata.h, which chains to runtime/core.h, whose u64
 * typedef (`unsigned long`) collides with the one this file must supply for
 * gpSP (`unsigned long long`). That is the same type boundary that forces the
 * adapter into two translation units in the first place.
 *
 * THAT IS WHY THE HEADER BUILDER LIVES ON THIS SIDE. gba_save_header_build()
 * is pure byte arithmetic over values gba_save.c already owns, so putting it
 * here is what lets the EXACT ON-DISK LAYOUT be checked offline, byte by byte,
 * against a hand-written expected array. The runtime half then only has to
 * write those 32 bytes to a file.
 *
 * ---- WHAT IS ACTUALLY BEING PROVED ----
 *
 *   1.  THE UNIT-COUNT TRAP. eeprom_size and flash_bank_cnt are UNIT COUNTS:
 *       EEPROM_8_KBYTE is 16 and FLASH_SIZE_128KB is 2. An adapter that echoed
 *       either would write a SIXTEEN BYTE save file for an 8 KB EEPROM.
 *   2.  UNKNOWN REFUSES PERSISTENCE -- 0 bytes, no header, no chunk.
 *   3.  THE REGION NEVER EXCEEDS gamepak_backup[]'s 131,072 bytes.
 *   4.  EEPROM512 PERSISTS 8192 AND 512 IS A STRICT PREFIX OF IT.
 *   5.  THE IDENTITY IS <CODE>_<HASH8> and is derived from ROM offset 0xAC.
 *   6.  SANITISATION replaces every byte outside [A-Za-z0-9] and REPORTS it,
 *       and a code that sanitises to "____" is still ACCEPTED.
 *   7.  THE IDENTITY HASH IS BOUNDED BY THE BLOCK SIZE, not by the resident
 *       byte count -- the bug that would read past gamepak_buffers[0].
 *   8.  THE FILENAMES are exactly /savedata0/gba/<ID>.sav and .hdr, and a
 *       buffer too small REFUSES rather than truncating.
 *   9.  THE HEADER IS BYTE-EXACT, little-endian, at fixed offsets.
 *   10. THE DIRTY MODEL uses the FULL ARRAY, so a pure RECLASSIFICATION with
 *       no byte change is CLEAN. This is the property a region-sized baseline
 *       would get wrong.
 *   11. THE BYTE TAP is bounded and returns the real bytes.
 *   12. THE ADAPTER IS PASSIVE -- it modifies no gpSP state and no save byte.
 *   13. THE LATCH IS IDEMPOTENT BY REFUSAL.
 *   14. THE FNV AGREES with an independent implementation, which is what lets
 *       gba_savefile.c carry its own copy across the type boundary.
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

/* ---- neutralise gpsp/common.h ------------------------------------------- */
#define COMMON_H

/* ---- the types gpSP would have supplied ---------------------------------
 * NOTE u64 IS `unsigned long long` HERE, matching gpsp/common.h:100 and NOT
 * runtime/core.h:32. That single line is the whole reason M12B's adapter is
 * two translation units, and it is why this harness can never include
 * gba_savefile.c. */
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

/* ---- the gpSP globals the adapter reads --------------------------------- */
u32  gamepak_size         = 0;
u32  gamepak_buffer_count = 0;
u32  backup_type          = BACKUP_UNKN;
u32  flash_bank_cnt       = FLASH_SIZE_64KB;
u32  eeprom_size          = EEPROM_512_BYTE;
u8   gamepak_backup[1024 * 128];

u8  *gamepak_buffers[32];
const unsigned gamepak_buffer_blocksize = 1024 * 1024;

/* ---- THE PRODUCTION ADAPTER, INCLUDED VERBATIM -------------------------- */
#include "gba_save.c"

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

static void ck_u(u32 got, u32 want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s -- got %u, want %u\n", what, got, want);
    }
}

static void ck_s(const char *got, const char *want, const char *what)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL: %s -- got '%s', want '%s'\n", what, got, want);
    }
}

/* AN INDEPENDENT FNV-1a, written from the specification rather than copied
 * from the adapter. Test 14 compares the two; if they ever disagree, the
 * figure printed on the console would not be reproducible on a PC, and
 * gba_savefile.c's own copy -- which cannot share the adapter's static one
 * across the type boundary -- would silently write a wrong .hdr. */
static u32 ref_fnv(const u8 *p, u32 n)
{
    u32 h = 2166136261u;
    u32 i;
    for (i = 0; i < n; i++) {
        h ^= (u32)p[i];
        h *= 16777619u;
    }
    return h;
}

/* A fake 2 MB cartridge. DELIBERATELY LARGER THAN ONE BUFFER so test 7 can
 * prove the identity hash stops at the block size: if the adapter hashed
 * `gamepak_buffer_count * blocksize` instead, it would read the second
 * megabyte -- which is left as zeros here -- and produce a different hash. */
static u8 rom_image[2 * 1024 * 1024];

/* RESET EVERY PIECE OF MODULE STATE.
 *
 * The adapter is deliberately idempotent-by-refusal -- gba_save_latch()
 * returns immediately if it has already run, so that latching twice cannot
 * absorb whatever happened in between into the baseline. That is correct for
 * production and means a harness testing more than one cartridge MUST clear
 * the state itself. It can, because #include'ing the .c puts these statics in
 * this translation unit. */
static void reset_module(void)
{
    u32 i;
    gs_latched      = 0;
    for (i = 0; i < GBA_SAVE_ID_MAX; i++) gs_id[i] = '\0';
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
}

/* A clean, plausible cartridge with a four-byte game code at 0xAC. */
static void setup_cart(u32 btype, u32 bankcnt, u32 eepsize, const char *code4)
{
    u32 i;

    reset_module();

    /* Deterministic, non-trivial ROM content so the identity hash is a real
     * function of the bytes rather than of a run of zeros. */
    for (i = 0; i < 1024u * 1024u; i++)
        rom_image[i] = (u8)((i * 31u + 7u) & 0xFFu);
    /* The SECOND megabyte stays zero -- see the note on rom_image. */
    memset(rom_image + 1024 * 1024, 0, 1024 * 1024);

    memcpy(rom_image + 0xA0, "TESTCART", 8);
    memcpy(rom_image + 0xAC, code4, 4);

    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));

    gamepak_buffers[0]   = rom_image;
    gamepak_buffer_count = 1;
    gamepak_size         = 1024 * 1024;

    backup_type    = btype;
    flash_bank_cnt = bankcnt;
    eeprom_size    = eepsize;
}

/* ---- 1: THE (class, declared, region) MATRIX -- THE UNIT-COUNT TRAP ----- */
static void test_class_and_size(void)
{
    printf("test 1: the (class, declared, region) matrix -- THE UNIT-COUNT TRAP\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_class(),          GBA_SAVE_SRAM, "SRAM class");
    ck_u(gba_save_declared_bytes(), 32768u,        "SRAM declares 32768");
    ck_u(gba_save_region_bytes(),   32768u,        "SRAM persists 32768");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_class(),          GBA_SAVE_FLASH64, "FLASH 64K class");
    ck_u(gba_save_declared_bytes(), 65536u,           "FLASH 64K declares 65536");
    ck_u(gba_save_region_bytes(),   65536u,           "FLASH 64K persists 65536");

    /* THE TRAP: flash_bank_cnt is 2, and 2 is not a size. */
    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_class(),          GBA_SAVE_FLASH128, "FLASH 128K class");
    ck_u(gba_save_declared_bytes(), 131072u,
         "FLASH 128K declares 131072 (NOT flash_bank_cnt == 2)");
    ck_u(gba_save_region_bytes(),   131072u, "FLASH 128K persists 131072");

    /* THE TRAP AGAIN: eeprom_size is 16, and 16 is not a size. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_8_KBYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_class(),          GBA_SAVE_EEPROM8K, "EEPROM 8K class");
    ck_u(gba_save_declared_bytes(), 8192u,
         "EEPROM 8K declares 8192 (NOT eeprom_size == 16)");
    ck_u(gba_save_region_bytes(),   8192u, "EEPROM 8K persists 8192");

    /* THE DIVERGENCE. 512 is DECLARED and 8192 is PERSISTED. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_class(),          GBA_SAVE_EEPROM512, "EEPROM 512B class");
    ck_u(gba_save_declared_bytes(), 512u,  "EEPROM 512B DECLARES 512");
    ck_u(gba_save_region_bytes(),   8192u,
         "EEPROM 512B PERSISTS 8192 -- the size policy, not a rounding");
}

/* ---- 2: UNKNOWN REFUSES PERSISTENCE ------------------------------------ */
static void test_unknown_refuses(void)
{
    unsigned char hdr[GBA_SAVE_HDR_BYTES];
    unsigned char buf[64];
    char name[GBA_SAVE_NAME_MAX];

    printf("test 2: UNKNOWN refuses to persist\n");

    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();

    ck_u(gba_save_class(), GBA_SAVE_UNKNOWN, "UNKNOWN class");
    ck_u(gba_save_declared_bytes(), 0u,
         "UNKNOWN declares 0 bytes, NOT a guessed 32768");
    ck_u(gba_save_region_bytes(), 0u, "UNKNOWN persists 0 bytes");
    ck_u(gba_save_confidence(), GBA_SAVE_CONF_UNKNOWN, "confidence is UNKNOWN");

    /* NO HEADER FOR A CLASS THAT IS NOT PERSISTED. Producing one would create a
     * commit marker for a .sav that must never be written. */
    ck_u(gba_save_header_build(hdr, sizeof hdr, 0x11223344u), 0u,
         "no .hdr is built for an UNKNOWN class");

    /* AND NO BYTES. Even asked directly, the tap yields nothing. */
    ck_u(gba_save_chunk(buf, 0u, sizeof buf), 0u,
         "the byte tap yields nothing for an UNKNOWN class");

    ck((gba_save_report() & GBA_SAVE_R_UNKNOWN) != 0,
       "the UNKNOWN observation is reported");

    /* The identity still builds -- the cartridge is real, it is only its save
     * hardware that is unidentified. Refusal is the file layer's job. */
    ck(gba_save_name_sav(name, sizeof name) != 0u,
       "an identity still exists for an UNKNOWN cartridge");
}

/* ---- 3: THE REGION IS BOUNDED BY gamepak_backup[] ---------------------- */
static void test_region_bounded(void)
{
    u32 c;

    printf("test 3: the persisted region never exceeds 131072 bytes\n");

    for (c = 0; c <= GBA_SAVE_CLASS_MAX; c++) {
        ck(gs_region_of(c) <= GBA_SAVE_BACKUP_BYTES,
           "every class's region fits inside gamepak_backup[]");
        ck(gs_declared_of(c) <= GBA_SAVE_BACKUP_BYTES,
           "every class's declared size fits inside gamepak_backup[]");
    }

    /* FLASH128 fills it exactly -- the boundary case. */
    ck_u(gs_region_of(GBA_SAVE_FLASH128), GBA_SAVE_BACKUP_BYTES,
         "FLASH 128K fills gamepak_backup[] exactly");

    /* A class value that does not exist must not invent a size. */
    ck_u(gs_region_of(99u), 0u, "an out-of-range class persists 0 bytes");
}

/* ---- 4: EEPROM512 IS A STRICT PREFIX OF EEPROM8K ----------------------- */
static void test_eeprom_prefix(void)
{
    unsigned char buf[8192];
    u32 i;
    u32 h512, h8k;

    printf("test 4: 512 B is a STRICT PREFIX of the 8192-byte EEPROM window\n");

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    /* A plausible 512-byte save: the chip's own window written, the rest of
     * the array still at the 0xFF that gpSP normalises a blank backup to. */
    for (i = 0; i < 512u; i++)
        gamepak_backup[i] = (u8)(i & 0xFFu);
    gba_save_latch();

    ck_u(gba_save_region_bytes(), 8192u, "EEPROM512 persists 8192");
    ck_u(gba_save_chunk(buf, 0u, sizeof buf), 8192u,
         "the tap yields the whole 8192-byte window");

    for (i = 0; i < 512u; i++) {
        if (buf[i] != (u8)(i & 0xFFu)) {
            ck(0, "the first 512 bytes are the chip's own data");
            break;
        }
    }
    for (i = 512u; i < 8192u; i++) {
        if (buf[i] != 0xFF) {
            ck(0, "bytes 512..8191 are still 0xFF -- never addressable on a "
                  "512-byte chip");
            break;
        }
    }
    ck(1, "the 8192-byte window is 512 bytes of data followed by 0xFF");

    /* THE DECISIVE PROPERTY: the same bytes reported as EEPROM8K produce the
     * same 8192-byte payload, so a cartridge that is later measured at 8 KB
     * needs NO format migration of a file written today. */
    h512 = gba_save_region_hash();
    eeprom_size = EEPROM_8_KBYTE;
    ck_u(gba_save_class(), GBA_SAVE_EEPROM8K, "the class moves to EEPROM 8K");
    ck_u(gba_save_region_bytes(), 8192u, "and still persists 8192");
    h8k = gba_save_region_hash();
    ck_u(h8k, h512,
         "the SAME BYTES hash identically under both EEPROM classes -- so a "
         "file written as 512 B needs NO migration when 8 KB is measured");

    /* AND IT IS REPORTED AS A DEFAULT rather than a measurement. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck((gba_save_report() & GBA_SAVE_R_EEPDEFAULT) != 0,
       "EEPROM512 reports that its size is a DEFAULT, not a measurement");
    ck_u(gba_save_confidence(), GBA_SAVE_CONF_PROVISIONAL,
         "EEPROM512 is never FINAL -- 512 B is gpSP's initialiser");
}

/* ---- 5: THE IDENTITY --------------------------------------------------- */
static void test_identity(void)
{
    char id[GBA_SAVE_ID_MAX];
    char want[GBA_SAVE_ID_MAX];
    u32 h;

    printf("test 5: the identity is <CODE>_<HASH8> from ROM offset 0xAC\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ATHE");
    gba_save_latch();

    h = gba_save_rom_hash();
    snprintf(want, sizeof want, "ATHE_%08X", h);

    gba_save_id_copy(id, sizeof id);
    ck_s(id, want, "the identity is the game code, an underscore and 8 hex");
    ck_u((u32)strlen(id), 13u, "the identity is exactly 13 characters");

    /* THE HASH IS A REAL FUNCTION OF THE ROM. A different cartridge must not
     * produce the same identity, or two games would share one save file. */
    {
        u32 h1 = gba_save_rom_hash();
        setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ATHE");
        rom_image[0x2000] ^= 0xFF;          /* one byte, deep in the ROM */
        gba_save_latch();
        ck(gba_save_rom_hash() != h1,
           "one changed ROM byte changes the identity hash");
    }

    /* A CARTRIDGE-LESS LATCH PRODUCES NO IDENTITY, and the name builders
     * refuse rather than emitting "/savedata0/gba/.sav". */
    {
        char name[GBA_SAVE_NAME_MAX];
        reset_module();
        gamepak_buffers[0]   = 0;
        gamepak_buffer_count = 0;
        gamepak_size         = 0;
        backup_type          = BACKUP_SRAM;
        gba_save_latch();
        gba_save_id_copy(id, sizeof id);
        ck_s(id, "", "no cartridge means no identity");
        ck_u(gba_save_name_sav(name, sizeof name), 0u,
             "the .sav name REFUSES when there is no identity");
        ck_s(name, "", "...and leaves the buffer empty rather than partial");
        ck((gba_save_report() & GBA_SAVE_R_NOROM) != 0, "NOROM is reported");
    }
}

/* ---- 6: SANITISATION --------------------------------------------------- */
static void test_sanitise(void)
{
    char id[GBA_SAVE_ID_MAX];
    char want[GBA_SAVE_ID_MAX];

    printf("test 6: the game code is sanitised for the filename, and reported\n");

    /* A code with a directory separator, a dot and a NUL -- each of which would
     * do something different and bad to a filename. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    rom_image[0xAC] = 'A';
    rom_image[0xAD] = '/';
    rom_image[0xAE] = '.';
    rom_image[0xAF] = 0x00;
    gba_save_latch();

    /* 'A' survives; '/', '.' and NUL each become '_'; then the separating '_'
     * and the eight hex digits. */
    snprintf(want, sizeof want, "A____%08X", gba_save_rom_hash());

    gba_save_id_copy(id, sizeof id);
    ck_s(id, want, "'/', '.' and NUL each become '_'");
    ck((gba_save_report() & GBA_SAVE_R_SANITISED) != 0,
       "the substitution is REPORTED rather than silent");

    /* A CODE THAT SANITISES TO "____" IS STILL ACCEPTED. The identity's
     * uniqueness comes from the 32-bit hash, not from the code, so refusing
     * would deny a save to a legitimate homebrew cartridge for no safety gain
     * whatever. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    rom_image[0xAC] = 0x01;
    rom_image[0xAD] = 0x02;
    rom_image[0xAE] = 0x03;
    rom_image[0xAF] = 0x04;
    gba_save_latch();

    snprintf(want, sizeof want, "_____%08X", gba_save_rom_hash());
    gba_save_id_copy(id, sizeof id);
    ck_s(id, want, "a wholly unprintable code sanitises to '____'");
    {
        char name[GBA_SAVE_NAME_MAX];
        ck(gba_save_name_sav(name, sizeof name) != 0u,
           "...and is STILL ACCEPTED -- the hash carries the uniqueness");
    }

    /* An ordinary alphanumeric code is left completely alone. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "BPRE");
    gba_save_latch();
    snprintf(want, sizeof want, "BPRE_%08X", gba_save_rom_hash());
    gba_save_id_copy(id, sizeof id);
    ck_s(id, want, "an alphanumeric code passes through untouched");
    ck((gba_save_report() & GBA_SAVE_R_SANITISED) == 0,
       "...and does NOT report a substitution");
}

/* ---- 7: THE HASH IS BOUNDED BY THE BLOCK SIZE -------------------------- *
 *
 * THE BUG THIS PINS DOWN. gamepak_buffers[0] is ONE 1 MB allocation, but
 * rom_resident_bytes() reports gamepak_buffer_count * 1 MB -- 2 MB under this
 * build's -DROM_BUFFER_SIZE=2. An identity hash bounded by the RESIDENT count
 * while indexing buffer 0 would read a megabyte past the end of the block.
 *
 * Here the second megabyte of rom_image is deliberately ZEROS while the first
 * is a non-trivial pattern, so an over-read produces a DIFFERENT HASH and is
 * caught rather than merely being undefined behaviour. */
static void test_hash_bounded(void)
{
    u32 want;

    printf("test 7: the identity hash stops at the BLOCK SIZE, not at the "
           "resident byte count\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gamepak_buffer_count = 2;                 /* two buffers are resident   */
    gamepak_size         = 2 * 1024 * 1024;   /* the cartridge is 2 MB      */
    gba_save_latch();

    ck_u(gba_save_read(GBA_SAVE_RD_HASHED), 1024u * 1024u,
         "exactly one block was hashed, NOT two");

    want = ref_fnv(rom_image, 1024u * 1024u);
    ck_u(gba_save_rom_hash(), want,
         "the hash covers precisely the first buffer");

    /* A cartridge SMALLER than one block is bounded by its own size instead. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gamepak_size = 32u * 1024u;
    gba_save_latch();
    ck_u(gba_save_read(GBA_SAVE_RD_HASHED), 32u * 1024u,
         "a sub-block cartridge is bounded by gamepak_size");
    ck_u(gba_save_rom_hash(), ref_fnv(rom_image, 32u * 1024u),
         "...and hashes exactly that much");
}

/* ---- 8: THE FILENAMES -------------------------------------------------- */
static void test_names(void)
{
    char sav[GBA_SAVE_NAME_MAX];
    char hdr[GBA_SAVE_NAME_MAX];
    char id[GBA_SAVE_ID_MAX];
    char want[GBA_SAVE_NAME_MAX];
    char small[8];

    printf("test 8: the filenames are built exactly, and REFUSE when short\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ATHE");
    gba_save_latch();
    gba_save_id_copy(id, sizeof id);

    snprintf(want, sizeof want, "/savedata0/gba/%s.sav", id);
    ck_u(gba_save_name_sav(sav, sizeof sav), (u32)strlen(want),
         "the .sav name reports its own length");
    ck_s(sav, want, "the .sav name is /savedata0/gba/<ID>.sav");

    snprintf(want, sizeof want, "/savedata0/gba/%s.hdr", id);
    ck_u(gba_save_name_hdr(hdr, sizeof hdr), (u32)strlen(want),
         "the .hdr name reports its own length");
    ck_s(hdr, want, "the .hdr name is /savedata0/gba/<ID>.hdr");

    /* GBA_SAVE_NAME_MAX must actually be big enough -- if it were not, every
     * name would refuse and nothing would ever be written. */
    ck((u32)strlen(want) < GBA_SAVE_NAME_MAX,
       "GBA_SAVE_NAME_MAX is large enough for a real name");

    /* ***** REFUSES RATHER THAN TRUNCATES. A truncated name is a DIFFERENT
     * FILE, and writing a save to it would look like success while silently
     * colliding with every other cartridge whose name truncated the same
     * way. */
    ck_u(gba_save_name_sav(small, sizeof small), 0u,
         "a buffer too small REFUSES");
    ck_s(small, "", "...and leaves the buffer empty rather than truncated");
}

/* ---- 9: THE HEADER IS BYTE-EXACT --------------------------------------- */
static void test_header_bytes(void)
{
    unsigned char got[GBA_SAVE_HDR_BYTES];
    unsigned char want[GBA_SAVE_HDR_BYTES];
    unsigned char small[GBA_SAVE_HDR_BYTES - 1];
    u32 rh;
    u32 i;
    const u32 payfnv = 0xDEADBEEFu;

    printf("test 9: the 32-byte .hdr is byte-exact, little-endian\n");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    rh = gba_save_rom_hash();

    ck_u(gba_save_header_build(got, sizeof got, payfnv), GBA_SAVE_HDR_BYTES,
         "the header builder reports 32 bytes");

    /* HAND-WRITTEN EXPECTATION. Every offset and every byte order is spelled
     * out here rather than computed with the same helpers the adapter uses, so
     * a change to gs_put16/gs_put32 cannot silently change both sides. */
    memset(want, 0, sizeof want);
    want[0]  = 'L'; want[1] = 'G'; want[2] = 'S'; want[3] = '1';
    want[4]  = 1;  want[5]  = 0;                       /* version 1          */
    want[6]  = (unsigned char)GBA_SAVE_FLASH64;
    want[7]  = 0;                                      /* class 2            */
    want[8]  = 0x00; want[9] = 0x00; want[10] = 0x01; want[11] = 0x00;
                                                       /* region  65536      */
    want[12] = 0x00; want[13] = 0x00; want[14] = 0x01; want[15] = 0x00;
                                                       /* declared 65536     */
    want[16] = (unsigned char)( rh        & 0xFF);
    want[17] = (unsigned char)((rh >>  8) & 0xFF);
    want[18] = (unsigned char)((rh >> 16) & 0xFF);
    want[19] = (unsigned char)((rh >> 24) & 0xFF);
    want[20] = 0xEF; want[21] = 0xBE; want[22] = 0xAD; want[23] = 0xDE;
                                                       /* payload fnv LE     */
    want[24] = (unsigned char)GBA_SAVE_CONF_PROVISIONAL;
    want[25] = 0;
    /* 26..31 stay zero -- the reserved field */

    for (i = 0; i < GBA_SAVE_HDR_BYTES; i++) {
        if (got[i] != want[i]) {
            printf("  FAIL: header byte %u is 0x%02X, expected 0x%02X\n",
                   i, got[i], want[i]);
            checks++;
            failures++;
            return;
        }
    }
    ck(1, "all 32 header bytes match the hand-written layout exactly");

    /* THE RESERVED FIELD IS ZERO, and stays that way -- M12C compares headers
     * for equality, so a filled reserved field would have to be parsed. */
    for (i = GBA_SAVE_HDR_O_RESERVED; i < GBA_SAVE_HDR_BYTES; i++)
        if (got[i] != 0) { ck(0, "the reserved field is zero"); return; }
    ck(1, "the reserved field is entirely zero");

    /* A BUFFER TOO SMALL REFUSES rather than writing a partial header. */
    ck_u(gba_save_header_build(small, sizeof small, payfnv), 0u,
         "a buffer smaller than 32 bytes REFUSES");

    /* DETERMINISM. The same state must produce the same 32 bytes -- there is
     * no timestamp and no counter, which is what lets M12C compare a stored
     * header against a freshly computed one. */
    {
        unsigned char again[GBA_SAVE_HDR_BYTES];
        gba_save_header_build(again, sizeof again, payfnv);
        ck(memcmp(got, again, GBA_SAVE_HDR_BYTES) == 0,
           "the header is DETERMINISTIC -- no timestamp, no counter");
    }
}

/* ---- 10: THE DIRTY MODEL USES THE FULL ARRAY --------------------------- *
 *
 * THE DEFECT THIS PINS DOWN. If the baseline were hashed over the PERSISTED
 * REGION rather than the whole array, then a cartridge that reclassified
 * mid-run -- UNKNOWN (0 bytes) to EEPROM512 (8192), which happens the first
 * time write_eeprom() runs -- would compare a hash of one range against a hash
 * of a different range and report DIRTY for a run in which NOT ONE BYTE
 * CHANGED. M12B would then commit a save the game never made. */
static void test_dirty_model(void)
{
    printf("test 10: the dirty decision hashes the FULL array, so a pure "
           "reclassification is CLEAN\n");

    /* A. Nothing happens at all -> CLEAN. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    gba_save_finish();
    ck_u(gba_save_is_dirty(), 0u, "an untouched region is CLEAN");
    ck((gba_save_report() & GBA_SAVE_R_CLEAN) != 0, "...and reports CLEAN");
    ck_u(gba_save_baseline_hash(), gba_save_exit_hash(),
         "the two endpoint hashes agree");

    /* B. One byte changes -> DIRTY. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    gamepak_backup[1234] = 0x5A;
    gba_save_finish();
    ck_u(gba_save_is_dirty(), 1u, "one changed byte is DIRTY");
    ck((gba_save_report() & GBA_SAVE_R_CLEAN) == 0, "...and does not report CLEAN");

    /* C. ***** THE DECISIVE CASE. ***** Latch UNKNOWN, then let the class
     *    resolve to EEPROM512 with NO byte change. The region grows from 0 to
     *    8192, and the answer must still be CLEAN. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_latch_class(), GBA_SAVE_UNKNOWN, "latched UNKNOWN");
    ck_u(gba_save_read(GBA_SAVE_RD_LATCH_REGION), 0u, "the latched region is 0");

    backup_type = BACKUP_EEPROM;      /* write_eeprom() at gba_memory.c:542  */
    gba_save_finish();

    ck_u(gba_save_class(), GBA_SAVE_EEPROM512, "the class resolved to EEPROM512");
    ck_u(gba_save_region_bytes(), 8192u, "the region grew from 0 to 8192");
    ck((gba_save_report() & GBA_SAVE_R_GREW) != 0, "the growth is reported");
    ck((gba_save_report() & GBA_SAVE_R_CLASSMOVED) != 0,
       "the class move is reported");
    ck_u(gba_save_is_dirty(), 0u,
         "***** a pure RECLASSIFICATION with no byte change is CLEAN *****");

    /* D. Reclassification AND a real write -> DIRTY. The fix must not make
     *    every reclassification clean regardless of the bytes. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    backup_type = BACKUP_EEPROM;
    gamepak_backup[0] = 0x5A;
    gba_save_finish();
    ck_u(gba_save_is_dirty(), 1u,
         "a reclassification WITH a real write is still DIRTY");

    /* E. A CHANGE THAT REVERTS IS CLEAN, and that is correct here even though
     *    M11's sticky evidence would call it dirty. The question M12B asks is
     *    "do the bytes on disk need updating", and for a byte written and
     *    written back the answer is no. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    gamepak_backup[77] = 0x11;
    gba_save_sample();
    gamepak_backup[77] = 0xFF;
    gba_save_finish();
    ck_u(gba_save_is_dirty(), 0u,
         "a byte written and written back needs no commit");
    ck_u(gba_save_samples(), 1u, "the sampler counted one sample");
    ck_u(gba_save_active_samples(), 1u,
         "...and saw the transient, which is DIAGNOSTIC ONLY");

    /* F. is_dirty() IS SELF-SUFFICIENT. A fixture that forgot to call
     *    finish() must not get a bogus answer from a zero exit hash. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    ck_u(gba_save_is_dirty(), 0u,
         "is_dirty() takes the exit hash itself when finish() was not called");
    ck_u(gba_save_exit_hash(), gba_save_baseline_hash(),
         "...and the hash it took is real");

    /* G. NO LATCH MEANS NOT DIRTY. Refusing to write is the safe direction:
     *    without a baseline there is no evidence the game saved. */
    reset_module();
    ck_u(gba_save_is_dirty(), 0u, "an unlatched adapter is never DIRTY");
    ck((gba_save_report() & GBA_SAVE_R_NOLATCH) != 0, "NOLATCH is reported");
}

/* ---- 11: THE BYTE TAP -------------------------------------------------- */
static void test_chunk(void)
{
    unsigned char buf[4096];
    u32 i, off, total, chunks;

    printf("test 11: the byte tap is bounded and returns the real bytes\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    for (i = 0; i < GBA_SAVE_BACKUP_BYTES; i++)
        gamepak_backup[i] = (u8)((i * 13u + 5u) & 0xFFu);
    gba_save_latch();

    ck_u(gba_save_region_bytes(), 32768u, "SRAM persists 32768");

    /* Streaming in 4 KB chunks, exactly as gba_savefile.c does, must yield the
     * region size and nothing more. */
    off = 0; total = 0; chunks = 0;
    while (off < 32768u) {
        u32 got = gba_save_chunk(buf, off, sizeof buf);
        if (got == 0) break;
        chunks++;
        total += got;
        off   += got;
    }
    ck_u(total, 32768u, "streaming 4 KB at a time yields exactly the region");
    ck_u(chunks, 8u, "32768 bytes is exactly eight 4096-byte chunks");
    ck_u(gba_save_chunk(buf, 32768u, sizeof buf), 0u,
         "asking past the region yields 0 -- the loop terminates");
    ck_u(gba_save_chunk(buf, 40000u, sizeof buf), 0u,
         "asking well past the region yields 0");

    /* The bytes are the real ones, in order. */
    ck_u(gba_save_chunk(buf, 100u, 16u), 16u, "a short read is honoured");
    for (i = 0; i < 16u; i++) {
        if (buf[i] != (u8)(((100u + i) * 13u + 5u) & 0xFFu)) {
            ck(0, "the tap returns the bytes at the requested offset");
            return;
        }
    }
    ck(1, "the tap returns the real bytes at the requested offset");

    /* A request straddling the end is CLAMPED, not refused. */
    ck_u(gba_save_chunk(buf, 32760u, 4096u), 8u,
         "a request straddling the region end is clamped to 8 bytes");

    /* A null destination and a zero length are handled. */
    ck_u(gba_save_chunk(0, 0u, 16u), 0u, "a NULL destination yields 0");
    ck_u(gba_save_chunk(buf, 0u, 0u), 0u, "a zero length yields 0");
}

/* ---- 12: PASSIVITY ----------------------------------------------------- */
static void test_passivity(void)
{
    static u8 before[sizeof(gamepak_backup)];
    static u8 rom_before[sizeof(rom_image)];
    u32 bt, bc, es, gs_size, gbc;
    unsigned char buf[256];
    unsigned char hdr[GBA_SAVE_HDR_BYTES];
    char name[GBA_SAVE_NAME_MAX];
    char id[GBA_SAVE_ID_MAX];
    int i;

    printf("test 12: PASSIVITY -- the adapter changes nothing\n");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE, "ABCD");

    for (i = 0; i < (int)sizeof(gamepak_backup); i++)
        gamepak_backup[i] = (u8)(i * 7 + 3);

    memcpy(before, gamepak_backup, sizeof(gamepak_backup));
    memcpy(rom_before, rom_image, sizeof(rom_image));

    bt = backup_type; bc = flash_bank_cnt; es = eeprom_size;
    gs_size = gamepak_size; gbc = gamepak_buffer_count;

    gba_save_latch();
    for (i = 0; i < 64; i++) gba_save_sample();
    gba_save_finish();
    (void)gba_save_class();
    (void)gba_save_latch_class();
    (void)gba_save_declared_bytes();
    (void)gba_save_region_bytes();
    (void)gba_save_confidence();
    (void)gba_save_rom_hash();
    (void)gba_save_region_hash();
    (void)gba_save_nonff();
    (void)gba_save_is_dirty();
    (void)gba_save_report();
    (void)gba_save_chunk(buf, 0u, sizeof buf);
    (void)gba_save_header_build(hdr, sizeof hdr, 0u);
    gba_save_id_copy(id, sizeof id);
    (void)gba_save_name_sav(name, sizeof name);
    (void)gba_save_name_hdr(name, sizeof name);
    for (i = 0; i <= (int)GBA_SAVE_RD_HASHED; i++)
        (void)gba_save_read((unsigned)i);

    ck(memcmp(before, gamepak_backup, sizeof(gamepak_backup)) == 0,
       "not one byte of gamepak_backup[] was modified");
    ck(memcmp(rom_before, rom_image, sizeof(rom_image)) == 0,
       "not one byte of the cartridge was modified");
    ck_u(backup_type,          bt,      "backup_type was not modified");
    ck_u(flash_bank_cnt,       bc,      "flash_bank_cnt was not modified");
    ck_u(eeprom_size,          es,      "eeprom_size was not modified");
    ck_u(gamepak_size,         gs_size, "gamepak_size was not modified");
    ck_u(gamepak_buffer_count, gbc,     "gamepak_buffer_count was not modified");
}

/* ---- 13: THE LATCH IS IDEMPOTENT BY REFUSAL ---------------------------- */
static void test_latch_idempotent(void)
{
    u32 h, id_hash;
    char id1[GBA_SAVE_ID_MAX], id2[GBA_SAVE_ID_MAX];

    printf("test 13: the latch refuses to run twice\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    gba_save_latch();
    h       = gba_save_baseline_hash();
    id_hash = gba_save_rom_hash();
    gba_save_id_copy(id1, sizeof id1);

    /* Change everything a second latch would absorb, then latch again. */
    gamepak_backup[500] = 0x11;
    rom_image[0xAC]     = 'Z';
    backup_type         = BACKUP_FLASH;
    gba_save_latch();

    ck_u(gba_save_baseline_hash(), h,
         "***** a second latch does NOT absorb the intervening writes *****");
    ck_u(gba_save_rom_hash(), id_hash, "the identity hash is unchanged");
    gba_save_id_copy(id2, sizeof id2);
    ck_s(id2, id1, "the identity string is unchanged");
    ck_u(gba_save_latch_class(), GBA_SAVE_SRAM,
         "the latched class is still the first one");

    /* And the change IS still visible as dirt, which is the whole point. */
    gba_save_finish();
    ck_u(gba_save_is_dirty(), 1u,
         "the intervening write is still correctly seen as DIRTY");
}

/* ---- 14: THE FNV AGREES WITH AN INDEPENDENT IMPLEMENTATION ------------- */
static void test_fnv_agreement(void)
{
    static const u8 v[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF, 'L', 'G', 'S' };

    printf("test 14: the adapter's FNV matches an independent implementation\n");

    ck_u(gs_fnv(v, (u32)sizeof v), ref_fnv(v, (u32)sizeof v),
         "the adapter's FNV-1a agrees byte for byte");

    /* THE SEED AND THE PRIME ARE THE PROJECT'S. A different seed would still
     * be self-consistent and would silently stop matching tools/upload.py and
     * every figure M4-M11 have already printed. */
    ck_u(gs_fnv(v, 0u), 2166136261u, "the empty hash is the FNV offset basis");

    /* The region hash is the same function over the same range, which is what
     * lets gba_savefile.c accumulate its own copy while writing and compare. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE, "ABCD");
    {
        u32 i;
        for (i = 0; i < 32768u; i++)
            gamepak_backup[i] = (u8)((i * 3u + 1u) & 0xFFu);
    }
    gba_save_latch();
    ck_u(gba_save_region_hash(), ref_fnv(gamepak_backup, 32768u),
         "the region hash is FNV-1a over exactly the persisted region");
}

int main(void)
{
    printf("==== gba_save offline harness (M12B) ====\n");
    printf("Testing the SHIPPING adapter, included verbatim.\n\n");

    test_class_and_size();
    test_unknown_refuses();
    test_region_bounded();
    test_eeprom_prefix();
    test_identity();
    test_sanitise();
    test_hash_bounded();
    test_names();
    test_header_bytes();
    test_dirty_model();
    test_chunk();
    test_passivity();
    test_latch_idempotent();
    test_fnv_agreement();

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M12B EQUIV FAILED\n");
        return 1;
    }
    printf("M12B EQUIV OK -- the adapter classifies, sizes, identifies, "
           "bounds, hashes and REFUSES correctly, and stays PASSIVE\n");
    return 0;
}
