/* LUAport M8 -- adapters/gba/gba_m8map.h
 *
 * THE M8-OWNED, ROM-SIZE-AWARE CARTRIDGE MAPPING PROBE.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * adapters/gba/gba_rom.{c,h} are FROZEN M4 evidence. m4_rom_probe_map() encodes
 * a mapping contract that is CORRECT for a ROM whose 32 KB page count is a
 * POWER OF TWO and WRONG for any other. Three of its six bits
 *
 *     M4_MAP_ROM0A   memory_map_read[5120] != gamepak_buffers[0]
 *     M4_MAP_ROM0C   memory_map_read[6144] != gamepak_buffers[0]
 *     M4_MAP_EEPROM  memory_map_read[6656] != NULL
 *
 * are statements about ONE RESIDENT PAGE dressed up as statements about the
 * mapping. M4 and M5-M7 ran mgba/cinema/gba/obj/2d-wrap/test.gba -- 4,096 bytes,
 * ONE page -- so all three held. M8 runs mgba/cinema/gba/irq/keyirq/test.gba --
 * 69,160 bytes, THREE pages -- and all three become false ON CORRECT CODE.
 *
 * gba_rom.c IS NOT EDITED. M4 through M7 are hardware-verified milestones that
 * link that object; changing it would invalidate four green results to fix a
 * probe that only M8 has a ROM shaped wrongly for. This file is ADDITIVE, is
 * linked ONLY into the M8 image, and is STRICTLY STRONGER than the three bits it
 * displaces -- see "WHAT THIS PROVES" below.
 *
 * THE TYPE RULE, UNCHANGED SINCE M2
 * ---------------------------------
 * gpsp/common.h:100 typedefs u64 as `unsigned long long`; runtime/core.h:30
 * typedefs it as `unsigned long`. Both are 64 bits and they are DISTINCT TYPES,
 * so a translation unit including both fails on the duplicate typedef.
 * apps/m8gpsp/main.c therefore never includes a gpSP header, and every
 * declaration below uses ONLY `unsigned int` -- `u32` in both worlds. NO POINTER
 * AND NOTHING 64-BIT CROSSES THIS BOUNDARY: where a pointer has to be reported
 * it is reported as a PAGE NUMBER, and additionally as the LOW 32 BITS of the
 * address, which is the convention adapters/gba/gba_exec.h:397 already
 * established with M5_RD_MAP08_LOW.
 *
 * WHY NOT PUT THIS IN gba_input.c
 * -------------------------------
 * gba_input.{c,h} is M8-owned and would have compiled. It is the WRONG HOME:
 * Makefile:3280-3295 pins that object's identity -- it must reference
 * io_registers and must carry NO plat_*, scePad*, or sceVideoOut* relocation.
 * Makefile:2836 documents it as "the DualSense -> GBA keypad" translation unit.
 * Adding memory_map_read/gamepak_buffers to it would pass those checks
 * mechanically while destroying the single-purpose claim they exist to defend.
 * One adapter, one gpSP surface, is the rule M2-M7 established.
 *
 * ============================================================================
 * WHAT gpSP ACTUALLY DOES, TRACED READ-ONLY BEFORE ANY OF THIS WAS WRITTEN
 * ============================================================================
 *
 * load_gamepak_raw (gba_memory.c:2671-2794):
 *
 *     raw_size            = (file_size + 0x7FFF) & ~0x7FFF        :2701
 *     gamepak_file_blocks = raw_size >> 15                        :2702
 *     gamepak_size        = raw_size            (mirror_1m false) :2704
 *     buf_blocks          = ceil(raw_size / gamepak_buffer_blocksize)  :2754
 *     rom_blocks          = gamepak_size >> 15                    :2755
 *     ldblks              = min(buf_blocks, gamepak_buffer_count) :2756
 *     map_null(read, 0x8000000, 0xD000000)                        :2771
 *     for i < ldblks:  read 1 MB into gamepak_buffers[i]          :2776
 *       for j < 32 && i*32+j < gamepak_file_blocks:               :2782
 *         phyn   = i*32 + j
 *         blkptr = &gamepak_buffers[i][32768 * j]
 *         map_rom_entry(read, phyn, blkptr, rom_blocks)           :2788
 *
 * map_rom_entry (gba_memory.c:2254-2263) is:
 *
 *     for (mcount = 0; mcount < 1024; mcount += mirror_blocks) {
 *         memory_map_read[4096 + idx + mcount] = ptr;
 *         memory_map_read[5120 + idx + mcount] = ptr;
 *     }
 *     for (mcount = 0; mcount <  512; mcount += mirror_blocks) {
 *         memory_map_read[6144 + idx + mcount] = ptr;
 *     }
 *
 * THE DEFECT THIS PROBE HAD TO BE BUILT AROUND. The loop bounds test `mcount`
 * but the write index is `idx + mcount`, so for any idx >= 1 the loops RUN PAST
 * THE END of the window they name. With mirror_blocks == B, the largest mcount
 * reached is the largest multiple of B below 1024 (resp. 512). When B is a power
 * of two that is 1024 - B (resp. 512 - B) and `idx + mcount <= 1023` (resp.
 * <= 511) for every idx < B, so nothing overruns and every window starts at page
 * 0. WHEN B IS NOT A POWER OF TWO IT DOES OVERRUN.
 *
 * For KEYIRQ, B = 3: 1023 = 3*341 and 510 = 3*170 are both reached, so
 *
 *     phyn 1, 0x08 loop, mcount 1023  ->  writes slot 4096+1+1023 = 5120 (0x0A)
 *     phyn 1, 0x0A loop, mcount 1023  ->  writes slot 5120+1+1023 = 6144 (0x0C)
 *     phyn 2, 0x0C loop, mcount  510  ->  writes slot 6144+2+ 510 = 6656 (0x0D)
 *
 * and because the j loop runs phyn ASCENDING, those late writes WIN. That is
 * exactly the 0x16 mask (bits 1, 2, 4) the console reported, and it is a REAL
 * gpSP BEHAVIOUR DIFFERENCE from GBA hardware -- not a probe artefact. It is
 * REPORTED BY NAME here (see m8map_report) and is NOT called correct mirroring.
 *
 * THE 0x08000000 EXECUTION WINDOW IS UNAFFECTED AND IS PROVED SO. Slot 4096+o
 * for o in [0,1024) can only be written by the 0x08 loop (the 0x0A and 0x0C
 * loops start at 5120 and 6144 and only ever increase), and within it exactly
 * one phyn satisfies `phyn = o mod B` with `mcount = o - phyn <= 1023`. So
 *
 *     memory_map_read[4096 + o] == page (o mod B)
 *
 * for EVERY o, with no gap and no NULL. That is the window the CPU fetches from,
 * and M8 asserts all 1024 entries of it FATALLY.
 *
 * ============================================================================
 * WHAT THIS PROVES -- THREE LAYERS
 * ============================================================================
 *
 * L1  REPLAY, FATAL (m8map_probe, M8MAP_REPLAY / M8MAP_ALIEN).
 *     Every slot from 4096 through 6656 INCLUSIVE -- 2,561 of them, the whole
 *     range map_rom_entry can reach -- is compared against a re-execution of the
 *     loops above for the OBSERVED gamepak_size and buffer layout. Nothing is
 *     hard-coded and nothing is waved through: a slot that gpSP's own algorithm
 *     does not write MUST be NULL, and one it does write MUST hold that exact
 *     page pointer. This is why the displaced M4_MAP_EEPROM is a NARROWING and
 *     not a LOSS -- 0x0D is still asserted, against a DERIVED value instead of
 *     against the literal NULL that only holds for a power-of-two page count.
 *
 * L2  HARDWARE-IDEAL DELTA, REPORT ONLY (m8map_report).
 *     The same replay is compared against what the three windows would hold if
 *     each mirrored the cartridge from page 0 -- which is what map_rom_entry is
 *     evidently TRYING to do and what a GBA does. Divergence is REPORTED with a
 *     named bit and a slot count. It does not fail M8, because the alternate
 *     windows are not on this cartridge's execution path; it is not hidden,
 *     because pretending gpSP is hardware-exact here would be false.
 *
 * L3  THE EXECUTION WINDOW, FATAL (M8MAP_EXEC_WINDOW / M8MAP_EXEC_NULL).
 *     Slots 4096..5119 are additionally required to match the HARDWARE-IDEAL
 *     value, not merely the replay. The two provably coincide there, so this bit
 *     can only fire if gpSP's model and hardware disagree about the window M8
 *     actually executes from -- which would be a genuine defect and must stay
 *     fatal.
 *
 * NOTHING HERE IS SPECIFIC TO 69,160 BYTES OR TO THREE PAGES. Every expected
 * value is computed from gamepak_size, gamepak_file_blocks, gamepak_buffer_count
 * and gamepak_buffer_blocksize as read out of gpSP at the moment of the call.
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py:108-111 matches the bare
 * substrings "path_", "retro_", "vfs_", "filestream_", "_stub" and "emit_"
 * against the lowercased symbol name. Every symbol below is m8map_* and contains
 * none of them. NO ALLOWLIST IS WIDENED FOR THIS FILE.
 *
 * THE gpsp TREE IS NOT MODIFIED BY ANY OF THIS.
 */

#ifndef LUAPORT_GBA_M8MAP_H
#define LUAPORT_GBA_M8MAP_H

/* ------------------------------------------------------------ the geometry --
 *
 * memory_map_read[] is bucketed in 32 KB units by map_region/map_null/
 * map_rom_entry (gba_memory.c:2254-2288). */
#define M8MAP_PAGE        32768u

#define M8MAP_IDX_08       4096u   /* 0x08000000, 1024 slots                   */
#define M8MAP_IDX_0A       5120u   /* 0x0A000000, 1024 slots                   */
#define M8MAP_IDX_0C       6144u   /* 0x0C000000,  512 slots                   */
#define M8MAP_IDX_0D       6656u   /* 0x0D000000, the EEPROM window            */

#define M8MAP_WIN_08       1024u
#define M8MAP_WIN_0A       1024u
#define M8MAP_WIN_0C        512u

/* The inclusive slot range map_rom_entry can reach. The HIGH bound is
 * M8MAP_IDX_0D and not M8MAP_IDX_0C + 511: the 0x0C loop's own overrun can write
 * slot 6144 + idx + mcount as high as 6656, which is precisely the EEPROM slot,
 * and a probe that stopped at 6655 would not see it. */
#define M8MAP_SLOT_LO      M8MAP_IDX_08
#define M8MAP_SLOT_HI      M8MAP_IDX_0D
#define M8MAP_SLOTS        (M8MAP_SLOT_HI - M8MAP_SLOT_LO + 1u)   /* 2561 */

/* Returned by the page accessors below. NOPAGE means "this slot is NULL", which
 * is a legitimate expected value; ALIEN means "non-NULL but not the base of any
 * 32 KB page of any allocated gamepak buffer", which never is. */
#define M8MAP_NOPAGE      0xFFFFFFFFu
#define M8MAP_ALIENPAGE   0xFFFFFFFEu

/* ------------------------------------------------- FATAL bits, m8map_probe --
 *
 * A SET BIT IS A FAILED ASSERTION, identical to M2-M5, so success is a single
 * `== 0` test and a newly added assertion cannot read as "pass". */
#define M8MAP_NO_ROM       (1u << 0)  /* no buffer / no resident cartridge     */
#define M8MAP_STRIDE       (1u << 1)  /* gamepak_size >> 15 is 0 or > 1024     */
#define M8MAP_PAGES        (1u << 2)  /* mapped page count != file_blocks      */
#define M8MAP_SWAP         (1u << 3)  /* gamepak_must_swap() -- demand paging  */
#define M8MAP_EXEC_NULL    (1u << 4)  /* a 0x08 window slot is NULL            */
#define M8MAP_EXEC_WINDOW  (1u << 5)  /* a 0x08 slot != hardware-ideal page    */
#define M8MAP_REPLAY       (1u << 6)  /* a slot != gpSP's own algorithm        */
#define M8MAP_ALIEN        (1u << 7)  /* a slot holds a non-page pointer       */
#define M8MAP_BIOS         (1u << 8)  /* memory_map_read[0] != bios_rom        */

/* ------------------------------------------- REPORT-ONLY bits, m8map_report --
 *
 * These describe gpSP's DIVERGENCE FROM HARDWARE, which is a property of the
 * emulator and of the ROM's page count -- never of LUAport. They are printed and
 * published, and they DO NOT FAIL M8.
 *
 * THEY ARE NOT A LICENCE FOR AN ARBITRARY MAPPING. Both are computed from the
 * REPLAY, and m8map_probe() has already required memory to equal that replay
 * exactly and fatally. A 0x0D that is non-NULL for any reason OTHER than the
 * derived overrun therefore still fails, through M8MAP_REPLAY. */
#define M8MAP_R_ALT_STRIDE (1u << 0)  /* 0x0A/0x0C mirror start is shifted     */
#define M8MAP_R_EEPROM     (1u << 1)  /* 0x0D carries the derived 0x0C overrun */

/* --------------------------------------------------------------- the calls -- */

/* THE FATAL PROBE. Walks all 2,561 slots and returns a mask of M8MAP_* bits;
 * 0 means the cartridge mapping is exactly what gpSP's source says it must be
 * AND the execution window is hardware-correct. Records the first failing slot
 * for the selectors below. */
unsigned int m8map_probe(void);

/* THE REPORT-ONLY COMPARISON against ideal GBA mirroring. Returns a mask of
 * M8MAP_R_* bits. Never fatal. Safe to call when m8map_probe() failed -- it
 * returns 0 if there is nothing coherent to compare. */
unsigned int m8map_report(void);

/* One raw value at a time, for the log and for ext->dbg[]. Returns 0 for an
 * unknown selector. */
unsigned int m8map_read(unsigned int sel);

/* The expected 32 KB page count for a given TRUE on-disk file size, derived the
 * same way gba_memory.c:2701-2702 derives it. This is what replaces the literal
 * `gamepak_file_blocks == 1` M8 inherited: for OBJTEST it still evaluates to 1,
 * for KEYIRQ it evaluates to 3, and for anything else it is right too. */
unsigned int m8map_expected_blocks(unsigned int file_size);

/* ------------------------------------------------------ m8map_read selectors */
#define M8MAP_RD_SIZE         0u  /* gamepak_size                              */
#define M8MAP_RD_FILEBLOCKS   1u  /* gamepak_file_blocks                       */
#define M8MAP_RD_STRIDE       2u  /* gamepak_size >> 15 -- map_rom_entry's arg */
#define M8MAP_RD_PAGES        3u  /* pages the load loop actually mapped       */
#define M8MAP_RD_BUFCOUNT     4u  /* gamepak_buffer_count                      */
#define M8MAP_RD_BLOCKSIZE    5u  /* gamepak_buffer_blocksize                  */
#define M8MAP_RD_MUSTSWAP     6u  /* gamepak_must_swap()                       */
#define M8MAP_RD_BADSLOT      7u  /* first failing slot, or M8MAP_NOPAGE       */
#define M8MAP_RD_EXP_PAGE     8u  /* page expected there                       */
#define M8MAP_RD_ACT_PAGE     9u  /* page actually there                       */
#define M8MAP_RD_EXP_LOW     10u  /* low 32 bits of the expected pointer       */
#define M8MAP_RD_ACT_LOW     11u  /* low 32 bits of the actual pointer         */
#define M8MAP_RD_ALT_SLOTS   12u  /* how many 0x0A/0x0C slots differ from ideal*/
#define M8MAP_RD_ALT_FIRST   13u  /* the first such slot, or M8MAP_NOPAGE      */
#define M8MAP_RD_EEPROM_PAGE 14u  /* page at 0x0D, or M8MAP_NOPAGE if NULL     */
#define M8MAP_RD_MAP08_LOW   15u  /* low 32 bits of memory_map_read[4096]      */

#endif /* LUAPORT_GBA_M8MAP_H */
