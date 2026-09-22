/* LUAport M5 -- adapters/gba/gba_exec.h
 *
 * The NARROW, TYPE-SAFE BOUNDARY between the M5 fixture and gpSP's ARM7TDMI
 * INTERPRETER -- the first LUAport milestone permitted to execute a GBA
 * instruction.
 *
 * WHY THIS HEADER EXISTS AT ALL
 * -----------------------------
 * Exactly the reason adapters/gba/gba_probe.h, gba_bios.h and gba_rom.h exist,
 * and the rule is unchanged at M5: gpsp/common.h:100 typedefs u64 as
 * `unsigned long long` while runtime/core.h:30 typedefs it as `unsigned long`.
 * Both are 64 bits on this ABI but they are DISTINCT TYPES in C, so any
 * translation unit including both fails on the duplicate typedef.
 *
 * apps/m5gpsp/main.c therefore never includes a gpSP header. All gpSP contact
 * lives in adapters/gba/gba_exec.c, which includes gpsp/common.h and NOTHING
 * from runtime/, and is reached only through this file. Every declaration below
 * uses ONLY `unsigned int` and `int`, which are the same types in both worlds.
 *
 * =============================================================================
 * THE FINDING THAT SHAPES THIS ENTIRE MILESTONE
 * =============================================================================
 * execute_arm(cycles) DOES NOT RETURN WHEN THE CYCLE BUDGET IS EXHAUSTED.
 * IT RETURNS ONLY WHEN A FRAME COMPLETES.
 *
 * gpsp/cpu.cc:1479-3561 contains exactly four `return` statements (1502, 3073,
 * 3553, and falling off 3560 which is unreachable). EVERY one is guarded by
 * `completed_frame(update_ret)`. gpsp/main.h:84 defines
 *
 *     #define completed_frame(c) ((c) & 0x80000000)
 *
 * and gpsp/main.c:243 sets that bit in exactly ONE place -- the `vcount == 228`
 * frame-wrap branch.
 *
 * The consequence is that "call execute_arm(1) to run one instruction and come
 * back" IS FALSE. It runs one instruction, calls update_gba(), receives a FRESH
 * budget from it (cpu.cc:3072), and keeps going for a WHOLE EMULATED FRAME --
 * roughly 280,896 cycles, 160 update_scanline() calls and a render_gbc_sound().
 * That is M6's workload, reached by accident.
 *
 * M5 THEREFORE DOES NOT CONTROL THE ARGUMENT. IT CONTROLS THE RETURN CONDITION.
 *
 * =============================================================================
 * HOW THE RETURN CONDITION IS CONTROLLED, WITHOUT TOUCHING gpsp/
 * =============================================================================
 * The PPU is positioned at a REAL hardware instant -- the last cycle of the last
 * vblank line -- so that the very first video event after our single instruction
 * is the frame wrap that makes execute_arm return.
 *
 *     skip_next_frame = 1        adapters/gba/gba_fixture.c:70 (LUAport-owned)
 *     execute_cycles  = 1        gpsp/main.c:27
 *     video_count     = 1        gpsp/main.c:28
 *     REG_VCOUNT      = 227      last vblank scanline
 *     REG_DISPSTAT    = 0x03     vblank set (0x01) + hblank set (0x02)
 *
 * Traced consequence, line by line through gpsp/main.c:137-306:
 *
 *   1. one ARM instruction runs; cycles_remaining = 1 - 15 = -14; loop exits
 *   2. update_gba(-14): completed_cycles = 1 - (-14) = 15; cpu_ticks = 15
 *   3. video_count = 1 - 15 = -14 <= 0                 -> video event
 *   4. dispstat & 0x02 is SET   -> takes the HBLANK-EXIT branch (main.c:193).
 *      update_scanline() is on the OTHER branch (main.c:178) AND IS NEVER
 *      ENTERED. This is the primary containment, by control flow.
 *   5. video_count += 960 -> 946 ; vcount 227 -> 228
 *   6. vcount == 228 -> vcount = 0, process_cheats() (a no-op, see below),
 *      render_gbc_sound(), frame_complete = 0x80000000, frame_counter = 1
 *   7. vcount(0) == dispstat>>8 (0) -> dispstat |= 0x04 ; dispstat & 0x20 == 0
 *      so NO IRQ is raised
 *   8. irq_raised == 0 ; IME == 0 -> check_and_raise_interrupts() returns 0
 *   9. while(HALT_STATE != ACTIVE && !frame_complete) -> exits
 *  10. returns 946 | 0x80000000 -> completed_frame() true -> execute_arm RETURNS
 *
 * NET: exactly 1 ARM instruction, exactly 1 update_gba, ZERO update_scanline
 * bodies, ZERO IRQs, ZERO DMA, ZERO paging, ZERO allocation.
 *
 * THIS IS NOT A FABRICATED IMPOSSIBLE STATE. Real GBA hardware passes through
 * "vcount 227, in hblank, in vblank" once every frame. What M5 does NOT do is
 * emulate a boot's real timing: it tests the CPU FETCH/DECODE/EXECUTE PATH, not
 * PPU pacing. That distinction is stated in the fixture's own output.
 *
 * =============================================================================
 * WHAT M5 HONESTLY CANNOT CLAIM
 * =============================================================================
 * "NO FRAME COMPLETED" IS NOT ACHIEVABLE AND IS NOT ASSERTED. Frame completion
 * is the ONLY return path out of execute_arm(). frame_counter WILL be 1 and
 * frame_complete WILL be set. M5 reports that openly instead of hiding it, and
 * asserts the three things that ARE provable:
 *
 *     - no scanline was RENDERED    (control flow + skip_next_frame)
 *     - no pixel was WRITTEN        (framebuffer sentinel, byte-compared)
 *     - no host output occurred     (no VideoOut/audio symbol is linked)
 *
 * render_gbc_sound() IS reached at the frame wrap and IS disclosed. It writes
 * only gpSP's internal sample ring in .bss and touches SOUNDCNT_X; it calls no
 * host audio API, because none is linked. Audio output remains M10.
 *
 * =============================================================================
 * WHY THE SINGLE-INSTRUCTION BUDGET IS SAFE, PROVEN NOT ASSUMED
 * =============================================================================
 * The instruction at the cartridge entry point is fixed by the file on disk:
 * bytes 0..3 of the test cartridge are 2E 00 00 EA, i.e. opcode 0xEA00002E.
 *
 *     condition   0xE            = AL, always executes
 *     (op>>20)&0xFF = 0xA0       -> case 0xA0 ... 0xAF, "B offset" (cpu.cc:3021)
 *     offset = ((s32)(op<<8))>>6 = 0x000000B8      (arm_decode_branch, cpu.cc:165)
 *     reg[REG_PC] += offset + 8  -> 0x08000000 + 0xB8 + 8 = 0x080000C0
 *
 * It reads NO data, writes NO memory, touches NO register but r15, and cannot
 * reach a SWI. m5_exec_predicted_pc() below RE-DERIVES that target from the ROM
 * bytes independently of the interpreter, so the fixture compares the
 * interpreter's answer against an INDEPENDENTLY COMPUTED one rather than against
 * a hard-coded constant.
 *
 * CYCLE COST IS DERIVED THE SAME WAY. gba_memory.c:2436 calls
 * reload_timing_info() from init_memory(), and with WAITCNT == 0 (memset at
 * gba_memory.c:2421, before the reload) the gamepak rows resolve to
 *
 *     ws_cyc_seq [8][0] = 1 + ws0_seq[0]      = 3   ->  [8][1] = 6
 *     ws_cyc_nseq[8][0] = 1 + ws012_nonseq[0] = 5   ->  [8][1] = 1+5+3 = 9
 *
 * so the branch costs 9 (cpu.cc:3026) + 6 (the skip_instruction fetch,
 * cpu.cc:3061) = 15. m5_exec_predicted_ticks() recomputes that from gpSP's OWN
 * ws_cyc tables at runtime rather than pinning the literal 15.
 *
 *   A LATENT HANG WORTH RECORDING: the STATIC initialisers for the gamepak rows
 *   of ws_cyc_seq/ws_cyc_nseq are {0,0} (gba_memory.c:308-313, 326-331). If
 *   execute_arm() were ever entered WITHOUT reload_timing_info() having run,
 *   every ROM instruction would cost ZERO cycles, cycles_remaining would never
 *   fall, and the inner do/while would spin forever. reset_gba() -> init_memory()
 *   -> reload_timing_info() closes this, which is why the fixture must never
 *   call execute_arm() before reset_gba() #2. m5_exec_probe_seed() asserts the
 *   tables are non-zero as a hard gate before the call is made.
 *
 * =============================================================================
 * TERMINATION IS PROVED, NOT HOPED FOR
 * =============================================================================
 *   1. the inner loop is do/while, so at least one instruction runs even at
 *      budget 0
 *   2. every instruction subtracts >= 1 cycle once reload_timing_info() has run
 *   3. therefore completed_cycles > 0 on every update_gba, so video_count
 *      strictly decreases
 *   4. video_count <= 0 advances hblank/vcount by one step, so vcount reaches
 *      228 in a bounded number of events
 *   5. vcount == 228 sets frame_complete -> return
 *   6. update_gba's own do/while cannot spin: its condition includes
 *      reg[CPU_HALT_STATE] != CPU_ACTIVE, and the CPU is active
 *
 * A ROM BUG CANNOT HANG US: even a branch-to-self burns cycles, so the frame
 * still completes and control still returns. The worst case of a mis-computed
 * seed is that ONE FULL FRAME runs (a few milliseconds) -- and skip_next_frame
 * guarantees that even then no pixel is written. That converts the worst case
 * from "M5 silently became M6" into "M5 ran longer than intended AND SAID SO",
 * which cpu_ticks makes immediately visible.
 *
 * =============================================================================
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * =============================================================================
 *   - does NOT call execute_arm_translate()  -- the dynarec is not linked and
 *     dynarec_enable is deliberately left UNDEFINED (gba_fixture.c:20-35) so any
 *     leak is a LINK ERROR rather than a silent success
 *   - does NOT call update_gba() directly    -- only execute_arm() does, once
 *   - does NOT call update_scanline()        -- nothing is rendered
 *   - does NOT call load_gamepak_page()      -- the ROM is fully resident and the
 *     PC region never changes, so it is unreachable (see m5_exec_probe_after)
 *   - does NOT present a framebuffer or output audio
 *   - does NOT write a save file
 *   - does NOT change selected_boot_mode     -- stays boot_game, so the BIOS
 *     reset vector is NOT entered and no SWI is possible in one instruction
 *   - does NOT modify gpsp/                  -- not one line
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py matches the BARE SUBSTRINGS
 * "path_", "retro_", "libretro_", "vfs_", "filestream_", "string_is_", "rtime_",
 * "encoding_", "_stub", "emit_", "translate_block", "translation_cache" and
 * "block_lookup_address" against the LOWERCASED symbol name. Every symbol below
 * is m5_exec_* / M5_* and contains none of them. NO ALLOWLIST IS WIDENED FOR M5.
 *
 * execute_arm ITSELF IS NOT A FORBIDDEN SYMBOL -- only execute_arm_translate is
 * (tools/check_forbidden.py:67). It has been linked since M1 as part of
 * gpsp_cpu.o; M5 is merely the first milestone to CALL it.
 *
 * BITMASK CONVENTION: A SET BIT IS A FAILED ASSERTION -- identical to M2, M3 and
 * M4, so a success is a single `== 0` test and a newly added assertion cannot
 * read as "pass" if the fixture forgets to name it.
 */

#ifndef LUAPORT_GBA_EXEC_H
#define LUAPORT_GBA_EXEC_H

/* ---------------------------------------------------------- the contract --
 *
 * The cartridge entry point init_cpu() installs on the boot_game branch
 * (gpsp/cpu.cc:3583-3587). */
#define M5_ENTRY_PC          0x08000000u

/* The single instruction M5 executes, and where it must land. Both are
 * CROSS-CHECKS on the independently derived values, never the assertion
 * itself -- see m5_exec_predicted_pc(). */
#define M5_EXPECT_OPCODE     0xEA00002Eu
#define M5_EXPECT_PC         0x080000C0u
#define M5_EXPECT_TICKS      15u

/* The seeded PPU position: the last cycle of the last vblank scanline.
 *
 * NAMED M5_SEEDVAL_* AND NOT M5_SEED_*, deliberately: the M5_SEED_* prefix is
 * reserved below for the FAILED-ASSERTION BITS of m5_exec_probe_seed(). Sharing
 * one prefix between "the value we write" and "the bit that says it is wrong"
 * is exactly the kind of collision that reads as correct and compiles. */
#define M5_SEEDVAL_VCOUNT       227u
#define M5_SEEDVAL_DISPSTAT     0x03u
#define M5_SEEDVAL_VIDEO_COUNT  1
#define M5_SEEDVAL_EXEC_CYCLES  1u

/* What update_gba leaves behind after the single frame-wrap event:
 * video_count = -14 + 960, vcount = 0, dispstat = vcount-match only. */
#define M5_AFTER_VIDEO_COUNT 946
#define M5_AFTER_DISPSTAT    0x04u
#define M5_AFTER_VCOUNT      0u

/* --------------------------------------------------------------- actions -- */

/* STEP 93 -- install the seeded PPU position described in this header.
 *
 * Every value written is either a plain gpSP global (execute_cycles,
 * video_count) or a LUAport-owned global (skip_next_frame,
 * adapters/gba/gba_fixture.c:70), or goes through gpSP's own write_ioreg macro
 * (gpsp/common.h:177). NO gpSP SOURCE FILE IS MODIFIED and no shared adapter is
 * edited -- skip_next_frame is a non-static u32 and is simply declared extern
 * here, the same technique gpsp/gba_memory.c:198 itself uses. */
void m5_exec_seed(void);

/* STEP 94 -- the milestone. Calls gpSP's OWN execute_arm(cycles), unmodified.
 *
 * This is the ONLY call to it in the entire LUAport tree. It is made exactly
 * once, with cycles == 1, and only after every M4 assertion has passed and
 * m5_exec_probe_seed() has returned 0. */
void m5_exec_run(unsigned int cycles);

/* -------------------------------------------------------------- snapshots -- */

#define M5_SNAP_BEFORE  0u
#define M5_SNAP_AFTER   1u

/* Capture the whole observable CPU/video/mapping state into one of two internal
 * slots. Taken immediately before the seed and immediately after execute_arm
 * returns, so every reported delta is attributable to the single instruction
 * and the single update_gba it triggered. */
void m5_exec_snapshot(unsigned int which);

/* Read one field out of a captured snapshot. Selectors below. Returns 0 for an
 * unknown selector or an unknown slot. */
unsigned int m5_exec_snap_read(unsigned int which, unsigned int sel);

/* --------------------------------------------------- independent derivation --
 *
 * These recompute the expected results from the ROM BYTES and from gpSP's OWN
 * waitstate tables, WITHOUT running the interpreter. That is what makes the
 * post-execution comparison a genuine verification rather than a tautology. */

/* The 32-bit word at the cartridge entry point, read through memory_map_read
 * exactly as execute_arm's fetch does -- but as DATA, executing nothing.
 * Returns 0 if the gamepak window is not mapped. */
unsigned int m5_exec_first_opcode(void);

/* Decode that opcode as an ARM branch and return the PC it must produce.
 * Returns 0 if it is not an unconditional ARM "B offset", which is itself a
 * gate: M5 refuses to execute anything it has not statically decoded. */
unsigned int m5_exec_predicted_pc(void);

/* The exact cycle cost of one ARM branch executed from the WS0 gamepak region,
 * recomputed from ws_cyc_nseq[8][1] + ws_cyc_seq[8][1] (gba_memory.h:245-246)
 * rather than pinned to the literal 15. */
unsigned int m5_exec_predicted_ticks(void);

/* ---------------------------------------------------------------- probes -- */

/* STEP 93 -- assert the seed took, and that nothing has executed yet.
 * A HARD GATE: the fixture must not call execute_arm() unless this returns 0. */
unsigned int m5_exec_probe_seed(void);

/* STEP 96 -- the CPU-state proof, evaluated after execute_arm returns. */
unsigned int m5_exec_probe_cpu(void);

/* STEP 97 -- containment: mappings intact, no paging, no IRQ, no host output,
 * and the video machine in exactly the state the traced path predicts. */
unsigned int m5_exec_probe_after(void);

/* STEP 97 DIAGNOSTIC -- resolve ONE containment bit into the exact pair of
 * numbers that made it fire.
 *
 * WHY THIS EXISTS. m5_exec_probe_after() returns a bitmask, and a bitmask alone
 * cannot distinguish "frame_counter was 2" from "frame_counter was 47". The UDP
 * log carried that detail, but the log is not reliable in every setup, so the
 * numbers have to be able to reach the on-screen notification instead.
 *
 * THIS ADDS NO ASSERTION AND CHANGES NO EXPECTED VALUE. It re-reads the SAME
 * quantities m5_exec_probe_after() compares, and reports them. If `bit` is not
 * a defined containment bit index, both outputs are set to 0.
 *
 * Pointer-valued bits (MAP08, MAP0D, BIOS, SCREENPTR) report the LOW 32 BITS of
 * the pointers, which is what distinguishes two live buffers in practice; the
 * identity assertion itself is still made on the full pointer inside
 * m5_exec_probe_after(). */
void m5_exec_contain_detail(unsigned int bit,
                            unsigned int *expected, unsigned int *actual);

/* ---- seed bits (m5_exec_probe_seed) --------------------------------------- */
#define M5_SEED_SKIPFRAME   (1u << 0)  /* skip_next_frame  != 1               */
#define M5_SEED_EXECCYC     (1u << 1)  /* execute_cycles   != 1               */
#define M5_SEED_VIDCOUNT    (1u << 2)  /* video_count      != 1               */
#define M5_SEED_VCOUNT_BAD  (1u << 3)  /* REG_VCOUNT       != 227             */
#define M5_SEED_DISPSTAT_BAD (1u << 4) /* REG_DISPSTAT     != 0x03            */
#define M5_SEED_PC_MOVED    (1u << 5)  /* PC is not still 0x08000000          */
#define M5_SEED_TICKS_SET   (1u << 6)  /* cpu_ticks        != 0               */
#define M5_SEED_FRAMES_SET  (1u << 7)  /* frame_counter    != 0               */
/* THE UNIQUELY ATTRIBUTABLE ONE. clear_gamepak_stickybits() is NEVER CALLED
 * anywhere in the tree and touch_gamepak_page() is called from ONLY two places,
 * both inside execute_arm (gpsp/cpu.cc:583 and :1493). So this bit being clear
 * before the call, and set after it, is conclusive proof that execute_arm's
 * prologue ran -- and nothing else in gpSP could have set it. */
#define M5_SEED_STICKY_SET  (1u << 8)  /* sticky bit already set -- something
                                          already executed                    */
/* THE LATENT-HANG GATE described in this header. A zero gamepak waitstate row
 * means reload_timing_info() has not run and execute_arm would spin forever. */
#define M5_SEED_WS_ZERO     (1u << 9)  /* ws_cyc_seq/nseq[8][1] == 0 -- FATAL  */
#define M5_SEED_NOT_MAPPED  (1u << 10) /* gamepak window is not mapped        */
#define M5_SEED_OPCODE_BAD  (1u << 11) /* entry word is not a decodable ARM B */

/* ---- CPU bits (m5_exec_probe_cpu) -----------------------------------------
 *
 * The branch alters r15 AND NOTHING ELSE, so every other register is asserted
 * UNCHANGED. That is only safe because the budget is one instruction; it is a
 * deliberate consequence of the design, not an accident. */
#define M5_CPU_PC           (1u << 0)  /* PC != the INDEPENDENTLY derived tgt  */
#define M5_CPU_PC_LITERAL   (1u << 1)  /* PC != 0x080000C0 cross-check         */
#define M5_CPU_TICKS        (1u << 2)  /* cpu_ticks != derived cost (15)       */
#define M5_CPU_SP           (1u << 3)  /* SP moved -- branch touches no stack   */
#define M5_CPU_CPSR         (1u << 4)  /* CPSR low byte != 0x1F                */
#define M5_CPU_THUMB        (1u << 5)  /* CPSR bit 5 set -- entered Thumb       */
#define M5_CPU_MODE         (1u << 6)  /* mode != MODE_SYSTEM -- vectored?      */
#define M5_CPU_HALT         (1u << 7)  /* halt state != CPU_ACTIVE             */
#define M5_CPU_SLEEP        (1u << 8)  /* REG_SLEEP_CYCLES != 0                */
/* NO SWI PROOF. reg[REG_BUS_VALUE] is 0 after init_cpu's memset (cpu.cc:3566
 * clears reg[0..31] and REG_BUS_VALUE is 19). The ONLY thing that writes it on
 * this path is the SWI case, which sets it to 0xe3a02004 (cpu.cc:3048). A zero
 * here is therefore positive evidence that no SWI was taken. */
#define M5_CPU_BUS_VALUE    (1u << 9)
#define M5_CPU_GPREGS       (1u << 10) /* r0..r12 or LR changed                */

/* ---- containment bits (m5_exec_probe_after) ------------------------------- */
/* frame_counter == 1 is EXPECTED -- it is execute_arm's return mechanism. This
 * bit fires when it is anything ELSE, which would mean more than one frame ran. */
#define M5_CON_FRAMES       (1u << 0)
#define M5_CON_VIDCOUNT     (1u << 1)  /* video_count != 946                   */
#define M5_CON_VCOUNT       (1u << 2)  /* REG_VCOUNT  != 0                     */
#define M5_CON_DISPSTAT     (1u << 3)  /* REG_DISPSTAT != 0x04                 */
#define M5_CON_EXECCYC      (1u << 4)  /* execute_cycles != 946                */
#define M5_CON_MAP08        (1u << 5)  /* 0x08 window POINTER changed          */
#define M5_CON_MAP0D        (1u << 6)  /* 0x0D EEPROM window no longer NULL    */
#define M5_CON_BIOS         (1u << 7)  /* memory_map_read[0] != bios_rom       */
#define M5_CON_BUFCOUNT     (1u << 8)  /* gamepak_buffer_count changed -- a
                                          page was loaded                     */
#define M5_CON_FILEBLOCKS   (1u << 9)  /* gamepak_file_blocks != 1             */
#define M5_CON_STICKY       (1u << 10) /* sticky bit NOT set -- prologue never
                                          ran, so nothing executed            */
#define M5_CON_IRQ_FLAGGED  (1u << 11) /* REG_IF non-zero -- an IRQ was raised  */
#define M5_CON_IME          (1u << 12) /* REG_IME or REG_IE moved off zero     */
#define M5_CON_SKIPFRAME    (1u << 13) /* skip_next_frame no longer 1          */
/* gba_screen_pixels is NULL, or is not bit-identical to what it was in the
 * BEFORE snapshot. Compared against the snapshot rather than a remembered
 * constant, so this adapter needs no setter and keeps no second copy of the
 * fixture's 77,280-byte buffer. */
#define M5_CON_SCREENPTR    (1u << 14)
#define M5_CON_OAM_UPDATED  (1u << 15) /* reg[OAM_UPDATED] set -- OBJ reorder   */

/* ---- m5_exec_snap_read selectors ------------------------------------------ */
#define M5_RD_PC              0u
#define M5_RD_SP              1u
#define M5_RD_CPSR            2u
#define M5_RD_MODE            3u
#define M5_RD_HALT            4u
#define M5_RD_CPU_TICKS       5u
#define M5_RD_VIDEO_COUNT     6u
#define M5_RD_EXEC_CYCLES     7u
#define M5_RD_FRAME_COUNTER   8u
#define M5_RD_VCOUNT          9u
#define M5_RD_DISPSTAT       10u
#define M5_RD_SKIP_FRAME     11u
#define M5_RD_IME            12u
#define M5_RD_IE             13u
#define M5_RD_IF             14u
#define M5_RD_STICKY0        15u  /* gamepak_sticky_bit[0] & 1                */
#define M5_RD_BUFFER_COUNT   16u
#define M5_RD_FILE_BLOCKS    17u
#define M5_RD_BUS_VALUE      18u
#define M5_RD_DISPCNT        19u
#define M5_RD_OAM_UPDATED    20u
#define M5_RD_MAP08_LOW      21u  /* low 32 bits of memory_map_read[4096]     */
#define M5_RD_GPREG_OR       22u  /* OR of r0..r12 and LR -- 0 if untouched    */

#endif /* LUAPORT_GBA_EXEC_H */
