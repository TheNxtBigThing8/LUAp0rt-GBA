/* LUAport M12C -- adapters/gba/gba_sramobs.c
 *
 * The SRAM upper-half observer. adapters/gba/gba_sramobs.h carries the full
 * derivation -- why gpSP's SRAM window is 64 KB and not 32 KB, why that is only
 * a hypothesis until measured, and how to read the four result bits. READ THE
 * HEADER FIRST.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT write gamepak_backup[]. It holds a `const u8 *` to it and never
 *     holds anything else. gba_restore.c remains THE SOLE WRITER of that array
 *     in M12C, and this file must not become a second one.
 *   - does NOT call read_backup(), write_backup(), read_eeprom() or
 *     write_eeprom(). All four MUTATE backup_type (gba_memory.c:464-465,
 *     :1140-1141, :542). An observer that changed the classification while
 *     measuring it would be reporting its own side effect. m12c-verify asserts
 *     the absence of all four from gba_sramobs.o.
 *   - does NOT read or write backup_type, backup_type_reset, eeprom_size,
 *     flash_bank_cnt, flash_bank_num, flash_mode, flash_command_position,
 *     eeprom_mode, eeprom_address or eeprom_counter. It needs NONE of them:
 *     the question is about BYTES, not about the class gpSP thinks it has.
 *   - does NOT call load_gamepak_page(). gamepak_backup[] is a plain static
 *     array and is never demand paged.
 *   - does NOT open, name, read or write a file, and calls nothing from
 *     runtime/ or from the platform layer.
 *   - does NOT allocate. Every byte of state below is fixed-size .bss.
 *
 * THE gpsp TREE IS NOT MODIFIED. The one symbol used from it -- gamepak_backup
 * -- is already declared in gpsp/gba_memory.h:320.
 */

#include "common.h"                  /* u8/u32 and gamepak_backup             */

#include "gba_sramobs.h"

/* ------------------------------------------------------------ module state --
 *
 * Three slots, five scalars each, all .bss. Under 128 bytes in total. THERE IS
 * NO COPY OF THE SAVE HERE and there must never be one: a 64 KB snapshot would
 * be a second copy of the array in an image whose whole design avoids even a
 * single staging buffer (see gba_restorefile.h). A hash and a count answer the
 * question; the bytes themselves are not needed. */
static u32 so_low_fnv  [GBA_SRAMOBS_SLOTS];
static u32 so_high_fnv [GBA_SRAMOBS_SLOTS];
static u32 so_low_nonff[GBA_SRAMOBS_SLOTS];
static u32 so_high_nonff[GBA_SRAMOBS_SLOTS];
static u32 so_taken    [GBA_SRAMOBS_SLOTS];
static u32 so_samples   = 0;

/* --------------------------------------------------------------- FNV-1a 32 --
 *
 * THE SAME SEED AND PRIME AS EVERYWHERE ELSE IN THIS PROJECT --
 * adapters/gba/gba_save.c:103-104, adapters/gba/gba_savehdr.c,
 * adapters/gba/gba_restore.c:73-74, adapters/gba/gba_rom.c:488 and
 * tools/upload.py all use 2166136261 / 16777619. NO NEW HASH IS INTRODUCED
 * HERE, deliberately: the low half's hash at T1 must be directly comparable
 * with the figure gba_restore_region_fnv() prints beside the restore and with
 * the payload hash M12B stored in the .hdr. A private algorithm would produce
 * numbers that could only be compared with themselves.
 *
 * RESTATED RATHER THAN SHARED, for gba_restore.c:67-72's reason: the other
 * implementations are `static` behind a type boundary this file is on the wrong
 * side of. tools/gba_restore_equiv.c asserts that all of them agree on the same
 * input. */
#define SO_FNV_OFFSET 2166136261u
#define SO_FNV_PRIME    16777619u

static u32 so_fnv(const u8 *p, u32 n)
{
    u32 h = SO_FNV_OFFSET;
    u32 i;

    for (i = 0; i < n; i++) {
        h ^= (u32)p[i];
        h *= SO_FNV_PRIME;
    }
    return h;
}

static u32 so_nonff(const u8 *p, u32 n)
{
    u32 c = 0;
    u32 i;

    for (i = 0; i < n; i++)
        if (p[i] != 0xFF) c++;

    return c;
}

/* ----------------------------------------------------------------- the API --
 */

void m12c_sramobs_reset(void)
{
    u32 i;

    for (i = 0; i < GBA_SRAMOBS_SLOTS; i++) {
        so_low_fnv[i]   = 0;
        so_high_fnv[i]  = 0;
        so_low_nonff[i] = 0;
        so_high_nonff[i]= 0;
        so_taken[i]     = 0;
    }
    so_samples = 0;
}

void m12c_sramobs_sample(unsigned int slot)
{
    /* A READ-ONLY VIEW, AND THE ONLY HANDLE THIS FILE EVER TAKES ON THE ARRAY.
       `const` here is not decoration: it is what makes "the observer cannot
       perturb the experiment" a property the COMPILER enforces rather than one
       the reader has to audit by eye. */
    const u8 *backup = (const u8 *)gamepak_backup;

    /* AN UNKNOWN SLOT IS DROPPED, NOT CLAMPED. Folding it onto slot 2 would
       overwrite a real measurement with a later one and the operator would
       never know the difference. */
    if (slot >= GBA_SRAMOBS_SLOTS) return;

    /* THE WINDOW MUST FIT INSIDE THE ARRAY. gamepak_backup[] is 131,072 bytes
       (gpsp/gba_memory.h:320) and the window is 65,536, so this can only fire
       if one of those two constants is ever changed -- in which case reading
       past the end is a far worse outcome than declining to measure. */
    if (GBA_SRAMOBS_HIGH_OFF + GBA_SRAMOBS_HALF_BYTES >
        (u32)sizeof(gamepak_backup)) return;

    so_low_fnv[slot]    = so_fnv  (backup + GBA_SRAMOBS_LOW_OFF,
                                   GBA_SRAMOBS_HALF_BYTES);
    so_low_nonff[slot]  = so_nonff(backup + GBA_SRAMOBS_LOW_OFF,
                                   GBA_SRAMOBS_HALF_BYTES);

    so_high_fnv[slot]   = so_fnv  (backup + GBA_SRAMOBS_HIGH_OFF,
                                   GBA_SRAMOBS_HALF_BYTES);
    so_high_nonff[slot] = so_nonff(backup + GBA_SRAMOBS_HIGH_OFF,
                                   GBA_SRAMOBS_HALF_BYTES);

    if (!so_taken[slot]) {
        so_taken[slot] = 1;
        so_samples++;
    }
}

/* THE FOUR BITS.
 *
 * "CHANGED" IS HASH-OR-COUNT, NOT HASH ALONE. FNV-1a is not a cryptographic
 * hash and a 32-bit digest over 32 KB has collisions; the non-0xFF count is a
 * cheap, independent witness that catches the overwhelming majority of them.
 * Requiring BOTH to agree before declaring "unchanged" is the conservative
 * direction: this observer exists to avoid MISSING a change.
 *
 * THE COMPARISON BASE IS T2, NOT T1. Only T2->T3 brackets a window in which
 * emulated cartridge code actually ran. Comparing against T1 would fold any
 * post-restore setup step into the game's account.
 *
 * AN UNTAKEN SLOT YIELDS NO CHANGE BIT. If the run ended before T3 -- a fatal
 * restore status, a failed stability gate -- then "changed" is UNKNOWN, and
 * reporting UNKNOWN as "no" would quietly refute the hypothesis on the
 * strength of a run that never tested it. */
static u32 so_bits(void)
{
    u32 b = 0;
    u32 have = so_taken[GBA_SRAMOBS_T2] && so_taken[GBA_SRAMOBS_T3];

    if (so_taken[GBA_SRAMOBS_T3] && so_high_nonff[GBA_SRAMOBS_T3])
        b |= GBA_SRAMOBS_B_HIGH_NONFF_T3;

    if (so_taken[GBA_SRAMOBS_T1] && so_high_nonff[GBA_SRAMOBS_T1])
        b |= GBA_SRAMOBS_B_HIGH_NONFF_T1;

    if (have &&
        (so_high_fnv[GBA_SRAMOBS_T2]   != so_high_fnv[GBA_SRAMOBS_T3] ||
         so_high_nonff[GBA_SRAMOBS_T2] != so_high_nonff[GBA_SRAMOBS_T3]))
        b |= GBA_SRAMOBS_B_HIGH_CHANGED;

    if (have &&
        (so_low_fnv[GBA_SRAMOBS_T2]   != so_low_fnv[GBA_SRAMOBS_T3] ||
         so_low_nonff[GBA_SRAMOBS_T2] != so_low_nonff[GBA_SRAMOBS_T3]))
        b |= GBA_SRAMOBS_B_LOW_CHANGED;

    return b;
}

unsigned int m12c_sramobs_read(unsigned int sel)
{
    if (sel == GBA_SRAMOBS_RD_BITS)    return (unsigned int)so_bits();
    if (sel == GBA_SRAMOBS_RD_SAMPLES) return (unsigned int)so_samples;

    /* LOW_FNV is 0u; unsigned sel therefore needs only the upper bound. */
    if (sel < GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_SLOTS)
        return (unsigned int)so_low_fnv[sel - GBA_SRAMOBS_RD_LOW_FNV];

    if (sel >= GBA_SRAMOBS_RD_HIGH_FNV &&
        sel <  GBA_SRAMOBS_RD_HIGH_FNV + GBA_SRAMOBS_SLOTS)
        return (unsigned int)so_high_fnv[sel - GBA_SRAMOBS_RD_HIGH_FNV];

    if (sel >= GBA_SRAMOBS_RD_LOW_NONFF &&
        sel <  GBA_SRAMOBS_RD_LOW_NONFF + GBA_SRAMOBS_SLOTS)
        return (unsigned int)so_low_nonff[sel - GBA_SRAMOBS_RD_LOW_NONFF];

    if (sel >= GBA_SRAMOBS_RD_HIGH_NONFF &&
        sel <  GBA_SRAMOBS_RD_HIGH_NONFF + GBA_SRAMOBS_SLOTS)
        return (unsigned int)so_high_nonff[sel - GBA_SRAMOBS_RD_HIGH_NONFF];

    if (sel >= GBA_SRAMOBS_RD_TAKEN &&
        sel <  GBA_SRAMOBS_RD_TAKEN + GBA_SRAMOBS_SLOTS)
        return (unsigned int)so_taken[sel - GBA_SRAMOBS_RD_TAKEN];

    return 0;
}
