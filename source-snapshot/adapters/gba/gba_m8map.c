/* LUAport M8 -- adapters/gba/gba_m8map.c
 *
 * The gpSP-facing half of M8's ROM-size-aware cartridge mapping probe.
 * adapters/gba/gba_m8map.h carries the full derivation; this file is the
 * mechanism. Read the header first.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT call execute_arm(), update_gba(), load_gamepak() or
 *     load_gamepak_page(). It is a pure OBSERVER: every function below only
 *     READS gpSP globals. Nothing here can change emulator state.
 *   - does NOT allocate. arena_used() is identical across a call, which is what
 *     lets M8's step-118 M8_CT_ARENA assertion stay meaningful.
 *   - does NOT include anything from runtime/ -- see the type rule in the
 *     header.
 *
 * THE gpsp TREE IS NOT MODIFIED. Every symbol below is either already declared
 * in a gpSP header or is a non-static global declared extern by hand, which is
 * the technique upstream itself uses at gpsp/gba_memory.c:198 and that
 * adapters/gba/gba_probe.c:75-85 established for M2.
 */

#include "common.h"                  /* memory_map_read, bios_rom, gamepak_size,
                                        gamepak_buffer_count, gamepak_must_swap */

#include "gba_m8map.h"

/* Non-static gpSP globals with no extern in a gpSP header. Identical
 * declarations to adapters/gba/gba_rom.c:81-84, which is deliberate: two
 * translation units declaring the same object must agree, and copying the
 * declaration verbatim is how that is guaranteed. */
extern u8      *gamepak_buffers[32];              /* gba_memory.c:369          */
extern u32      gamepak_file_blocks;              /* gba_memory.c:372          */
extern const unsigned gamepak_buffer_blocksize;   /* gba_memory.c:378          */

/* ------------------------------------------------------------- the detail --
 *
 * The first failing slot and its expected/actual values, recorded by
 * m8map_probe() so the fixture can put them in ext->dbg[] without the probe
 * having to return a struct across the type boundary. .bss, zero cost in the
 * transferred blob. */
static u32 m8map_bad_slot  = M8MAP_NOPAGE;
static u32 m8map_bad_exp   = M8MAP_NOPAGE;
static u32 m8map_bad_act   = M8MAP_NOPAGE;
static u32 m8map_bad_exp_l = 0;
static u32 m8map_bad_act_l = 0;

static u32 m8map_alt_slots = 0;
static u32 m8map_alt_first = M8MAP_NOPAGE;

/* ------------------------------------------------------------- primitives -- */

/* map_rom_entry's `mirror_blocks` argument, gba_memory.c:2755+2788. This is
 * derived from gamepak_size, NOT from gamepak_file_blocks: for the ordinary
 * (non-mirror_1m) path they are equal, but it is the former that gpSP actually
 * passes and the probe must model what gpSP does, not what it means. */
static u32 m8map_stride(void)
{
    return gamepak_size >> 15;
}

/* How many physical pages the load loop actually mapped. The j loop at
 * gba_memory.c:2782 is bounded BOTH by gamepak_file_blocks and by the number of
 * 1 MB chunks that were read (ldblks * 32), so a partially resident ROM maps
 * fewer pages than it has. */
static u32 m8map_pages(void)
{
    u32 bs = (u32)gamepak_buffer_blocksize;
    u32 buf_blocks, ldblks, cap;

    if (bs == 0)
        return 0;

    buf_blocks = (gamepak_size + bs - 1u) / bs;                  /* :2754 */
    ldblks = (buf_blocks < gamepak_buffer_count) ? buf_blocks
                                                 : gamepak_buffer_count; /* :2756 */
    cap = ldblks * 32u;

    return (gamepak_file_blocks < cap) ? gamepak_file_blocks : cap;
}

/* The pointer load_gamepak_raw hands to map_rom_entry for physical page phyn:
 * &gamepak_buffers[phyn / 32][32768 * (phyn % 32)], gba_memory.c:2785. */
static u8 *m8map_page_ptr(u32 phyn)
{
    u32 b = phyn >> 5;

    if (b >= 32u || b >= gamepak_buffer_count || gamepak_buffers[b] == 0)
        return 0;

    return &gamepak_buffers[b][M8MAP_PAGE * (phyn & 31u)];
}

/* The inverse: which physical page does this pointer name? M8MAP_NOPAGE for
 * NULL, M8MAP_ALIENPAGE for anything that is not the base of a 32 KB page of an
 * allocated buffer. */
static u32 m8map_page_of(const u8 *p)
{
    u32 b, off;

    if (p == 0)
        return M8MAP_NOPAGE;

    for (b = 0; b < gamepak_buffer_count && b < 32u; b++) {
        if (gamepak_buffers[b] == 0)
            continue;
        if (p >= gamepak_buffers[b] &&
            p <  gamepak_buffers[b] + gamepak_buffer_blocksize) {
            off = (u32)(p - gamepak_buffers[b]);
            if (off & (M8MAP_PAGE - 1u))
                return M8MAP_ALIENPAGE;      /* inside a buffer but not a page */
            return b * 32u + (off >> 15);
        }
    }
    return M8MAP_ALIENPAGE;
}

/* ------------------------------------------------------------- the replay --
 *
 * Which physical page do gpSP's OWN loops leave in `slot`?
 *
 * WHY THIS IS THREE MODULO OPERATIONS AND NOT A 2,561-ENTRY SCRATCH ARRAY.
 * Each of the three window loops writes `base + idx + mcount` for
 * mcount = 0, B, 2B, ... < limit. For a given slot the writing idx is therefore
 * UNIQUELY determined: idx = (slot - base) mod B, with mcount = (slot - base) -
 * idx. So there are at most THREE candidate writers, one per loop, and they can
 * be enumerated directly.
 *
 * WHICH ONE WINS. The j loop at gba_memory.c:2782 runs phyn ASCENDING, and
 * within a single map_rom_entry expansion no slot is written twice -- the 0x08
 * writes span [4096+idx, 5119+idx], the 0x0A writes [5120+idx, 6143+idx] and the
 * 0x0C writes [6144+idx, 6654+idx], three disjoint intervals for any idx < 1024.
 * So the LAST write to a slot is the one from the LARGEST candidate idx, and
 * that is what this returns.
 *
 * Returns M8MAP_NOPAGE when no loop writes the slot -- which means map_null's
 * NULL (gba_memory.c:2771) must still be there. */
static u32 m8map_replay_page(u32 slot)
{
    u32 stride = m8map_stride();
    u32 pages  = m8map_pages();
    u32 best   = M8MAP_NOPAGE;
    u32 w, base, limit, d, phyn, mcount;

    if (stride == 0u)
        return M8MAP_NOPAGE;

    for (w = 0; w < 3u; w++) {
        if (w == 0u)      { base = M8MAP_IDX_08; limit = M8MAP_WIN_08; }
        else if (w == 1u) { base = M8MAP_IDX_0A; limit = M8MAP_WIN_0A; }
        else              { base = M8MAP_IDX_0C; limit = M8MAP_WIN_0C; }

        if (slot < base)
            continue;

        d      = slot - base;
        phyn   = d % stride;
        mcount = d - phyn;

        if (mcount >= limit)        /* this loop never reaches that far        */
            continue;
        if (phyn >= pages)          /* that page was never loaded or mapped    */
            continue;

        if (best == M8MAP_NOPAGE || phyn > best)
            best = phyn;
    }

    return best;
}

/* What a GBA -- and what map_rom_entry evidently INTENDS -- would put in `slot`:
 * each of the three windows mirrors the cartridge from page 0, and 0x0D is not
 * cartridge at all.
 *
 * THIS IS NOT ASSERTED OUTSIDE THE 0x08 WINDOW. It is the L2 reference, used to
 * REPORT gpSP's divergence by name. */
static u32 m8map_ideal_page(u32 slot)
{
    u32 stride = m8map_stride();

    if (stride == 0u)
        return M8MAP_NOPAGE;

    if (slot >= M8MAP_IDX_08 && slot < M8MAP_IDX_08 + M8MAP_WIN_08)
        return (slot - M8MAP_IDX_08) % stride;
    if (slot >= M8MAP_IDX_0A && slot < M8MAP_IDX_0A + M8MAP_WIN_0A)
        return (slot - M8MAP_IDX_0A) % stride;
    if (slot >= M8MAP_IDX_0C && slot < M8MAP_IDX_0C + M8MAP_WIN_0C)
        return (slot - M8MAP_IDX_0C) % stride;

    return M8MAP_NOPAGE;            /* 0x0D000000 and above */
}

static void m8map_record(u32 slot, u32 exp_page, u32 act_page,
                         const u8 *exp_ptr, const u8 *act_ptr)
{
    if (m8map_bad_slot != M8MAP_NOPAGE)
        return;                     /* FIRST failure only -- later ones are
                                       almost always the same defect repeated */
    m8map_bad_slot  = slot;
    m8map_bad_exp   = exp_page;
    m8map_bad_act   = act_page;
    /* (u32)(u64)ptr, NOT uintptr_t: gpsp/common.h does not include <stdint.h>,
     * so uintptr_t is not guaranteed to be in scope. Same cast, same reason, as
     * adapters/gba/gba_exec.c:270. These low halves are for DIAGNOSIS only --
     * the pointer IDENTITY assertion above is made on the full pointers. */
    m8map_bad_exp_l = (u32)(u64)exp_ptr;
    m8map_bad_act_l = (u32)(u64)act_ptr;
}

/* ----------------------------------------------------------------- probes -- */

unsigned int m8map_probe(void)
{
    unsigned int f = 0;
    u32 slot, stride, pages;

    m8map_bad_slot  = M8MAP_NOPAGE;
    m8map_bad_exp   = M8MAP_NOPAGE;
    m8map_bad_act   = M8MAP_NOPAGE;
    m8map_bad_exp_l = 0;
    m8map_bad_act_l = 0;

    /* Refuse to dereference anything if init_gamepak_buffer got nothing or no
     * cartridge was loaded -- the same guard gba_rom.c:154 applies. */
    if (gamepak_buffer_count == 0 || gamepak_buffers[0] == 0 || gamepak_size == 0)
        return M8MAP_NO_ROM;

    stride = m8map_stride();
    pages  = m8map_pages();

    /* map_rom_entry steps mcount by `mirror_blocks`; a zero stride is an
     * infinite loop upstream and a division by zero here, and a stride above
     * 1024 means the 0x08 loop never executes at all. */
    if (stride == 0u || stride > 1024u)
        f |= M8MAP_STRIDE;

    /* THE PAGING QUESTION, ANSWERED FROM SOURCE RATHER THAN ASSUMED.
     * gamepak_must_swap() (gba_memory.c:2383) is
     * `gamepak_buffer_count * gamepak_buffer_blocksize < gamepak_size`. With
     * -DROM_BUFFER_SIZE=2 that is 2 MB against KEYIRQ's 96 KB, so it is FALSE
     * and load_gamepak_page() is unreachable for this cartridge. If it were ever
     * TRUE, slots would legitimately be NULL and the walk below would be
     * measuring the wrong contract -- so it is a fatal bit, not a silent
     * branch. */
    if (gamepak_must_swap())
        f |= M8MAP_SWAP;

    /* Everything the load loop could map, it must have mapped. */
    if (pages != gamepak_file_blocks)
        f |= M8MAP_PAGES;

    /* THE BIOS, ASSERTED SEPARATELY AND BY ITS OWN NAME. init_memory maps
     * [0, 0x1000000) to bios_rom (gba_memory.c:2410) and nothing on the
     * cartridge path touches an index below 4096, so a cartridge fault can never
     * move this -- which is exactly why it gets its own bit instead of being
     * folded into a "cartridge or BIOS" message. */
    if (memory_map_read[0] != bios_rom)
        f |= M8MAP_BIOS;

    /* If the geometry itself is wrong the per-slot expectations are computed
     * from a lie, and 2,561 derived failures would bury the one real one. */
    if (f & (M8MAP_STRIDE | M8MAP_SWAP | M8MAP_PAGES))
        return f;

    for (slot = M8MAP_SLOT_LO; slot <= M8MAP_SLOT_HI; slot++) {
        u8 *actual = memory_map_read[slot];
        u32 rp     = m8map_replay_page(slot);
        u8 *want   = (rp == M8MAP_NOPAGE) ? (u8 *)0 : m8map_page_ptr(rp);
        u32 ap     = m8map_page_of(actual);

        /* L1 -- gpSP's own algorithm, replayed. */
        if (actual != want) {
            f |= M8MAP_REPLAY;
            m8map_record(slot, rp, ap, want, actual);
        }

        /* A non-NULL entry that is not a page base is never legitimate, however
         * the windows are strided. */
        if (actual != 0 && ap == M8MAP_ALIENPAGE) {
            f |= M8MAP_ALIEN;
            m8map_record(slot, rp, ap, want, actual);
        }

        /* L3 -- the execution window, held to the HARDWARE-IDEAL value. */
        if (slot < M8MAP_IDX_08 + M8MAP_WIN_08) {
            u32 ip = m8map_ideal_page(slot);

            if (actual == 0) {
                f |= M8MAP_EXEC_NULL;
                m8map_record(slot, ip, ap, m8map_page_ptr(ip), actual);
            } else if (ap != ip) {
                f |= M8MAP_EXEC_WINDOW;
                m8map_record(slot, ip, ap, m8map_page_ptr(ip), actual);
            }
        }
    }

    return f;
}

unsigned int m8map_report(void)
{
    unsigned int r = 0;
    u32 slot;

    m8map_alt_slots = 0;
    m8map_alt_first = M8MAP_NOPAGE;

    if (gamepak_buffer_count == 0 || gamepak_buffers[0] == 0 ||
        gamepak_size == 0 || m8map_stride() == 0u)
        return 0;

    /* Starts at 0x0A: the 0x08 window is asserted FATALLY against the ideal in
     * m8map_probe(), so any divergence there is a failure and not a report. */
    for (slot = M8MAP_IDX_0A; slot <= M8MAP_SLOT_HI; slot++) {
        u32 rp = m8map_replay_page(slot);
        u32 ip = m8map_ideal_page(slot);

        if (rp == ip)
            continue;

        m8map_alt_slots++;
        if (m8map_alt_first == M8MAP_NOPAGE)
            m8map_alt_first = slot;

        if (slot == M8MAP_IDX_0D)
            r |= M8MAP_R_EEPROM;
        else
            r |= M8MAP_R_ALT_STRIDE;
    }

    return r;
}

unsigned int m8map_expected_blocks(unsigned int file_size)
{
    /* gba_memory.c:2701-2702, the same rounding m4_rom_expected_size() performs.
     * Restated rather than called so this adapter does not depend on FROZEN M4
     * code, and asserted equal to it by apps/m8gpsp/main.c at run time. */
    return ((file_size + (M8MAP_PAGE - 1u)) & ~(M8MAP_PAGE - 1u)) >> 15;
}

unsigned int m8map_read(unsigned int sel)
{
    switch (sel) {
    case M8MAP_RD_SIZE:         return gamepak_size;
    case M8MAP_RD_FILEBLOCKS:   return gamepak_file_blocks;
    case M8MAP_RD_STRIDE:       return m8map_stride();
    case M8MAP_RD_PAGES:        return m8map_pages();
    case M8MAP_RD_BUFCOUNT:     return gamepak_buffer_count;
    case M8MAP_RD_BLOCKSIZE:    return (unsigned int)gamepak_buffer_blocksize;
    case M8MAP_RD_MUSTSWAP:     return gamepak_must_swap() ? 1u : 0u;
    case M8MAP_RD_BADSLOT:      return m8map_bad_slot;
    case M8MAP_RD_EXP_PAGE:     return m8map_bad_exp;
    case M8MAP_RD_ACT_PAGE:     return m8map_bad_act;
    case M8MAP_RD_EXP_LOW:      return m8map_bad_exp_l;
    case M8MAP_RD_ACT_LOW:      return m8map_bad_act_l;
    case M8MAP_RD_ALT_SLOTS:    return m8map_alt_slots;
    case M8MAP_RD_ALT_FIRST:    return m8map_alt_first;
    case M8MAP_RD_EEPROM_PAGE:  return m8map_page_of(memory_map_read[M8MAP_IDX_0D]);
    case M8MAP_RD_MAP08_LOW:
        return (u32)(u64)memory_map_read[M8MAP_IDX_08];
    default:                    return 0u;
    }
}
