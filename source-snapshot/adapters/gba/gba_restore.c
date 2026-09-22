/* LUAport M12C -- adapters/gba/gba_restore.c
 *
 * The gpSP-facing half of GBA cartridge save restore.
 * adapters/gba/gba_restore.h carries the full derivation -- why raw bytes are
 * sufficient, why seeding eeprom_size would be actively harmful, why the
 * rollback wipes to 0xFF, and why M12C owns its own baseline. READ THE HEADER
 * FIRST.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT open, read, close or name a file, and names no mount point.
 *     Everything filesystem-shaped is in gba_restorefile.c. There is no way for
 *     this file to load anything by itself; the bytes are pushed in by a caller
 *     that has already validated them.
 *   - does NOT include anything from runtime/. It includes gpsp/common.h, and
 *     runtime/core.h's `typedef unsigned long u64` would collide with
 *     gpsp/common.h:100's `unsigned long long`.
 *   - does NOT call read_backup(), write_backup(), read_eeprom() or
 *     write_eeprom(). Those four MUTATE backup_type (gba_memory.c:464-465,
 *     :1140-1141, :542): calling one to "check the save type" would MANUFACTURE
 *     the class it then reported, and M12C would go on to accept a header for a
 *     class the cartridge never had. m12c-verify asserts the absence of all four
 *     from gba_restore.o.
 *   - does NOT write backup_type_reset, eeprom_size, flash_bank_cnt,
 *     flash_bank_num, flash_mode, flash_command_position, eeprom_mode,
 *     eeprom_address or eeprom_counter -- IN ANY CONFIGURATION. gpSP re-derives
 *     every one of them on its own reset path and re-measures the two that are
 *     measurable from the game's own bus traffic.
 *
 *     backup_type IS THE ONE EXCEPTION, AND ONLY UNDER LUAPORT_SESSION_REUSE.
 *     gba_restore_flash64(1) -- M16-2, compiled out of every frozen milestone --
 *     stores BACKUP_FLASH there and NOTHING ELSE. In M12C, M12B, M11 and M13C
 *     this file still reads three classification globals and assigns NONE, which
 *     is what m12c-verify measures. See the derivation at that function.
 *   - does NOT call load_gamepak_page(). It touches gamepak_backup[] only,
 *     which is a plain static array and is never demand paged.
 *   - does NOT allocate. All state below is fixed-size .bss and totals well
 *     under 64 bytes. THERE IS NO 128 KB STAGING BUFFER ANYWHERE IN M12C --
 *     see the two-pass design in gba_restorefile.h.
 *
 * THE gpsp TREE IS NOT MODIFIED. Every symbol used below is already declared in
 * a gpSP header.
 */

#include "common.h"                  /* u8/u32, gamepak_backup, backup_type,
                                        flash_bank_cnt, eeprom_size and the
                                        BACKUP_* / FLASH_* / EEPROM_* constants */

#include "gba_restore.h"

/* ------------------------------------------------------------ module state --
 *
 * All .bss, all fixed size, under 64 bytes in total. THERE IS NO COPY OF THE
 * SAVE HERE: bytes arrive from gba_restorefile.c 4096 at a time and go straight
 * into gamepak_backup[]. */
static u32 gr_armed     = 0;
static u32 gr_applied   = 0;
static u32 gr_rollbacks = 0;

static u32 gr_base_kind = GBA_RESTORE_BASE_NONE;
static u32 gr_base_hash = 0;
static u32 gr_exit_hash = 0;
static u32 gr_exit_taken = 0;

/* --------------------------------------------------------------- FNV-1a 32 --
 *
 * THE SAME SEED AND PRIME AS EVERYWHERE ELSE IN THIS PROJECT --
 * adapters/gba/gba_save.c:103-104, adapters/gba/gba_savehdr.c,
 * adapters/gba/gba_rom.c:488 and tools/upload.py all use 2166136261 / 16777619.
 * That is what lets the figure this file computes from the ARRAY be compared
 * directly against the one gba_restorefile.c computed from the FILE, and
 * against the one M12B stored in the .hdr months earlier.
 *
 * RESTATED RATHER THAN SHARED, for gba_savefile.c:48-63's reason: the other
 * implementations are `static` behind a type boundary this file is on the wrong
 * side of, and exporting one would widen a contract deliberately limited to
 * scalars and byte pointers. tools/gba_restore_equiv.c asserts all of them
 * agree on the same input. */
#define GR_FNV_OFFSET 2166136261u
#define GR_FNV_PRIME    16777619u

static u32 gr_fnv(const u8 *p, u32 n)
{
    u32 h = GR_FNV_OFFSET;
    u32 i;

    for (i = 0; i < n; i++) {
        h ^= (u32)p[i];
        h *= GR_FNV_PRIME;
    }

    return h;
}

/* The whole backup array -- the quantity both baselines use. See THE BASELINE
 * in the header for why this is the full array and not the persisted region. */
static u32 gr_full_hash(void)
{
    return gr_fnv(gamepak_backup, GBA_RESTORE_BACKUP_BYTES);
}

/* ------------------------------------------------------------ the live class --
 *
 * A TRANSCRIPTION of the same resolution gba_save.c:134-148 and
 * gba_m11save.c:165-179 perform, and PURELY READ-ONLY. backup_type ALONE IS NOT
 * AN ANSWER: BACKUP_FLASH covers 64 KB and 128 KB and BACKUP_EEPROM covers
 * 512 B and 8 KB, so each is resolved against its companion UNIT COUNT.
 *
 * NOTE THE UNIT COUNTS ARE NOT BYTE COUNTS. gpsp/gba_memory.h:294-298 defines
 * EEPROM_8_KBYTE as 16 and FLASH_SIZE_128KB as 2. Nothing below echoes either
 * as a size; they are compared, never returned. */
unsigned int gba_restore_live_class(void)
{
    if (backup_type == BACKUP_SRAM)
        return GBA_RESTORE_CLS_SRAM;

    if (backup_type == BACKUP_FLASH)
        return (flash_bank_cnt == FLASH_SIZE_128KB) ? GBA_RESTORE_CLS_FLASH128
                                                    : GBA_RESTORE_CLS_FLASH64;

    if (backup_type == BACKUP_EEPROM)
        return (eeprom_size == EEPROM_8_KBYTE) ? GBA_RESTORE_CLS_EEPROM8K
                                               : GBA_RESTORE_CLS_EEPROM512;

    return GBA_RESTORE_CLS_UNKNOWN;   /* BACKUP_UNKN, and anything else */
}

/* ------------------------------------------- THE PRE-BOOT DATABASE EVIDENCE --
 *
 * ***** READ-ONLY, AND IT REPORTS EVIDENCE RATHER THAN A CLASS. *****
 *
 * It answers ONE question: has gpSP's OWN cartridge database asserted a 128 KB
 * FLASH part while leaving the save TYPE unset? That is a real, narrow state
 * with exactly one producer, and it is the missing fact that made a FLASH128
 * save unrestorable on a cartridge whose flash the database already knew about.
 *
 * WHY THE CONJUNCTION IS A PROOF AND NOT A GUESS. Enumerate every writer of
 * flash_bank_cnt that can run BEFORE the first instruction executes:
 *
 *   gba_memory.c:3002   the default, 1                    -- not 2
 *   gba_memory.c:1727   gba_over.h FLAGS_FLASH_128KB, 2   -- LEAVES THE TYPE UNSET
 *   detect_backup_subcircuit() FLASH1M_V signature, 2     -- ALSO sets FLASH
 *   the Pokemon short-circuit, 2                          -- ALSO sets FLASH
 *   the is_hack override, 2                               -- ALSO sets FLASH
 *
 * Every route that reaches 2 ALSO sets backup_type_reset to BACKUP_FLASH --
 * except the gba_over.h one, which sets flash_bank_cnt and the Sanyo device id
 * and THEN FALLS THROUGH WITHOUT A TYPE (only FLAGS_EEPROM sets one, at :1740).
 * So `UNKN && bank_cnt == 128KB` at the restore point is REACHABLE FROM EXACTLY
 * ONE PLACE, and that place is gpSP's own database asserting a flash cartridge.
 * adapters/gba/gba_m11save.h:90-95 recorded this same gpSP defect during M11.
 *
 * ***** IT IS NOT A DETECTION AND MUST NEVER BE REPORTED AS ONE. ***** The
 * cartridge's live class is still UNKNOWN after this returns 1, and stays that
 * way until the game's own bus traffic classifies it. Nothing here writes
 * backup_type, backup_type_reset, flash_bank_cnt or eeprom_size -- this function
 * is two comparisons over two globals it only reads, exactly like
 * gba_restore_live_class() above. m12c-verify's "no classification global is
 * written" gate and tools/gba_restore_equiv.c's NOT-seeded assertions both
 * continue to hold unchanged.
 *
 * THE UNIT COUNT IS COMPARED, NEVER RETURNED. FLASH_SIZE_128KB is 2, not
 * 131072 (gpsp/gba_memory.h:294-298); this returns a 0/1 FLAG so the unit-count
 * trap documented in gba_savehdr.c:84-95 cannot be sprung through it. */
unsigned int gba_restore_db_flash128(void)
{
    return (backup_type == BACKUP_UNKN &&
            flash_bank_cnt == FLASH_SIZE_128KB) ? 1u : 0u;
}

#ifdef LUAPORT_SESSION_REUSE
/* ------------------- M16-2: THE FLASH64 ELIGIBILITY, AND THE ACTIVATION -----
 *
 * ***** COMPILED ONLY FOR M16 TARGETS. ***** Absent from M13C's preprocessed
 * source, so gba_restore.o is unchanged for every frozen milestone -- and the
 * "this file assigns NO classification global" property those milestones are
 * measured against remains literally true for them.
 *
 * ##### WHAT THE OBSERVATION IS, AND WHAT IT IS NOT #####
 *
 *     backup_type == BACKUP_UNKN  &&  flash_bank_cnt == FLASH_SIZE_64KB
 *
 * IS NOT EVIDENCE THAT THE CARTRIDGE IS A 64 KB FLASH PART. It is ALSO gpSP's
 * DEFAULT state for any unclassified cartridge -- init_memory() leaves exactly
 * these two values, so an EEPROM cartridge, an SRAM cartridge and a genuine
 * 64 KB flash cartridge are all in it before the first instruction runs. Read
 * alone it says NOTHING about the hardware.
 *
 * WHAT IT PROVES IS ELIGIBILITY: gpSP has not already classified this cartridge
 * as something ELSE, so activating cannot contradict an observation the emulator
 * actually made. THE AUTHORITY FOR THE CLASS IS THE VALIDATED ROM-BOUND LGS1
 * FLASH64 HEADER, proved in full by gba_restorefile.c before `activate` is ever
 * passed as 1. Contrast gba_restore_db_flash128() above, which IS a genuine
 * database assertion with exactly one producer; there is no 64 KB counterpart
 * because FLAGS_FLASH_64KB does not exist in gba_over.h at all.
 *
 * ***** WHY OBSERVE AND ACTIVATE ARE ONE FUNCTION. ***** The activation RE-TESTS
 * the identical precondition it was authorised under. Had they been split, a
 * caller could observe eligibility, and then activate after something else had
 * classified the cartridge -- overwriting a real detection with a value derived
 * from a file. Fused, that is impossible: if the state has moved, the store does
 * not happen and 0 comes back.
 *
 * ***** IT STORES backup_type AND NOTHING ELSE. ***** NOT backup_type_reset,
 * which must stay BACKUP_UNKN so a class derived from a FILE cannot survive a
 * reset into a session gpSP never observed. NOT flash_bank_cnt -- it is ALREADY
 * at its 64 KB value, which is precisely why this cartridge was eligible, and
 * rewriting geometry to suit a header is how a "fix" passes a byte-exactness
 * test and corrupts one reset later. NOT eeprom_size, flash_bank_num,
 * flash_mode or flash_command_position. After the single store the emulator
 * holds EXACTLY the state a natively detected 64 KB flash cartridge holds after
 * init_memory(): nothing is invented, the one global gpSP failed to record is
 * supplied.
 *
 * ***** WHY IT MUST HAPPEN AT ALL. ***** Admitting the bytes without it would be
 * WORSE than refusing them. backup_type UNKN collapses to BACKUP_SRAM inside
 * read_backup() (gba_memory.c:465) on the cartridge's very first backup read --
 * permanently -- and a clean-and-modified exit would then commit through
 * gs_region_of(SRAM) = 32768: a 32 KB SRAM-classed file written over a 64 KB
 * FLASH64 save, accepted on the next launch by the existing SRAM carve-out,
 * silently destroying the upper half with every gate reporting green.
 *
 * THE UNIT COUNT IS COMPARED, NEVER RETURNED, exactly as above -- FLASH_SIZE_64KB
 * is 1, not 65536.
 *
 * Returns 1 when the state is eligible (and, when `activate` was non-zero, when
 * the store was therefore performed), 0 otherwise. `activate` = 0 is PURELY
 * READ-ONLY and is how the class gate asks the question. */
unsigned int gba_restore_flash64(int activate)
{
    if (backup_type != BACKUP_UNKN || flash_bank_cnt != FLASH_SIZE_64KB)
        return 0u;

    if (activate)
        backup_type = BACKUP_FLASH;

    return 1u;
}
#endif /* LUAPORT_SESSION_REUSE */

/* ------------------------------------------------------------------ the arm -- */

void gba_restore_arm(void)
{
    gr_armed = 1;
}

unsigned int gba_restore_armed(void)
{
    return (unsigned int)gr_armed;
}

#ifdef LUAPORT_SESSION_REUSE
/* --------------------------------------------- THE DISARM (M16 ONLY) ------
 *
 * ***** COMPILED ONLY FOR M16 TARGETS. ***** Absent from M13C's preprocessed
 * source, so gba_restore.o is unchanged for every frozen milestone.
 *
 * ***** WHAT THIS FIXES. ***** gba_restore_arm() sets gr_armed and NOTHING IN
 * THIS MODULE EVER CLEARS IT -- there is no store of 0 to gr_armed anywhere
 * else in the file. For a one-cartridge process that is harmless. For a second
 * session it breaks a stated safety invariant: the validate-only pass that runs
 * before a restore is documented as PROVABLY PASSIVE precisely because STAGE 0
 * never arms, and gba_restore_apply() refuses while disarmed (see the refusal
 * at the top of gba_restore_apply). If gr_armed is still set from session 1,
 * session 2's validate pass would run ARMED and could write gamepak_backup[]
 * during what the design guarantees is a read-only probe.
 *
 * ***** THIS RETURNS THE MODULE TO ITS PRE-ARM STATE. IT IS NOT A ROLLBACK.
 * ***** gba_restore_rollback() wipes gamepak_backup[] to 0xFF and is a failure
 * path. This function does not touch gamepak_backup[] AT ALL -- it clears only
 * this module's own fixed-size .bss. Clearing measurements that describe a save
 * that is no longer loaded is the entire point: carrying session 1's applied
 * byte count or baseline hash into session 2 would make session 2's restore
 * report a result it never produced.
 *
 * NO VALIDATION POLICY IS ALTERED. Every clamp, every refusal and every class
 * resolution in this file is untouched. */
void gba_restore_disarm(void)
{
    /* The measurements first, the arm flag last: a fault midway through then
     * leaves the module armed-but-zeroed, which the relaunch probe reports as a
     * failure, rather than disarmed-but-stale, which it would not. */
    gr_applied   = 0;
    gr_rollbacks = 0;

    gr_base_kind = GBA_RESTORE_BASE_NONE;
    gr_base_hash = 0;
    gr_exit_hash = 0;

    /* The taken flag must go with the hash it describes -- leaving it set would
     * let the next session report an exit hash it never computed. */
    gr_exit_taken = 0;

    gr_armed = 0;
}
#endif /* LUAPORT_SESSION_REUSE */

/* ---------------------------------------------------------------- the apply --
 *
 * THE ONLY FUNCTION IN M12C THAT WRITES gamepak_backup[]. */
unsigned int gba_restore_apply(const unsigned char *src, unsigned int off,
                               unsigned int n, unsigned int region)
{
    u32 limit = (u32)region;
    u32 i;

    /* ***** DISARMED MEANS DISARMED. ***** STAGE 0 never calls
     * gba_restore_arm(), so this refusal -- not a branch in the fixture -- is
     * what makes the validate-only stage provably passive. */
    if (!gr_armed)
        return 0u;

    if (!src || n == 0u)
        return 0u;

    /* CLAMP 1 OF 2: to the array. No class and no header may ever cause a write
     * past gamepak_backup[]'s 131,072 bytes, whatever the caller believes its
     * region to be. */
    if (limit > GBA_RESTORE_BACKUP_BYTES)
        limit = GBA_RESTORE_BACKUP_BYTES;

    if (limit == 0u || off >= limit)
        return 0u;

    /* CLAMP 2 OF 2: to the remaining bytes of this save. Both are needed -- the
     * first stops a write past the ARRAY, the second stops a write past the
     * SAVE. */
    if (n > limit - off)
        n = limit - off;

    for (i = 0; i < (u32)n; i++)
        gamepak_backup[(u32)off + i] = (u8)src[i];

    gr_applied += (u32)n;

    return n;
}

/* ------------------------------------------------------------- the rollback --
 *
 * WRITTEN AS AN EXPLICIT LOOP RATHER THAN memset(), so this translation unit
 * depends on nothing beyond gpsp/common.h and so the wipe is visibly the same
 * 0xFF fill that m4_rom_backup_init() (gba_rom.c:253) and
 * normalize_blank_backup_for_detected_type() (gba_memory.c:2906) perform. It
 * runs at most once per launch, on a failure path, so the loop costs nothing
 * that matters. */
void gba_restore_rollback(void)
{
    u32 i;

    for (i = 0; i < GBA_RESTORE_BACKUP_BYTES; i++)
        gamepak_backup[i] = 0xFF;

    gr_applied = 0;
    gr_rollbacks++;
}

/* ------------------------------------------------------------- the baseline -- */

void gba_restore_baseline(unsigned int kind)
{
    /* An unrecognised kind is recorded as NONE rather than stored blindly. The
     * kind is printed to the operator as the difference between "this is a
     * restored save" and "this is a new game", and a garbage value there is a
     * garbage claim about the entire run. */
    if (kind == GBA_RESTORE_BASE_NEWGAME || kind == GBA_RESTORE_BASE_RESTORED)
        gr_base_kind = (u32)kind;
    else
        gr_base_kind = GBA_RESTORE_BASE_NONE;

    gr_base_hash  = gr_full_hash();

    /* Re-taking the baseline invalidates any exit hash already measured: the
     * two must describe the same interval or "modified" is meaningless. */
    gr_exit_hash  = 0;
    gr_exit_taken = 0;
}

unsigned int gba_restore_baseline_kind(void)
{
    return (unsigned int)gr_base_kind;
}

unsigned int gba_restore_baseline_hash(void)
{
    return (unsigned int)gr_base_hash;
}

void gba_restore_finish(void)
{
    gr_exit_hash  = gr_full_hash();
    gr_exit_taken = 1;
}

unsigned int gba_restore_exit_hash(void)
{
    return (unsigned int)gr_exit_hash;
}

unsigned int gba_restore_modified(void)
{
    /* WITHOUT A BASELINE THERE IS NO CLAIM TO MAKE. Reporting "not modified"
     * would be a statement the adapter cannot support, and reporting "modified"
     * would be worse; 0 here means "no evidence", and the fixture prints the
     * baseline KIND beside it so the operator can tell the two apart. */
    if (gr_base_kind == GBA_RESTORE_BASE_NONE)
        return 0u;

    /* Self-sufficient, for gba_save.c:519-527's reason: a fixture that forgot
     * to call finish() would otherwise compare a zero against a real baseline
     * and report a modification that never happened. */
    if (!gr_exit_taken)
        gba_restore_finish();

    return (gr_exit_hash != gr_base_hash) ? 1u : 0u;
}

/* -------------------------------------------------------------- the answers -- */

unsigned int gba_restore_applied(void)
{
    return (unsigned int)gr_applied;
}

unsigned int gba_restore_region_fnv(unsigned int n)
{
    u32 c = (u32)n;

    if (c > GBA_RESTORE_BACKUP_BYTES)
        c = GBA_RESTORE_BACKUP_BYTES;

    if (c == 0u)
        return 0u;

    return (unsigned int)gr_fnv(gamepak_backup, c);
}

unsigned int gba_restore_nonff(unsigned int n)
{
    u32 c = (u32)n;
    u32 i;
    u32 k = 0;

    if (c > GBA_RESTORE_BACKUP_BYTES)
        c = GBA_RESTORE_BACKUP_BYTES;

    for (i = 0; i < c; i++)
        if (gamepak_backup[i] != 0xFF)
            k++;

    return (unsigned int)k;
}

/* ------------------------------------------------------------------ the log -- */

unsigned int gba_restore_read(unsigned int sel)
{
    switch (sel) {
        case GBA_RESTORE_RD_LIVECLASS:   return gba_restore_live_class();
        case GBA_RESTORE_RD_ARMED:       return (unsigned int)gr_armed;
        case GBA_RESTORE_RD_APPLIED:     return (unsigned int)gr_applied;
        case GBA_RESTORE_RD_BASEKIND:    return (unsigned int)gr_base_kind;
        case GBA_RESTORE_RD_BASEHASH:    return (unsigned int)gr_base_hash;
        case GBA_RESTORE_RD_EXITHASH:    return (unsigned int)gr_exit_hash;
        case GBA_RESTORE_RD_MODIFIED:    return gba_restore_modified();
        case GBA_RESTORE_RD_ROLLBACKS:   return (unsigned int)gr_rollbacks;

        /* RAW UNIT COUNTS, LABELLED AS SUCH AT EVERY PRINT SITE.
         * flash_bank_cnt is 1 or 2 and eeprom_size is 1 or 16 -- NEITHER IS A
         * BYTE COUNT. */
        case GBA_RESTORE_RD_BACKUP_TYPE: return (unsigned int)backup_type;
        case GBA_RESTORE_RD_BANKCNT:     return (unsigned int)flash_bank_cnt;
        case GBA_RESTORE_RD_EEPSIZE:     return (unsigned int)eeprom_size;

        /* 1 = gpSP's DATABASE asserted a 128 KB flash part and left the type
         * unset. NOT a detection -- see gba_restore_db_flash128(). */
        case GBA_RESTORE_RD_DBFLASH128:  return gba_restore_db_flash128();

        default:                         return 0u;
    }
}
