/* LUAport M10 -- adapters/gba/gba_m10map.h
 *
 * THE M10-OWNED, DEMAND-PAGING-AWARE CARTRIDGE MAPPING PROBE.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * adapters/gba/gba_m8map.{c,h} are FROZEN M8 evidence and are linked into M8,
 * M9, M9-D and M10. m8map_probe() encodes a FULL-RESIDENCY mapping contract:
 *
 *     M8MAP_PAGES   mapped page count != gamepak_file_blocks   (gba_m8map.c:251)
 *     M8MAP_SWAP    gamepak_must_swap() is true                (gba_m8map.c:247)
 *
 * Both are CORRECT ASSERTIONS FOR A CARTRIDGE THAT FITS IN THE PAGE CACHE and
 * both are FALSE ASSERTIONS FOR ONE THAT DOES NOT. M8/M9 ran a 69,160-byte ROM
 * (3 x 32 KB pages, 100 % resident in one 1 MB buffer) so neither could fire.
 * M10C runs an 8,388,608-byte cartridge with -DROM_BUFFER_SIZE=2, so:
 *
 *     gamepak_file_blocks       256
 *     resident pages             64      (2 buffers x 32 pages)
 *     gamepak_must_swap()      true
 *     m8map_probe()            0x0C      -> M10_ROM_UNMAPPED (-262)
 *
 * That -262 is HARDWARE-CONFIRMED and it is NOT a cartridge fault, NOT a load
 * fault and NOT a paging fault. gpSP's demand paging is working exactly as
 * written; the probe measuring it asserts a contract the cartridge was never
 * required to satisfy.
 *
 * THIS FILE IS THE CORRECT CONTRACT, AND IT IS NOT A RELAXATION.
 * ------------------------------------------------------------
 * The temptation is to mask bits 2 and 3 in m8map_probe(). That would be worse
 * than useless, and provably so: gba_m8map.c:264-265 EARLY-RETURNS on those two
 * bits BEFORE the 2,561-slot walk. Suppressing them does not "let the good walk
 * run" -- it lets a walk run that immediately raises M8MAP_EXEC_NULL on the 768
 * legitimately-NULL 0x08000000 slots of a partially resident cartridge. One
 * honest failure would become a different, dishonest one.
 *
 * So the residency question is RE-ASKED CORRECTLY here instead:
 *
 *     M8   "is every page of the cartridge resident?"        -- wrong question
 *     M10  "is the set of resident pages exactly the set the
 *           cache can hold, is every one of them mapped to
 *           its exact correct pointer, and is every page that
 *           is NOT resident in the exact state gpSP requires
 *           in order to fault it in?"                        -- right question
 *
 * NOTHING IS WAVED THROUGH. gamepak_must_swap() IS NOT A BIT AND IS NOT A PASS
 * CONDITION here. It appears in exactly one place -- choosing WHICH residency
 * count is expected -- and it can never turn a failing map into a passing one.
 *
 * ============================================================================
 * WHAT gpSP ACTUALLY DOES, TRACED READ-ONLY BEFORE ANY OF THIS WAS WRITTEN
 * ============================================================================
 *
 * THE CARTRIDGE MAP HAS EXACTLY THREE WRITERS. There is no fourth.
 *
 *   map_null(read, 0x8000000, 0xD000000)          gba_memory.c:2771
 *       slots 4096..6655 <- NULL, once, at load time.
 *
 *   map_rom_entry(read, phyn, blkptr, rom_blocks) gba_memory.c:2789  (load)
 *   map_rom_entry(read, physical_index, swap_location, rom_blocks)
 *                                                 gba_memory.c:2350  (fault in)
 *       the ten mirror slots of page phyn <- that page's pointer.
 *
 *   map_rom_entry(read, phyrom, NULL, gamepak_size >> 15)
 *                                                 gba_memory.c:2313  (evict)
 *       the ten mirror slots of page phyrom <- NULL.
 *
 * init_memory() (gba_memory.c:2393-2454) NEVER TOUCHES SLOTS 4096..7167. It
 * maps [0,0x1000000), 0x2000000..0x5000000 and VRAM, and nulls
 * 0x5000000..0x8000000 and 0xE000000..0x10000000. The cartridge window is
 * skipped entirely -- which is why reset_gba() cannot destroy the map, and why
 * the -262 site 255 has never fired on any hardware run.
 *
 * THE INVARIANT, STATED ONCE. Let
 *
 *     B    = gamepak_size >> 15                       (map_rom_entry's stride)
 *     N    = 32 * gamepak_buffer_count                (live LRU queue length)
 *     P(e) = &gamepak_buffers[e >> 5][32768 * (e & 31)]
 *
 * I1  RESIDENCY BIJECTION. For every queue entry e in [0,N) whose
 *     gamepak_blk_queue[e].phy_rom is q >= 0, all ten mirror slots of page q
 *     hold exactly P(e). Two entries never name the same q.
 *
 * I2  FAULTABILITY. For every page p in [0,B) named by NO entry, all ten of its
 *     mirror slots are NULL.
 *
 * I3  PARTITION. The three windows' 2,560 slots partition into B pages x 10
 *     slots. No slot has two writers.
 *
 * I2 IS THE WHOLE POINT AND IT IS COUNTER-INTUITIVE. NULL IS NOT AN ERROR
 * VALUE IN THE CARTRIDGE WINDOW -- IT IS THE PAGE-FAULT TRIGGER. cpu.cc:869
 * tests `!(map = memory_map_read[_address >> 15])` and cpu.cc:582-586 /
 * cpu_threaded.c:280-283 then call load_gamepak_page(pc_region & 0x3FF). A
 * legitimately non-resident page is INDISTINGUISHABLE FROM A MISSING MAPPING BY
 * POINTER VALUE ALONE. It is distinguished ONLY by cross-checking the LRU queue,
 * which is precisely why this probe reads gamepak_blk_queue[] and m8map does
 * not. Asserting non-NULL there -- the instinctive check -- would break demand
 * paging outright.
 *
 * THE RESIDENT COUNT IS AN EXACT INVARIANT, NOT A STARTUP PROPERTY.
 * load_gamepak_page() evicts exactly one entry and installs exactly one
 * (gba_memory.c:2330-2350), so
 *
 *     resident == min(gamepak_file_blocks, ldblks * 32)
 *
 * holds at EVERY instant, not merely after load_gamepak(). That is what lets
 * M10MAP_RESIDENT be FATAL rather than advisory.
 *
 * THE CONTIGUOUS PREFIX IS A STARTUP-ONLY PROPERTY AND IS NOT ASSERTED.
 * The load loop (gba_memory.c:2774-2790) runs i ascending over ldblks and j
 * ascending over 32, so it enumerates pages 0,1,...,resident-1 with no gap, and
 * because evict_gamepak_page() pops a virgin ring in order, entry == phy_rom ==
 * p at t=0. THE FIRST PAGE FAULT DESTROYS BOTH FACTS. A probe that encoded the
 * prefix would pass at startup and fail spuriously one second into Metroid, so
 * every check below is written against I1/I2/I3 and against nothing else.
 *
 * ============================================================================
 * map_rom_entry's OVERRUN, AND WHY IT MATTERS LESS HERE THAN AT M8
 * ============================================================================
 * gba_memory.c:2254-2263 tests `mcount` but writes `idx + mcount`, so the loops
 * run past the window they name whenever B is not a power of two. gba_m8map.h
 * :77-94 derives this in full; it is a REAL gpSP behaviour difference and it is
 * not restated here.
 *
 * FOR A POWER-OF-TWO B IT CANNOT HAPPEN, AND IT ADDITIONALLY MAKES THE MAP
 * UNAMBIGUOUS. B divides 1024 and 2048, so for any slot the candidate page is
 * the same in all three windows:
 *
 *     (s - 4096) mod B  ==  (s - 5120) mod B  ==  (s - 6144) mod B
 *
 * Exactly one page can therefore write any given slot, and I1/I2 are decidable
 * by inspection. Metroid's B is 256 and KEYIRQ-sized ROMs are checked by
 * m8map anyway, so this is the case that matters.
 *
 * WHEN B IS NOT A POWER OF TWO the writer of a shared slot depends on FAULT
 * ORDER, which no observer can reconstruct, and an eviction can legitimately
 * NULL a slot whose other candidate page is still resident. Rather than assert
 * something unprovable, the probe DETECTS that geometry, reports it by name
 * (M10MAP_R_AMBIG), and accepts ANY resident candidate -- or NULL -- for the
 * affected slots only. Every other check stays fatal. This is stated here
 * because a relaxation that is not written down is a lie by omission.
 *
 * ============================================================================
 * THE TYPE RULE, UNCHANGED SINCE M2
 * ============================================================================
 * gpsp/common.h:100 typedefs u64 as `unsigned long long`; runtime/core.h:30
 * typedefs it as `unsigned long`. Both are 64 bits and they are DISTINCT TYPES,
 * so a translation unit including both fails on the duplicate typedef.
 * apps/m10gpsp/main.c therefore never includes a gpSP header, and every
 * declaration below uses ONLY `unsigned int` -- u32 in both worlds. NO POINTER
 * AND NOTHING 64-BIT CROSSES THIS BOUNDARY: a pointer is reported as a PAGE
 * NUMBER and, additionally, as the LOW 32 BITS of its address -- the convention
 * adapters/gba/gba_exec.h:397 established with M5_RD_MAP08_LOW and that
 * gba_m8map.h:229-246 already follows.
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py:108-111 matches the bare
 * substrings "path_", "retro_", "vfs_", "filestream_", "_stub" and "emit_"
 * against the lowercased symbol name. Every symbol below is m10map_* and
 * contains none of them. NO ALLOWLIST IS WIDENED FOR THIS FILE.
 *
 * THIS FILE IS LINKED INTO M10 ONLY. adapters/gba/gba_m8map.{c,h} ARE NOT
 * EDITED, gpsp/ IS NOT MODIFIED, runtime/ IS NOT MODIFIED, and M0-M9 gain
 * nothing and lose nothing. m10-verify proves all four.
 */

#ifndef LUAPORT_GBA_M10MAP_H
#define LUAPORT_GBA_M10MAP_H

/* ------------------------------------------------------------ the geometry --
 *
 * memory_map_read[] is bucketed in 32 KB units by map_region/map_null/
 * map_rom_entry (gba_memory.c:2254-2288). Identical to gba_m8map.h:154-171 --
 * restated rather than included so this adapter does not depend on the FROZEN
 * M8 header, and asserted equal to it by apps/m10gpsp/main.c at run time. */
#define M10MAP_PAGE        32768u

#define M10MAP_IDX_08       4096u   /* 0x08000000, 1024 slots -- THE FETCH PATH */
#define M10MAP_IDX_0A       5120u   /* 0x0A000000, 1024 slots                   */
#define M10MAP_IDX_0C       6144u   /* 0x0C000000,  512 slots                   */
#define M10MAP_IDX_0D       6656u   /* 0x0D000000, the EEPROM window            */

#define M10MAP_WIN_08       1024u
#define M10MAP_WIN_0A       1024u
#define M10MAP_WIN_0C        512u

/* THE INCLUSIVE SLOT RANGE map_rom_entry CAN REACH, DERIVED RATHER THAN
 * ASSUMED. gba_m8map.h:169-171 stops at 6656 because that is the maximum its
 * two cartridges (B = 1 and B = 3) can reach. THAT BOUND IS NOT GENERAL. The
 * 0x0C loop writes 6144 + idx + mcount with idx <= B-1 <= 1023 and mcount <=
 * 511, so it can reach 6144 + 1023 + 511 = 7678; the 0x0A loop can reach
 * 5120 + (B-1) + (1024-1) <= 7166 and the 0x08 loop <= 6142.
 *
 * Walking to 7678 is STRICTLY STRONGER and cannot produce a false failure.
 * init_memory() (gba_memory.c:2409-2419) maps [0,0x1000000), 0x2000000..
 * 0x5000000 and VRAM and nulls 0x5000000..0x8000000 and 0xE000000..0x10000000
 * -- it NEVER TOUCHES slots 4096..7167. So slots 6656..7167 are NULL from .bss
 * zero-initialisation, slots 7168..7678 are NULL from that last map_null, and
 * anything non-NULL up there is a genuine map_rom_entry overrun that a probe
 * stopping at 6656 would not see.
 *
 * For Metroid (B = 256, a power of two) the true write set is exactly
 * 4096..6655 and every slot above it is asserted NULL. */
#define M10MAP_SLOT_LO      M10MAP_IDX_08
#define M10MAP_SLOT_HI      7678u
#define M10MAP_SLOTS        (M10MAP_SLOT_HI - M10MAP_SLOT_LO + 1u)   /* 3583 */

/* gamepak_blk_queue[] is 1024 entries (gba_memory.c:384) and the sticky bitmap
 * is 1024 bits (gba_memory.c:391), so 1024 bounds both the queue walk and the
 * page number space. gamepak_size >> 15 is additionally capped at 1024 because
 * map_rom_entry's 0x08 loop never executes for a larger stride. */
#define M10MAP_MAXENTRIES   1024u
#define M10MAP_MAXPAGES     1024u

/* Returned by the page accessors. NOPAGE means "this slot is NULL", which in
 * the cartridge window is a LEGITIMATE EXPECTED VALUE and never an error by
 * itself. ALIENPAGE means "non-NULL, inside an allocated buffer, but not on a
 * 32 KB page boundary". OOBPAGE means "non-NULL and inside no allocated buffer
 * at all". The last two are never legitimate. */
#define M10MAP_NOPAGE      0xFFFFFFFFu
#define M10MAP_ALIENPAGE   0xFFFFFFFEu
#define M10MAP_OOBPAGE     0xFFFFFFFDu

/* ------------------------------------------------ FATAL bits, m10map_probe --
 *
 * A SET BIT IS A FAILED ASSERTION, identical to M2-M8, so success is a single
 * `== 0` test and a newly added assertion cannot read as "pass".
 *
 * THERE IS NO "SWAP" BIT AND THERE WILL NEVER BE ONE. Demand paging is a
 * PROPERTY OF THE CARTRIDGE AND THE CACHE, not a defect. It is reported through
 * M10MAP_RD_MUSTSWAP and M10MAP_R_PARTIAL and it fails nothing. */
#define M10MAP_NO_ROM      (1u << 0)  /* no buffer / no cartridge loaded        */
#define M10MAP_STRIDE      (1u << 1)  /* gamepak_size >> 15 is 0 or > 1024      */
#define M10MAP_GEOM        (1u << 2)  /* size not 32 KB-aligned, or file_blocks
                                         is 0 or exceeds the stride             */
#define M10MAP_RESIDENT    (1u << 3)  /* resident count != min(file_blocks,
                                         ldblks * 32) -- THE EXACT INVARIANT    */
#define M10MAP_BUFPTR      (1u << 4)  /* gamepak_buffers[b] NULL for b<bufcount */
#define M10MAP_QUEUE       (1u << 5)  /* phy_rom >= stride, or two entries claim
                                         the same page -- I1's bijection broke  */
#define M10MAP_RESPTR      (1u << 6)  /* a RESIDENT page's slot != its exact
                                         &buffers[e>>5][32768*(e&31)]           */
#define M10MAP_FAULTSLOT   (1u << 7)  /* a NON-RESIDENT page's slot is NOT NULL
                                         -- it can never be faulted in          */
#define M10MAP_ALIEN       (1u << 8)  /* a slot holds a non-page-base pointer   */
#define M10MAP_OOB         (1u << 9)  /* a slot points outside every buffer     */
#define M10MAP_EXECWIN    (1u << 10)  /* the failing slot is in 0x08000000 --
                                         the window the CPU FETCHES from        */
#define M10MAP_BIOS       (1u << 11)  /* memory_map_read[0] != bios_rom         */

/* ------------------------------------------ REPORT-ONLY bits, m10map_report --
 *
 * These describe the CARTRIDGE'S SHAPE or gpSP's divergence from hardware.
 * They are printed and published and THEY DO NOT FAIL M10. */
#define M10MAP_R_PARTIAL   (1u << 0)  /* fewer pages resident than the cartridge
                                         has -- demand paging is LIVE           */
#define M10MAP_R_AMBIG     (1u << 1)  /* non-power-of-two stride: some slot has
                                         more than one resident candidate page  */
#define M10MAP_R_NOSTICKY  (1u << 2)  /* gamepak_sticky_bit[] is entirely zero
                                         while paging is live -- NO code page is
                                         pinned during interpretation. gpSP
                                         defines touch_gamepak_page()
                                         (gba_memory.h:324) and NOTHING IN THE
                                         M10 LINK SET CALLS IT; the only caller
                                         of the matching clear is
                                         libretro.c:1455, which M10 does not
                                         link. Reported, not fixed: gpsp/ is
                                         frozen.                                */
#define M10MAP_R_MIRROR    (1u << 3)  /* gamepak_file_blocks != stride -- the
                                         Classic-NES 1 MB mirror geometry       */
#define M10MAP_R_0DTAIL    (1u << 4)  /* slot 0x0D000000 is non-NULL -- the
                                         derived 0x0C overrun reached it        */

/* --------------------------------------------------------------- the calls -- */

/* THE FATAL PROBE. Walks the LRU queue and then all 2,561 reachable slots, and
 * returns a mask of M10MAP_* bits; 0 means the cartridge map satisfies I1, I2
 * and I3 exactly. Valid for a FULLY RESIDENT cartridge too -- that is the
 * degenerate case in which I2's page set is empty -- so it can be run on the
 * KEYIRQ path for cross-validation. Records the first failing slot and entry
 * for the selectors below. */
unsigned int m10map_probe(void);

/* THE REPORT-ONLY OBSERVATIONS. Returns a mask of M10MAP_R_* bits. Never fatal.
 * Safe to call when m10map_probe() failed. */
unsigned int m10map_report(void);

/* One raw value at a time, for the log and for ext->dbg[]. Returns 0 for an
 * unknown selector. */
unsigned int m10map_read(unsigned int sel);

/* The resident page count the CURRENT cache MUST hold, derived exactly as
 * load_gamepak_raw does it (gba_memory.c:2754-2756 then the j-loop bound at
 * :2782): min(gamepak_file_blocks, min(ceil(size/blocksize), bufcount) * 32).
 * INVARIANT ACROSS PAGE FAULTS -- see the header note above. */
unsigned int m10map_expected_resident(void);

/* ---------------------------------------------- THE PAGE-LOAD MEASUREMENT --
 *
 * WHY THIS IS HERE AND NOT IN THE FIXTURE. M10 counted page loads by watching
 * gamepak_buffer_count for a change (apps/m10gpsp/main.c, pre-M10C). THAT
 * COUNTER CANNOT WORK: load_gamepak_page() (gba_memory.c:2319-2357) never
 * assigns gamepak_buffer_count. Its only writers are init_gamepak_buffer()
 * (:2363, :2369) and memory_term() (:2464-2466). The old counter therefore
 * reads 0 for ANY workload, M10_L_NOPAGING latches unconditionally, and the
 * screen prints "ROM PAGE LOADS 0" for a cartridge that is paging hard -- a
 * FALSE NEGATIVE THAT LOOKS LIKE A PASS.
 *
 * WHAT REPLACES IT. Every fault writes gamepak_blk_queue[entry].phy_rom
 * (gba_memory.c:2336). Snapshot that vector, and the number of entries whose
 * phy_rom changed since the last sample IS the number of page loads. The
 * fixture samples once per frame.
 *
 * ITS ONE STATED LIMIT. If a single sampling interval saw more than N faults
 * AND an entry was reused within it, the count is a LOWER BOUND. At N = 64 that
 * needs 64 synchronous 32 KB reads inside one 16 ms frame; it is reported as a
 * lower bound rather than claimed exact, and M10MAP_RD_LRUEVICTS carries the
 * independent LRU-head-derived count as a cross-check. */
void m10map_paging_reset(void);
unsigned int m10map_paging_delta(void);

/* ----------------------------------------------------- m10map_read selectors */
#define M10MAP_RD_SIZE          0u  /* gamepak_size                             */
#define M10MAP_RD_FILEBLOCKS    1u  /* gamepak_file_blocks                      */
#define M10MAP_RD_STRIDE        2u  /* gamepak_size >> 15 -- map_rom_entry's arg*/
#define M10MAP_RD_RESIDENT      3u  /* pages the LRU queue actually names       */
#define M10MAP_RD_EXPRESIDENT   4u  /* m10map_expected_resident()               */
#define M10MAP_RD_BUFCOUNT      5u  /* gamepak_buffer_count                     */
#define M10MAP_RD_BLOCKSIZE     6u  /* gamepak_buffer_blocksize                 */
#define M10MAP_RD_MUSTSWAP      7u  /* gamepak_must_swap() -- REPORTED, not a
                                       failure and not a pass condition        */
#define M10MAP_RD_ENTRIES       8u  /* 32 * gamepak_buffer_count                */
#define M10MAP_RD_BADSLOT       9u  /* first failing slot, or M10MAP_NOPAGE     */
#define M10MAP_RD_EXP_PAGE     10u  /* page expected there                      */
#define M10MAP_RD_ACT_PAGE     11u  /* page actually there                      */
#define M10MAP_RD_EXP_LOW      12u  /* low 32 bits of the expected pointer      */
#define M10MAP_RD_ACT_LOW      13u  /* low 32 bits of the actual pointer        */
#define M10MAP_RD_BADENTRY     14u  /* first failing queue entry, or NOPAGE     */
#define M10MAP_RD_MAPPEDSLOTS  15u  /* non-NULL slots in 4096..6656             */
#define M10MAP_RD_NULLSLOTS    16u  /* NULL slots in 4096..6656                 */
#define M10MAP_RD_AMBIGSLOTS   17u  /* slots with >1 resident candidate page    */
#define M10MAP_RD_LRUHEAD      18u  /* gamepak_lru_head                         */
#define M10MAP_RD_LRUEVICTS    19u  /* LRU-head-derived eviction count          */
#define M10MAP_RD_PAGELOADS    20u  /* queue-vector-derived page-load count     */
#define M10MAP_RD_STICKY       21u  /* OR of gamepak_sticky_bit[] -- 0 means no
                                       page is pinned; see M10MAP_R_NOSTICKY    */
#define M10MAP_RD_MAP08_LOW    22u  /* low 32 bits of memory_map_read[4096]     */

#endif /* LUAPORT_GBA_M10MAP_H */
