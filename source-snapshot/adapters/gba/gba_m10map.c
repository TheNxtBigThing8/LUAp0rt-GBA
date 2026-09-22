/* LUAport M10 -- adapters/gba/gba_m10map.c
 *
 * The gpSP-facing half of M10's DEMAND-PAGING-AWARE cartridge mapping probe.
 * adapters/gba/gba_m10map.h carries the full derivation of the contract; this
 * file is the mechanism. READ THE HEADER FIRST -- in particular the note that
 * NULL in the cartridge window is the PAGE-FAULT TRIGGER and not an error.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT call execute_arm(), update_gba(), load_gamepak(),
 *     load_gamepak_page() or evict_gamepak_page(). It is a pure OBSERVER:
 *     every function below only READS gpSP globals. Nothing here can change
 *     emulator state, and in particular NOTHING HERE CAN CAUSE A PAGE FAULT --
 *     which is why the page-load count it reports is the emulator's own and
 *     not an artefact of measuring it.
 *   - does NOT allocate. arena_used() is identical across every call, which is
 *     what lets M10's steady-state arena tripwire stay meaningful.
 *   - does NOT include anything from runtime/ -- see the type rule in the
 *     header.
 *   - does NOT reference adapters/gba/gba_m8map.h. The two probes are
 *     INDEPENDENT by construction so that a defect in one cannot mask the
 *     other; m10-verify asserts the absence of any m8map_ relocation here.
 *
 * THE gpsp TREE IS NOT MODIFIED. Every symbol below is either already declared
 * in a gpSP header or is a non-static global declared extern by hand, which is
 * the technique upstream itself uses at gpsp/gba_memory.c:198 and that
 * adapters/gba/gba_probe.c:75-85 established for M2.
 */

#include "common.h"                  /* memory_map_read, bios_rom, gamepak_size,
                                        gamepak_buffer_count, gamepak_must_swap,
                                        gamepak_sticky_bit                     */

#include "gba_m10map.h"

/* Non-static gpSP globals with no extern in a gpSP header.
 *
 * The first three are IDENTICAL to adapters/gba/gba_rom.c:81-84 and
 * adapters/gba/gba_m8map.c:32-34, which is deliberate: translation units
 * declaring the same object must agree, and copying the declaration verbatim is
 * how that is guaranteed. */
extern u8      *gamepak_buffers[32];              /* gba_memory.c:369          */
extern u32      gamepak_file_blocks;              /* gba_memory.c:372          */
extern const unsigned gamepak_buffer_blocksize;   /* gba_memory.c:378          */

/* THE LRU QUEUE, AND WHY IT HAS TO BE RE-DECLARED BY HAND.
 *
 * gba_memory.c:381-384 defines it as an UNTAGGED struct with external linkage
 * and gba_memory.h does not declare it, so there is no type to include. The
 * declaration below is transcribed CHARACTER FOR CHARACTER from that
 * definition. C99 6.2.7 makes two untagged structures with the same member
 * names, types and order COMPATIBLE ACROSS TRANSLATION UNITS, so this is a
 * declaration of the same object and not a new one.
 *
 * THIS IS THE STATE m8map CANNOT SEE AND THAT MAKES THE WHOLE DIFFERENCE. A
 * non-resident cartridge page and a broken mapping both present as a NULL
 * memory_map_read[] slot. ONLY gamepak_blk_queue[] says which of the two it is. */
extern struct {
  u16 next_lru;             /* Index in the struct to the next LRU entry       */
  s16 phy_rom;              /* ROM page number (-1 means not mapped)           */
} gamepak_blk_queue[1024];                        /* gba_memory.c:381-384      */

extern u16 gamepak_lru_head;                      /* gba_memory.c:386          */

/* ------------------------------------------------------------- the detail --
 *
 * Recorded by m10map_probe() so the fixture can put it in ext->dbg[] without
 * the probe having to return a struct across the type boundary. .bss, zero cost
 * in the transferred blob. */
static u32 m10map_bad_slot  = M10MAP_NOPAGE;
static u32 m10map_bad_exp   = M10MAP_NOPAGE;
static u32 m10map_bad_act   = M10MAP_NOPAGE;
static u32 m10map_bad_exp_l = 0;
static u32 m10map_bad_act_l = 0;
static u32 m10map_bad_entry = M10MAP_NOPAGE;

/* THE FIRST-FAILURE FLAG IS SEPARATE FROM m10map_bad_slot ON PURPOSE.
 * gba_m8map.c:198 can key "have I already recorded one?" off its bad-slot
 * sentinel because every failure it records HAS a slot. This probe also records
 * QUEUE failures, which are properties of a cache ENTRY and have no slot at
 * all -- they legitimately store M10MAP_NOPAGE there. Reusing the sentinel as
 * the guard would let a second queue failure overwrite the first, so the
 * "FIRST failure only" rule needs a flag of its own. */
static u32 m10map_have_bad  = 0;

static u32 m10map_mapped_slots = 0;
static u32 m10map_null_slots   = 0;
static u32 m10map_ambig_slots  = 0;
static u32 m10map_sticky_or    = 0;

/* ------------------------------------------------------------- the bitmaps --
 *
 * THREE BITMAPS, NOT THREE ARRAYS OF POINTERS. gba_m8map.h:122-127 argues
 * against a 2,561-entry scratch table; the same argument applies here and these
 * are the cheap form of the same information.
 *
 *   page_bm   which ROM pages the LRU queue currently names       1024 bits
 *   slot_bm   which slots some resident page writes               3583 bits
 *   ambig_bm  slots more than one resident page writes            3583 bits
 *
 * 128 + 448 + 448 = 1,024 bytes of .bss, plus the 2,048-byte paging snapshot
 * below. All STATIC because M10's arena tripwire terminates a run on ONE byte
 * of steady-state allocation. */
#define M10MAP_SLOT_WORDS  ((M10MAP_SLOTS + 31u) / 32u)          /* 112 */
#define M10MAP_PAGE_WORDS  (M10MAP_MAXPAGES / 32u)               /*  32 */

static u32 m10map_page_bm[M10MAP_PAGE_WORDS];
static u32 m10map_slot_bm[M10MAP_SLOT_WORDS];
static u32 m10map_ambig_bm[M10MAP_SLOT_WORDS];

/* The page-load measurement's state -- see the header's PAGE-LOAD MEASUREMENT
 * block for why gamepak_buffer_count cannot be used for this. */
static s16 m10map_snap[M10MAP_MAXENTRIES];
static u32 m10map_snap_n     = 0;
static u32 m10map_lru_prev   = 0;
static u32 m10map_lru_evicts = 0;
static u32 m10map_page_loads = 0;

static void m10map_bm_set(u32 *bm, u32 i)
{
    bm[i >> 5] |= (1u << (i & 31u));
}

static u32 m10map_bm_test(const u32 *bm, u32 i)
{
    return (bm[i >> 5] >> (i & 31u)) & 1u;
}

static void m10map_bm_clear(u32 *bm, u32 words)
{
    u32 i;

    for (i = 0; i < words; i++)
        bm[i] = 0u;
}

/* ------------------------------------------------------------- primitives -- */

/* map_rom_entry's `mirror_blocks` argument, gba_memory.c:2755+2789 and :2321.
 * Derived from gamepak_size and NOT from gamepak_file_blocks, because that is
 * what gpSP actually passes -- the probe must model what gpSP DOES, not what it
 * means. */
static u32 m10map_stride(void)
{
    return gamepak_size >> 15;
}

/* The LIVE length of the LRU ring. init_gamepak_buffer sets
 * gamepak_lru_tail = 32 * gamepak_buffer_count - 1 (gba_memory.c:2380), so
 * entries [0, 32*bufcount) are the whole reachable ring and entries above it
 * are permanently unreachable. */
static u32 m10map_entries(void)
{
    u32 n = gamepak_buffer_count * 32u;

    return (n > M10MAP_MAXENTRIES) ? M10MAP_MAXENTRIES : n;
}

unsigned int m10map_expected_resident(void)
{
    u32 bs = (u32)gamepak_buffer_blocksize;
    u32 buf_blocks, ldblks, cap;

    if (bs == 0u)
        return 0u;

    buf_blocks = (gamepak_size + bs - 1u) / bs;                  /* :2754 */
    ldblks = (buf_blocks < gamepak_buffer_count) ? buf_blocks
                                                 : gamepak_buffer_count; /* :2756 */
    cap = ldblks * 32u;

    return (gamepak_file_blocks < cap) ? gamepak_file_blocks : cap;   /* :2782 */
}

/* How many pages the LRU queue says are resident RIGHT NOW. Compare against
 * m10map_expected_resident(): they must be equal at every instant, because
 * load_gamepak_page() evicts exactly one entry and installs exactly one
 * (gba_memory.c:2330-2350). */
static u32 m10map_resident_count(void)
{
    u32 n = m10map_entries();
    u32 e, c = 0;

    for (e = 0; e < n; e++)
        if (gamepak_blk_queue[e].phy_rom >= 0)
            c++;

    return c;
}

/* The pointer gpSP hands to map_rom_entry for CACHE ENTRY e:
 * &gamepak_buffers[e / 32][32768 * (e % 32)], gba_memory.c:2331-2333 for the
 * fault path and :2785 for the load path -- which are the same expression, and
 * that identity is exactly why one formula covers both map states. */
static u8 *m10map_entry_ptr(u32 e)
{
    u32 b = e >> 5;

    if (b >= 32u || b >= gamepak_buffer_count || gamepak_buffers[b] == 0)
        return 0;

    return &gamepak_buffers[b][M10MAP_PAGE * (e & 31u)];
}

/* The inverse: which CACHE ENTRY does this pointer name?
 *
 *   M10MAP_NOPAGE     the pointer is NULL
 *   M10MAP_ALIENPAGE  inside an allocated buffer but NOT on a 32 KB boundary
 *   M10MAP_OOBPAGE    not inside any allocated buffer at all
 *
 * The last two are split because they mean different things: ALIEN is a bad
 * offset computation, OOB is a pointer that escaped the buffers entirely. */
static u32 m10map_entry_of(const u8 *p)
{
    u32 b, off;

    if (p == 0)
        return M10MAP_NOPAGE;

    for (b = 0; b < gamepak_buffer_count && b < 32u; b++) {
        if (gamepak_buffers[b] == 0)
            continue;
        if (p >= gamepak_buffers[b] &&
            p <  gamepak_buffers[b] + gamepak_buffer_blocksize) {
            off = (u32)(p - gamepak_buffers[b]);
            if (off & (M10MAP_PAGE - 1u))
                return M10MAP_ALIENPAGE;     /* inside a buffer but not a page */
            return b * 32u + (off >> 15);
        }
    }
    return M10MAP_OOBPAGE;
}

/* Which ROM PAGE is currently held in the cache entry this pointer names? The
 * sentinels pass through unchanged: they are all >= M10MAP_MAXENTRIES and a
 * real entry index never is. */
static u32 m10map_page_of(const u8 *p)
{
    u32 e = m10map_entry_of(p);

    if (e >= M10MAP_MAXENTRIES)
        return e;
    if (gamepak_blk_queue[e].phy_rom < 0)
        return M10MAP_NOPAGE;

    return (u32)gamepak_blk_queue[e].phy_rom;
}

static void m10map_record(u32 slot, u32 exp_page, u32 act_page,
                          const u8 *exp_ptr, const u8 *act_ptr, u32 entry)
{
    if (m10map_have_bad)
        return;                     /* FIRST failure only -- later ones are
                                       almost always the same defect repeated */
    m10map_have_bad  = 1u;
    m10map_bad_slot  = slot;
    m10map_bad_exp   = exp_page;
    m10map_bad_act   = act_page;
    m10map_bad_entry = entry;
    /* (u32)(u64)ptr, NOT uintptr_t: gpsp/common.h does not include <stdint.h>,
     * so uintptr_t is not guaranteed to be in scope. Same cast, same reason, as
     * adapters/gba/gba_exec.c:270 and gba_m8map.c:204-209. These low halves are
     * for DIAGNOSIS only -- the pointer IDENTITY assertion is made on the FULL
     * pointers. */
    m10map_bad_exp_l = (u32)(u64)exp_ptr;
    m10map_bad_act_l = (u32)(u64)act_ptr;
}

/* --------------------------------------------------- the map_rom_entry walk --
 *
 * Enumerates EXACTLY the slots map_rom_entry(read, page, ., stride) writes,
 * in its own order, INCLUDING its overrun. gba_memory.c:2254-2263:
 *
 *     for (mcount = 0; mcount < 1024; mcount += stride) {
 *         [4096 + page + mcount] = ptr;
 *         [5120 + page + mcount] = ptr;
 *     }
 *     for (mcount = 0; mcount <  512; mcount += stride) {
 *         [6144 + page + mcount] = ptr;
 *     }
 *
 * `mode` selects what to do with each slot:
 *   0  MARK   -- record the slot in slot_bm, and in ambig_bm if already marked
 *   1  CHECK  -- require memory_map_read[slot] == want, skipping ambiguous ones
 *
 * Returns a failure mask (CHECK mode only). A single page never writes one slot
 * twice: for a fixed page the three intervals [4096+p, 5119+p], [5120+p,
 * 6143+p] and [6144+p, 6655+p] are disjoint, so every collision ambig_bm
 * records is between two DIFFERENT resident pages. */
static unsigned int m10map_walk_page(u32 page, u8 *want, u32 entry, u32 mode)
{
    unsigned int f = 0;
    u32 w, base, limit, mcount, slot, idx;
    u32 stride = m10map_stride();

    if (stride == 0u)
        return 0u;

    for (w = 0; w < 3u; w++) {
        if (w == 0u)      { base = M10MAP_IDX_08; limit = M10MAP_WIN_08; }
        else if (w == 1u) { base = M10MAP_IDX_0A; limit = M10MAP_WIN_0A; }
        else              { base = M10MAP_IDX_0C; limit = M10MAP_WIN_0C; }

        for (mcount = 0; mcount < limit; mcount += stride) {
            slot = base + page + mcount;

            /* Cannot happen for stride <= 1024 and page < stride -- the header
             * derives the 7678 bound -- but a probe that indexes an array from
             * emulator state without a bound is not a probe. */
            if (slot < M10MAP_SLOT_LO || slot > M10MAP_SLOT_HI)
                continue;

            idx = slot - M10MAP_SLOT_LO;

            if (mode == 0u) {
                if (m10map_bm_test(m10map_slot_bm, idx)) {
                    if (!m10map_bm_test(m10map_ambig_bm, idx)) {
                        m10map_bm_set(m10map_ambig_bm, idx);
                        m10map_ambig_slots++;
                    }
                } else {
                    m10map_bm_set(m10map_slot_bm, idx);
                }
                continue;
            }

            /* AMBIGUOUS SLOTS ARE NOT ASSERTED, AND THE HEADER SAYS SO OUT
             * LOUD. Two resident pages write this slot, the winner depends on
             * FAULT ORDER which no observer can reconstruct, and an eviction of
             * either co-tenant legitimately NULLs it. Only a non-power-of-two
             * stride can produce one; Metroid's is 256 and produces none. */
            if (m10map_bm_test(m10map_ambig_bm, idx))
                continue;

            if (memory_map_read[slot] != want) {
                f |= M10MAP_RESPTR;
                if (slot < M10MAP_IDX_08 + M10MAP_WIN_08)
                    f |= M10MAP_EXECWIN;
                m10map_record(slot, page, m10map_page_of(memory_map_read[slot]),
                              want, memory_map_read[slot], entry);
            }
        }
    }

    return f;
}

/* ----------------------------------------------------------------- probes -- */

unsigned int m10map_probe(void)
{
    unsigned int f = 0;
    u32 stride, entries, expect, resident = 0;
    u32 b, e, slot;

    m10map_have_bad  = 0;
    m10map_bad_slot  = M10MAP_NOPAGE;
    m10map_bad_exp   = M10MAP_NOPAGE;
    m10map_bad_act   = M10MAP_NOPAGE;
    m10map_bad_entry = M10MAP_NOPAGE;
    m10map_bad_exp_l = 0;
    m10map_bad_act_l = 0;
    m10map_mapped_slots = 0;
    m10map_null_slots   = 0;
    m10map_ambig_slots  = 0;
    m10map_bm_clear(m10map_page_bm,  M10MAP_PAGE_WORDS);
    m10map_bm_clear(m10map_slot_bm,  M10MAP_SLOT_WORDS);
    m10map_bm_clear(m10map_ambig_bm, M10MAP_SLOT_WORDS);

    /* Refuse to dereference anything if init_gamepak_buffer got nothing or no
     * cartridge was loaded -- the same guard gba_rom.c:154 applies. */
    if (gamepak_buffer_count == 0 || gamepak_buffers[0] == 0 || gamepak_size == 0)
        return M10MAP_NO_ROM;

    stride  = m10map_stride();
    entries = m10map_entries();
    expect  = m10map_expected_resident();

    /* map_rom_entry steps mcount by `mirror_blocks`; a zero stride is an
     * infinite loop upstream and a division by zero here, and a stride above
     * 1024 means the 0x08 loop never executes at all. */
    if (stride == 0u || stride > M10MAP_MAXPAGES)
        f |= M10MAP_STRIDE;

    /* gamepak_size is rounded to 32 KB at gba_memory.c:2701, so a size that is
     * not a whole number of pages means someone wrote it afterwards. A file
     * block count of zero means no payload; one ABOVE the stride means pages
     * exist that map_rom_entry can never address. */
    if ((gamepak_size & (M10MAP_PAGE - 1u)) != 0u ||
        gamepak_file_blocks == 0u || gamepak_file_blocks > stride)
        f |= M10MAP_GEOM;

    /* THE BIOS, ASSERTED SEPARATELY AND BY ITS OWN NAME. init_memory maps
     * [0, 0x1000000) to bios_rom (gba_memory.c:2410) and nothing on the
     * cartridge path touches an index below 4096, so a cartridge fault can
     * never move this -- which is exactly why it gets its own bit instead of
     * being folded into a "cartridge or BIOS" message. */
    if (memory_map_read[0] != bios_rom)
        f |= M10MAP_BIOS;

    for (b = 0; b < gamepak_buffer_count && b < 32u; b++)
        if (gamepak_buffers[b] == 0)
            f |= M10MAP_BUFPTR;

    /* If the geometry itself is wrong, every per-slot expectation below is
     * computed from a lie and thousands of derived failures would bury the one
     * real one. gba_m8map.c:262-265 stops for the same reason -- but note what
     * is NOT in this set: residency and paging are NOT geometry errors and do
     * NOT stop the walk. That difference is the entire milestone. */
    if (f & (M10MAP_STRIDE | M10MAP_GEOM | M10MAP_BUFPTR))
        return f;

    /* ---- PASS 1: the LRU queue defines the resident set (invariant I1) ---- */
    for (e = 0; e < entries; e++) {
        s32 q = (s32)gamepak_blk_queue[e].phy_rom;

        if (q < 0)
            continue;                               /* entry holds no page     */

        if ((u32)q >= stride) {
            f |= M10MAP_QUEUE;                      /* page outside the ROM    */
            m10map_record(M10MAP_NOPAGE, (u32)q, M10MAP_NOPAGE, 0, 0, e);
            continue;
        }
        if (m10map_bm_test(m10map_page_bm, (u32)q)) {
            f |= M10MAP_QUEUE;                      /* two entries, one page   */
            m10map_record(M10MAP_NOPAGE, (u32)q, M10MAP_NOPAGE, 0, 0, e);
            continue;
        }
        m10map_bm_set(m10map_page_bm, (u32)q);
        resident++;
    }

    /* THE EXACT RESIDENCY INVARIANT, AND IT IS FATAL.
     *
     * This is what replaces M8MAP_PAGES. M8 asked `pages == gamepak_file_blocks`
     * -- "is the WHOLE cartridge resident" -- which a demand-paged cartridge can
     * never satisfy. M10 asks whether the cache holds EXACTLY as many pages as
     * it is able to hold. That is not a relaxation: it is an equality, it is
     * invariant across page faults, and one page too few is as fatal as one too
     * many. gamepak_must_swap() appears nowhere in it. */
    if (resident != expect)
        f |= M10MAP_RESIDENT;

    /* A broken bijection makes every slot expectation below meaningless. */
    if (f & M10MAP_QUEUE)
        return f;

    /* ---- PASS 2a: mark the write set and detect ambiguous slots ---------- */
    for (e = 0; e < entries; e++) {
        s32 q = (s32)gamepak_blk_queue[e].phy_rom;

        if (q >= 0)
            (void)m10map_walk_page((u32)q, 0, e, 0u);
    }

    /* ---- PASS 2b: every resident page's slots hold its EXACT pointer ----- */
    for (e = 0; e < entries; e++) {
        s32 q = (s32)gamepak_blk_queue[e].phy_rom;
        u8 *want;

        if (q < 0)
            continue;

        want = m10map_entry_ptr(e);
        if (want == 0) {
            f |= M10MAP_BUFPTR;
            m10map_record(M10MAP_NOPAGE, (u32)q, M10MAP_NOPAGE, 0, 0, e);
            continue;
        }
        f |= m10map_walk_page((u32)q, want, e, 1u);
    }

    /* ---- PASS 3: everything else is NULL, and nothing is alien (I2, I3) --
     *
     * THIS IS THE ASSERTION THAT MAKES DEMAND PAGING PROVABLE RATHER THAN
     * ASSUMED. A slot no resident page writes MUST be NULL, because NULL is
     * what cpu.cc:869 tests to raise the page fault that calls
     * load_gamepak_page(). A non-NULL slot there is a page that can never be
     * faulted in and would be read as stale data forever.
     *
     * NOTE THE POLARITY CAREFULLY. The instinct -- and M8's contract -- is that
     * a NULL cartridge slot is a broken mapping. For a partially resident
     * cartridge the OPPOSITE is true, and getting this backwards is precisely
     * the defect that produced -262. */
    for (slot = M10MAP_SLOT_LO; slot <= M10MAP_SLOT_HI; slot++) {
        u8 *actual = memory_map_read[slot];
        u32 idx    = slot - M10MAP_SLOT_LO;

        if (actual == 0) {
            m10map_null_slots++;
        } else {
            u32 ent = m10map_entry_of(actual);

            m10map_mapped_slots++;

            if (ent == M10MAP_ALIENPAGE) {
                f |= M10MAP_ALIEN;
                m10map_record(slot, M10MAP_NOPAGE, M10MAP_ALIENPAGE,
                              0, actual, M10MAP_NOPAGE);
            } else if (ent == M10MAP_OOBPAGE) {
                f |= M10MAP_OOB;
                m10map_record(slot, M10MAP_NOPAGE, M10MAP_OOBPAGE,
                              0, actual, M10MAP_NOPAGE);
            }
        }

        if (m10map_bm_test(m10map_slot_bm, idx))
            continue;                   /* owned by a resident page -- PASS 2b
                                           already asserted its exact value    */

        if (actual != 0) {
            f |= M10MAP_FAULTSLOT;
            if (slot < M10MAP_IDX_08 + M10MAP_WIN_08)
                f |= M10MAP_EXECWIN;
            m10map_record(slot, M10MAP_NOPAGE, m10map_page_of(actual),
                          0, actual, M10MAP_NOPAGE);
        }
    }

    return f;
}

/* REPORT-ONLY. Call AFTER m10map_probe(): M10MAP_R_AMBIG reflects the slot
 * collisions that probe recorded. Every other bit is computed live. */
unsigned int m10map_report(void)
{
    unsigned int r = 0;
    u32 i;

    m10map_sticky_or = 0;
    for (i = 0; i < M10MAP_PAGE_WORDS; i++)
        m10map_sticky_or |= gamepak_sticky_bit[i];

    if (gamepak_buffer_count == 0 || gamepak_size == 0)
        return 0;

    if (m10map_expected_resident() < gamepak_file_blocks)
        r |= M10MAP_R_PARTIAL;

    if (m10map_ambig_slots != 0u)
        r |= M10MAP_R_AMBIG;

    /* NO CODE PAGE IS PINNED. touch_gamepak_page() (gba_memory.h:324) exists to
     * stop the interpreter's own code page being evicted under it, and NOTHING
     * IN THE M10 LINK SET CALLS IT -- the only caller of the matching
     * clear_gamepak_stickybits() is libretro.c:1455, which M10 deliberately does
     * not link (Makefile:4763-4774). Reported by name because it is a real
     * property of this build; NOT fixed, because gpsp/ is frozen. */
    if (m10map_sticky_or == 0u && gamepak_must_swap())
        r |= M10MAP_R_NOSTICKY;

    if (gamepak_file_blocks != m10map_stride())
        r |= M10MAP_R_MIRROR;

    if (memory_map_read[M10MAP_IDX_0D] != 0)
        r |= M10MAP_R_0DTAIL;

    return r;
}

/* ------------------------------------------------- the page-load measurement */

void m10map_paging_reset(void)
{
    u32 n = m10map_entries();
    u32 e;

    for (e = 0; e < n; e++)
        m10map_snap[e] = gamepak_blk_queue[e].phy_rom;

    m10map_snap_n     = n;
    m10map_lru_prev   = (n != 0u) ? ((u32)gamepak_lru_head % n) : 0u;
    m10map_lru_evicts = 0;
    m10map_page_loads = 0;
}

unsigned int m10map_paging_delta(void)
{
    u32 n = m10map_entries();
    u32 e, d = 0, head;

    if (n == 0u)
        return 0u;

    /* The buffer count cannot change after init_gamepak_buffer (its only other
     * writer is memory_term), so this can only fire if the baseline was never
     * taken. Re-baseline rather than compare against uninitialised state. */
    if (m10map_snap_n != n) {
        m10map_paging_reset();
        return 0u;
    }

    for (e = 0; e < n; e++) {
        s16 cur = gamepak_blk_queue[e].phy_rom;

        if (cur != m10map_snap[e]) {
            d++;
            m10map_snap[e] = cur;
        }
    }

    /* THE INDEPENDENT CROSS-CHECK. evict_gamepak_page() pops the head and
     * appends the victim at the tail (gba_memory.c:2297-2306), and because
     * init_gamepak_buffer lays the ring out as 0 -> 1 -> ... -> n-1 and the
     * first eviction closes it with next_lru[n-1] = 0, the head advances by
     * exactly one per eviction, modulo n. Sticky pages could make it skip, but
     * nothing in the M10 link set ever sets a sticky bit -- see
     * M10MAP_R_NOSTICKY -- so this counts evictions exactly, modulo n. */
    head = (u32)gamepak_lru_head % n;
    m10map_lru_evicts += (head + n - m10map_lru_prev) % n;
    m10map_lru_prev = head;

    m10map_page_loads += d;
    return d;
}

unsigned int m10map_read(unsigned int sel)
{
    switch (sel) {
    case M10MAP_RD_SIZE:         return gamepak_size;
    case M10MAP_RD_FILEBLOCKS:   return gamepak_file_blocks;
    case M10MAP_RD_STRIDE:       return m10map_stride();
    case M10MAP_RD_RESIDENT:     return m10map_resident_count();
    case M10MAP_RD_EXPRESIDENT:  return m10map_expected_resident();
    case M10MAP_RD_BUFCOUNT:     return gamepak_buffer_count;
    case M10MAP_RD_BLOCKSIZE:    return (unsigned int)gamepak_buffer_blocksize;
    case M10MAP_RD_MUSTSWAP:     return gamepak_must_swap() ? 1u : 0u;
    case M10MAP_RD_ENTRIES:      return m10map_entries();
    case M10MAP_RD_BADSLOT:      return m10map_bad_slot;
    case M10MAP_RD_EXP_PAGE:     return m10map_bad_exp;
    case M10MAP_RD_ACT_PAGE:     return m10map_bad_act;
    case M10MAP_RD_EXP_LOW:      return m10map_bad_exp_l;
    case M10MAP_RD_ACT_LOW:      return m10map_bad_act_l;
    case M10MAP_RD_BADENTRY:     return m10map_bad_entry;
    case M10MAP_RD_MAPPEDSLOTS:  return m10map_mapped_slots;
    case M10MAP_RD_NULLSLOTS:    return m10map_null_slots;
    case M10MAP_RD_AMBIGSLOTS:   return m10map_ambig_slots;
    case M10MAP_RD_LRUHEAD:      return (unsigned int)gamepak_lru_head;
    case M10MAP_RD_LRUEVICTS:    return m10map_lru_evicts;
    case M10MAP_RD_PAGELOADS:    return m10map_page_loads;
    case M10MAP_RD_STICKY:       return m10map_sticky_or;
    case M10MAP_RD_MAP08_LOW:
        return (u32)(u64)memory_map_read[M10MAP_IDX_08];
    default:                     return 0u;
    }
}
