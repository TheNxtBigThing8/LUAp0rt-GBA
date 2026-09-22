/* LUAport M5 -- adapters/gba/gba_exec.c
 *
 * The gpSP-facing half of the M5 controlled-execution milestone, and the ONLY
 * place in the entire LUAport tree that calls execute_arm().
 *
 * WHAT THIS FILE IS
 * -----------------
 * The only translation unit in the M5 build that drives gpSP's ARM7TDMI
 * interpreter. It is the only new gpSP-side TU M5 adds; everything else in the
 * closure is M4's, linked unchanged.
 *
 * apps/m5gpsp/main.c sits on the other side of the type boundary (runtime/
 * core.h, shim.h, boot.inc) and cannot include gpSP headers: gpsp/common.h:100
 * typedefs u64 as `unsigned long long` while runtime/core.h:30 uses `unsigned
 * long`, and a TU including both fails on the duplicate typedef. See
 * adapters/gba/gba_exec.h, which also carries the full argument for why the
 * execution is bounded, why it terminates, and what M5 can and cannot claim.
 *
 * gpsp/ IS NOT MODIFIED. Not one line. Every symbol touched below is either
 * already declared in a gpSP header or is a non-static global declared extern by
 * hand -- the same technique upstream itself uses at gpsp/gba_memory.c:198
 * (`extern timer_type timer[4];`) and that adapters/gba/gba_probe.c:75-85
 * established for M2.
 *
 * NO SHARED ADAPTER IS EDITED EITHER. skip_next_frame is DEFINED at
 * adapters/gba/gba_fixture.c:70 as a non-static u32; M5 declares it extern and
 * assigns it at runtime. gba_fixture.c, gba_probe.c, gba_bios.c and gba_rom.c
 * are all linked byte-identically to M4.
 */

#include "common.h"      /* pulls cpu.h (execute_arm, reg[], REG_*, MODE_*),
                            gba_memory.h (memory_map_read, bios_rom,
                            io_registers, ws_cyc_seq/nseq, gamepak_*),
                            main.h (cpu_ticks, frame_counter, execute_cycles),
                            video.h (gba_screen_pixels)                      */

#include "gba_exec.h"

/* The memory_map_read[] index for an address. map_region/map_null/map_rom_entry
 * (gba_memory.c:2254-2288) all bucket the map in 32 KB (0x8000) units. Kept
 * privately here, exactly as gba_probe.c, gba_bios.c and gba_rom.c each keep
 * their own copy, so no file depends on another's internals. */
#define MAP_IDX(addr) ((addr) / 0x8000)

#define ROM_IDX_08  MAP_IDX(0x8000000)   /* 4096 -- the PC region M5 executes */
#define ROM_IDX_0D  MAP_IDX(0xD000000)   /* 6656 -- the EEPROM boundary       */

/* The gamepak waitstate row the entry point lives in. reload_timing_info()
 * writes rows 0x8..0xD (gba_memory.c:429-448); 0x08000000 >> 24 == 8. */
#define WS_ROW_GAMEPAK 0x8

/* ---------------------------------------------------- hand-declared state --
 *
 * Each is a NON-STATIC global that simply has no extern in a gpSP header.
 * Declaring them here is READING (and, for the two seed variables, writing a
 * value gpSP itself writes on the same path), not modifying gpSP.
 *
 *   video_count        defined gpsp/main.c:28
 *   gamepak_file_blocks defined gpsp/gba_memory.c:372 -- declared in no header,
 *                      which is why adapters/gba/gba_rom.c:82 also externs it
 *   skip_next_frame    defined adapters/gba/gba_fixture.c:70 -- LUAport-owned,
 *                      declared gpsp/main.h:74, read gpsp/video.cc:2354
 *
 * Everything else M5 needs IS in a gpSP header and is used through it, never
 * restated here: reg[]/REG_* (cpu.h, gba_memory.h:284), execute_arm (cpu.h:112),
 * cpu_ticks/frame_counter/execute_cycles (main.h:71-73), memory_map_read,
 * bios_rom, io_registers, gamepak_buffer_count, gamepak_sticky_bit and
 * ws_cyc_seq/ws_cyc_nseq (gba_memory.h), and gba_screen_pixels (video.h:29).
 */
extern s32 video_count;
extern u32 gamepak_file_blocks;
extern u32 skip_next_frame;

/* ------------------------------------------------------------- snapshots --
 *
 * Two slots, captured immediately before the seed and immediately after
 * execute_arm() returns, so every reported delta is attributable to the single
 * instruction and the single update_gba() it triggered.
 *
 * .bss cost is 2 * sizeof(struct) -- roughly 200 bytes, far below the 2 MB
 * tripwire in tools/check_forbidden.py and irrelevant against M4's 1,414,496. */
typedef struct {
    u32 pc, sp, cpsr, mode, halt;
    u32 cpu_ticks, frame_counter;
    s32 video_count;
    u32 execute_cycles;
    u32 vcount, dispstat, dispcnt;
    u32 skip_frame;
    u32 ime, ie, iflag;
    u32 sticky0;
    u32 buffer_count, file_blocks;
    u32 bus_value, oam_updated;
    u32 gpreg_or;
    u8 *map08;
    u16 *screen;
} m5_snap_t;

static m5_snap_t m5_snap[2];
static int       m5_snap_taken[2];

/* ------------------------------------------------------------- helpers ---- */

/* OR-reduce r0..r12 and LR. A branch writes r15 and NOTHING else, so a non-zero
 * result after execution means some other register moved. Reduced rather than
 * compared one-by-one so the assertion is a single test that cannot silently
 * skip a register. */
static u32 m5_gpreg_or(void)
{
    u32 acc = 0, i;

    for (i = 0; i <= 12; i++)
        acc |= reg[i];

    acc |= reg[REG_LR];
    return acc;
}

/* --------------------------------------------------------------- reads ---- */

unsigned int m5_exec_first_opcode(void)
{
    u8 *blk = memory_map_read[ROM_IDX_08];

    /* Refuse to dereference an unmapped window rather than faulting. A 0 here
     * is caught by M5_SEED_NOT_MAPPED before anything executes. */
    if (!blk)
        return 0u;

    /* THE SAME FETCH execute_arm PERFORMS, as DATA. cpu.cc:1525 does
     *     opcode = readaddress32(pc_address_block, (reg[REG_PC] & 0x7FFF));
     * and readaddress32 is gpsp/common.h:174. Reading it here executes nothing;
     * it merely lets the fixture prove that the word the interpreter will fetch
     * is the word that is actually on the cartridge. */
    return (unsigned int)readaddress32(blk, (M5_ENTRY_PC & 0x7FFF));
}

unsigned int m5_exec_predicted_pc(void)
{
    u32 op = (u32)m5_exec_first_opcode();
    s32 offset;

    if (!op)
        return 0u;

    /* Unconditional only. gpSP dispatches on `opcode >> 28` (cpu.cc:1527); 0xE
     * is AL. M5 refuses to execute an instruction whose execution is itself
     * conditional on flags, because then the predicted PC would depend on CPU
     * state rather than on the ROM bytes alone. */
    if ((op >> 28) != 0xEu)
        return 0u;

    /* case 0xA0 ... 0xAF -- "B offset", cpu.cc:3021. Deliberately NOT accepting
     * 0xB0..0xBF (BL): that one also writes REG_LR, which would break the
     * "r15 and nothing else" assertion this milestone rests on. */
    if (((op >> 20) & 0xFFu) < 0xA0u || ((op >> 20) & 0xFFu) > 0xAFu)
        return 0u;

    /* arm_decode_branch(), cpu.cc:165-166:
     *     s32 offset = ((s32)((u32)(opcode << 8))) >> 6
     * then cpu.cc:3024:
     *     reg[REG_PC] += offset + 8
     * Reproduced EXACTLY, including the sign-extending arithmetic shift, so the
     * prediction is the same computation the interpreter will perform -- but
     * carried out independently, from the bytes, before it runs. */
    offset = ((s32)((u32)(op << 8))) >> 6;

    return (unsigned int)(M5_ENTRY_PC + (u32)offset + 8u);
}

unsigned int m5_exec_predicted_ticks(void)
{
    /* The branch itself: cpu.cc:3026 subtracts
     *     ws_cyc_nseq[reg[REG_PC] >> 24][1]
     * using the NEW pc, which is still in the 0x08 region.
     * The fetch of the next instruction: cpu.cc:3061 subtracts
     *     ws_cyc_seq[(reg[REG_PC] >> 24) & 0xF][1]
     *
     * Recomputed from gpSP's OWN tables (gba_memory.h:245-246) rather than
     * pinning the literal 15, so if a future WAITCNT default or a change to
     * reload_timing_info() moved the cost, this tracks it and the assertion
     * stays true for the right reason. */
    return (unsigned int)ws_cyc_nseq[WS_ROW_GAMEPAK][1]
         + (unsigned int)ws_cyc_seq[WS_ROW_GAMEPAK][1];
}

/* ------------------------------------------------------------ snapshots --- */

void m5_exec_snapshot(unsigned int which)
{
    m5_snap_t *s;

    if (which > M5_SNAP_AFTER)
        return;

    s = &m5_snap[which];

    s->pc             = reg[REG_PC];
    s->sp             = reg[REG_SP];
    s->cpsr           = reg[REG_CPSR];
    s->mode           = reg[CPU_MODE];
    s->halt           = reg[CPU_HALT_STATE];
    s->bus_value      = reg[REG_BUS_VALUE];
    s->oam_updated    = reg[OAM_UPDATED];
    s->gpreg_or       = m5_gpreg_or();

    s->cpu_ticks      = cpu_ticks;
    s->frame_counter  = frame_counter;
    s->video_count    = video_count;
    s->execute_cycles = execute_cycles;

    s->vcount         = read_ioreg(REG_VCOUNT);
    s->dispstat       = read_ioreg(REG_DISPSTAT);
    s->dispcnt        = read_ioreg(REG_DISPCNT);
    s->ime            = read_ioreg(REG_IME);
    s->ie             = read_ioreg(REG_IE);
    s->iflag          = read_ioreg(REG_IF);

    s->skip_frame     = skip_next_frame;

    /* gamepak_sticky_bit[0] bit 0 is the page-4096 marker: touch_gamepak_page
     * (gba_memory.h:324-330) computes idx = (4096>>5)&31 = 0 and
     * bof = 4096&31 = 0. Both in range -- the & 31 is what prevents the raw
     * index from overflowing the [1024/32] array. */
    s->sticky0        = gamepak_sticky_bit[0] & 1u;

    s->buffer_count   = gamepak_buffer_count;
    s->file_blocks    = gamepak_file_blocks;

    s->map08          = memory_map_read[ROM_IDX_08];
    s->screen         = gba_screen_pixels;

    m5_snap_taken[which] = 1;
}

unsigned int m5_exec_snap_read(unsigned int which, unsigned int sel)
{
    const m5_snap_t *s;

    if (which > M5_SNAP_AFTER || !m5_snap_taken[which])
        return 0u;

    s = &m5_snap[which];

    switch (sel) {
    case M5_RD_PC:            return s->pc;
    case M5_RD_SP:            return s->sp;
    case M5_RD_CPSR:          return s->cpsr;
    case M5_RD_MODE:          return s->mode;
    case M5_RD_HALT:          return s->halt;
    case M5_RD_CPU_TICKS:     return s->cpu_ticks;
    case M5_RD_VIDEO_COUNT:   return (unsigned int)s->video_count;
    case M5_RD_EXEC_CYCLES:   return s->execute_cycles;
    case M5_RD_FRAME_COUNTER: return s->frame_counter;
    case M5_RD_VCOUNT:        return s->vcount;
    case M5_RD_DISPSTAT:      return s->dispstat;
    case M5_RD_SKIP_FRAME:    return s->skip_frame;
    case M5_RD_IME:           return s->ime;
    case M5_RD_IE:            return s->ie;
    case M5_RD_IF:            return s->iflag;
    case M5_RD_STICKY0:       return s->sticky0;
    case M5_RD_BUFFER_COUNT:  return s->buffer_count;
    case M5_RD_FILE_BLOCKS:   return s->file_blocks;
    case M5_RD_BUS_VALUE:     return s->bus_value;
    case M5_RD_DISPCNT:       return s->dispcnt;
    case M5_RD_OAM_UPDATED:   return s->oam_updated;
    /* Cast through u64 (gpsp/common.h:100) rather than uintptr_t: common.h does
     * not include <stdint.h>, so uintptr_t is not guaranteed to be in scope. The
     * low 32 bits are only ever printed for diagnosis -- the actual pointer
     * IDENTITY assertion is made against the snapshot in m5_exec_probe_after. */
    case M5_RD_MAP08_LOW:     return (unsigned int)(u64)s->map08;
    case M5_RD_GPREG_OR:      return s->gpreg_or;
    default:                  return 0u;
    }
}

/* --------------------------------------------------------------- actions -- */

void m5_exec_seed(void)
{
    /* THE CONTAINMENT LEVER, and the reason M5 needs no gpSP edit.
     *
     * update_scanline() begins (gpsp/video.cc:2346-2355) by computing a pitch,
     * a dispcnt, a vcount and a screen_offset POINTER -- and then:
     *
     *     if(skip_next_frame)
     *       return;
     *
     * The early return happens BEFORE any dereference of that pointer, so even
     * if update_scanline were somehow entered, not one pixel is written and
     * gba_screen_pixels is never read through.
     *
     * This is BELT AND BRACES. The primary containment is control flow: the
     * seeded DISPSTAT makes update_gba take the hblank-EXIT branch
     * (gpsp/main.c:193), and update_scanline is on the other branch entirely. */
    skip_next_frame = 1u;

    /* update_gba computes completed_cycles as `execute_cycles - remaining`
     * (main.c:142) from the GLOBAL, never from execute_arm's argument. Setting
     * it to 1 alongside the budget of 1 is what makes cpu_ticks come out as the
     * TRUE cost of the single instruction (15) instead of an inflated figure. */
    execute_cycles = M5_SEEDVAL_EXEC_CYCLES;

    /* One cycle to the next video event, so the very first update_gba crosses
     * the boundary and the frame wrap -- execute_arm's only return path -- is
     * reached immediately rather than 280,896 cycles later. */
    video_count = M5_SEEDVAL_VIDEO_COUNT;

    /* The last cycle of the last vblank scanline: a REAL hardware instant that
     * every frame passes through. vcount 227 + hblank set means the next event
     * is "leave hblank, advance to line 228", and 228 is the frame wrap. */
    write_ioreg(REG_VCOUNT,   M5_SEEDVAL_VCOUNT);
    write_ioreg(REG_DISPSTAT, M5_SEEDVAL_DISPSTAT);
}

void m5_exec_run(unsigned int cycles)
{
    /* =====================================================================
     * THE MILESTONE. gpSP's OWN execute_arm(), called UNMODIFIED, exactly once.
     * =====================================================================
     *
     * This is the only call to it in the LUAport tree. It is reached only after
     * every M4 assertion has passed and m5_exec_probe_seed() has returned 0 --
     * which includes the waitstate gate that makes termination provable.
     *
     * execute_arm returns void: it reports nothing. All evidence is gathered by
     * snapshotting gpSP's state immediately afterwards, which is why the
     * snapshot machinery above exists.
     *
     * NOT execute_arm_translate(). That symbol is on tools/check_forbidden.py's
     * dynarec list (line 67), cpu_threaded.c is not compiled, HAVE_DYNAREC is
     * not defined, and dynarec_enable is deliberately left UNDEFINED so a leak
     * would be a link error. */
    execute_arm(cycles);
}

/* ---------------------------------------------------------------- probes -- */

unsigned int m5_exec_probe_seed(void)
{
    unsigned int f = 0;
    unsigned int op;

    if (skip_next_frame != 1u)
        f |= M5_SEED_SKIPFRAME;

    if (execute_cycles != M5_SEEDVAL_EXEC_CYCLES)
        f |= M5_SEED_EXECCYC;

    if (video_count != M5_SEEDVAL_VIDEO_COUNT)
        f |= M5_SEED_VIDCOUNT;

    if (read_ioreg(REG_VCOUNT) != M5_SEEDVAL_VCOUNT)
        f |= M5_SEED_VCOUNT_BAD;

    if (read_ioreg(REG_DISPSTAT) != M5_SEEDVAL_DISPSTAT)
        f |= M5_SEED_DISPSTAT_BAD;

    /* Nothing may have executed yet. init_cpu put PC here (cpu.cc:3585) and
     * only an executed instruction can move it. */
    if (reg[REG_PC] != M5_ENTRY_PC)
        f |= M5_SEED_PC_MOVED;

    if (cpu_ticks != 0u)
        f |= M5_SEED_TICKS_SET;

    if (frame_counter != 0u)
        f |= M5_SEED_FRAMES_SET;

    /* UNIQUELY ATTRIBUTABLE. touch_gamepak_page is called from exactly two
     * places, cpu.cc:583 and cpu.cc:1493, BOTH inside execute_arm, and
     * clear_gamepak_stickybits() is never called anywhere in the tree. So a set
     * bit here before we have called anything would mean execute_arm had
     * already run -- which at this point in the fixture is impossible unless
     * something is very wrong. */
    if ((gamepak_sticky_bit[0] & 1u) != 0u)
        f |= M5_SEED_STICKY_SET;

    /* THE LATENT-HANG GATE. The static initialisers for the gamepak rows of
     * both waitstate tables are {0,0} (gba_memory.c:308-313, 326-331). If
     * reload_timing_info() had not run -- i.e. if execute_arm were called before
     * reset_gba()/init_memory() -- every ROM instruction would cost ZERO cycles,
     * cycles_remaining would never fall, and the interpreter's inner do/while
     * would SPIN FOREVER on the console.
     *
     * This is the assertion that makes termination provable rather than hoped
     * for, and it is the single most important gate in the milestone. */
    if (ws_cyc_seq[WS_ROW_GAMEPAK][1] == 0 || ws_cyc_nseq[WS_ROW_GAMEPAK][1] == 0)
        f |= M5_SEED_WS_ZERO;

    /* execute_arm's prologue calls load_gamepak_page() when the PC region is
     * unmapped (cpu.cc:1491-1492). M5 requires a fully resident, already-mapped
     * cartridge so that path is never taken. */
    if (memory_map_read[ROM_IDX_08] == 0)
        f |= M5_SEED_NOT_MAPPED;

    /* M5 REFUSES TO EXECUTE WHAT IT HAS NOT STATICALLY DECODED. If the entry
     * word is not an unconditional ARM branch, the predicted PC is undefined and
     * the milestone's central assertion would be meaningless. */
    op = m5_exec_first_opcode();
    if (op == 0u || m5_exec_predicted_pc() == 0u)
        f |= M5_SEED_OPCODE_BAD;

    return f;
}

unsigned int m5_exec_probe_cpu(void)
{
    unsigned int f = 0;
    unsigned int want_pc    = m5_exec_predicted_pc();
    unsigned int want_ticks = m5_exec_predicted_ticks();

    /* THE MILESTONE'S CENTRAL ASSERTION. The PC the interpreter produced must
     * equal the one derived independently from the cartridge bytes. No other
     * outcome could counterfeit this value: it encodes the fetch (the right
     * word, from the right place), the decode (recognising 0xEA as a branch) and
     * the arithmetic (offset + 8) all at once. */
    if (want_pc == 0u || reg[REG_PC] != want_pc)
        f |= M5_CPU_PC;

    /* An independent cross-check against the value read off the file on a PC.
     * Kept SEPARATE from the derived check so a divergence between "what the
     * bytes say" and "what we expected the bytes to say" is distinguishable
     * from an interpreter fault. */
    if (reg[REG_PC] != M5_EXPECT_PC)
        f |= M5_CPU_PC_LITERAL;

    /* Exact arithmetic, cross-checking the whole waitstate path. */
    if (want_ticks == 0u || cpu_ticks != want_ticks)
        f |= M5_CPU_TICKS;

    /* A branch touches no stack. */
    if (reg[REG_SP] != 0x03007F00u)
        f |= M5_CPU_SP;

    /* Mode preserved, no exception taken. The branch alters no flag. */
    if ((reg[REG_CPSR] & 0xFFu) != 0x1Fu)
        f |= M5_CPU_CPSR;

    /* Still ARM state -- cpu.cc:1510 dispatches to thumb_loop on bit 5. */
    if ((reg[REG_CPSR] & 0x20u) != 0u)
        f |= M5_CPU_THUMB;

    /* No SWI, no IRQ vectoring. */
    if (reg[CPU_MODE] != (u32)MODE_SYSTEM)
        f |= M5_CPU_MODE;

    if (reg[CPU_HALT_STATE] != (u32)CPU_ACTIVE)
        f |= M5_CPU_HALT;

    if (reg[REG_SLEEP_CYCLES] != 0u)
        f |= M5_CPU_SLEEP;

    /* THE NO-SWI PROOF. The only writer of REG_BUS_VALUE on this path is the
     * SWI case (cpu.cc:3048, 0xe3a02004). init_cpu's memset cleared it
     * (cpu.cc:3566 clears reg[0..31]; REG_BUS_VALUE is 19), so it must still
     * be 0. */
    if (reg[REG_BUS_VALUE] != 0u)
        f |= M5_CPU_BUS_VALUE;

    /* r0..r12 and LR are untouched by a plain B. Asserted tightly ONLY because
     * the budget is one statically decoded instruction. */
    if (m5_gpreg_or() != 0u)
        f |= M5_CPU_GPREGS;

    return f;
}

/* Resolve one containment bit into the exact (expected, actual) pair that made
 * it fire. READ-ONLY, and deliberately a MIRROR of m5_exec_probe_after(): every
 * case below re-reads the same quantity that function compares, using the same
 * expected constant. It introduces no new assertion and relaxes none.
 *
 * Kept in this file rather than the fixture because most of these quantities are
 * gpSP globals that apps/m5gpsp/main.c cannot see across the type boundary. */
void m5_exec_contain_detail(unsigned int bit,
                            unsigned int *expected, unsigned int *actual)
{
    unsigned int e = 0, a = 0;
    int have_before = m5_snap_taken[M5_SNAP_BEFORE];

    switch (bit) {
    case 0:  /* M5_CON_FRAMES     */ e = 1u;
             a = frame_counter; break;
    case 1:  /* M5_CON_VIDCOUNT   */ e = (unsigned int)M5_AFTER_VIDEO_COUNT;
             a = (unsigned int)video_count; break;
    case 2:  /* M5_CON_VCOUNT     */ e = (unsigned int)M5_AFTER_VCOUNT;
             a = read_ioreg(REG_VCOUNT); break;
    case 3:  /* M5_CON_DISPSTAT   */ e = (unsigned int)M5_AFTER_DISPSTAT;
             a = read_ioreg(REG_DISPSTAT); break;
    case 4:  /* M5_CON_EXECCYC    */ e = (unsigned int)M5_AFTER_VIDEO_COUNT;
             a = execute_cycles; break;
    case 5:  /* M5_CON_MAP08      */
             e = have_before
                 ? (unsigned int)(u64)m5_snap[M5_SNAP_BEFORE].map08 : 0u;
             a = (unsigned int)(u64)memory_map_read[ROM_IDX_08]; break;
    case 6:  /* M5_CON_MAP0D      */ e = 0u;
             a = (unsigned int)(u64)memory_map_read[ROM_IDX_0D]; break;
    case 7:  /* M5_CON_BIOS       */ e = (unsigned int)(u64)bios_rom;
             a = (unsigned int)(u64)memory_map_read[0]; break;
    case 8:  /* M5_CON_BUFCOUNT   */
             e = have_before ? m5_snap[M5_SNAP_BEFORE].buffer_count : 0u;
             a = gamepak_buffer_count; break;
    case 9:  /* M5_CON_FILEBLOCKS */ e = 1u;
             a = gamepak_file_blocks; break;
    case 10: /* M5_CON_STICKY     */ e = 1u;
             a = gamepak_sticky_bit[0] & 1u; break;
    case 11: /* M5_CON_IRQ_FLAGGED*/ e = 0u;
             a = read_ioreg(REG_IF); break;
    /* IME in the low half, IE in the high half: one word carries both, which is
     * what the single expected/actual slot in the notification can show. */
    case 12: /* M5_CON_IME        */ e = 0u;
             a = (read_ioreg(REG_IME) & 0xFFFFu)
               | ((read_ioreg(REG_IE) & 0xFFFFu) << 16); break;
    case 13: /* M5_CON_SKIPFRAME  */ e = 1u;
             a = skip_next_frame; break;
    case 14: /* M5_CON_SCREENPTR  */
             e = have_before
                 ? (unsigned int)(u64)m5_snap[M5_SNAP_BEFORE].screen : 0u;
             a = (unsigned int)(u64)gba_screen_pixels; break;
    case 15: /* M5_CON_OAM_UPDATED*/ e = 0u;
             a = reg[OAM_UPDATED]; break;
    default: break;
    }

    if (expected) *expected = e;
    if (actual)   *actual   = a;
}

unsigned int m5_exec_probe_after(void)
{
    unsigned int f = 0;

    /* frame_counter == 1 IS THE EXPECTED, DESIGNED OUTCOME -- it is
     * execute_arm's only return path. Anything else means more than one frame
     * ran, i.e. the seed did not land and M5 quietly did M6's work. */
    if (frame_counter != 1u)
        f |= M5_CON_FRAMES;

    /* -14 + 960. One video event and exactly one. */
    if (video_count != M5_AFTER_VIDEO_COUNT)
        f |= M5_CON_VIDCOUNT;

    if (read_ioreg(REG_VCOUNT) != M5_AFTER_VCOUNT)
        f |= M5_CON_VCOUNT;

    /* vblank and hblank cleared, vcount-match set (main.c:223, 196, 250). */
    if (read_ioreg(REG_DISPSTAT) != M5_AFTER_DISPSTAT)
        f |= M5_CON_DISPSTAT;

    /* execute_cycles = MAX(video_count,0) then MIN with serial_next_event(),
     * which returns ~0U with no cable (serial.c:193-196), so it is video_count. */
    if (execute_cycles != (u32)M5_AFTER_VIDEO_COUNT)
        f |= M5_CON_EXECCYC;

    /* THE MAPPING MUST BE POINTER-IDENTICAL, not merely non-NULL. A changed
     * pointer would mean load_gamepak_page() ran and swapped a block in. */
    if (!m5_snap_taken[M5_SNAP_BEFORE]
        || memory_map_read[ROM_IDX_08] != m5_snap[M5_SNAP_BEFORE].map08
        || memory_map_read[ROM_IDX_08] == 0)
        f |= M5_CON_MAP08;

    /* The EEPROM boundary M4 established must still hold. */
    if (memory_map_read[ROM_IDX_0D] != 0)
        f |= M5_CON_MAP0D;

    /* M3's guarantee must survive execution. */
    if (memory_map_read[0] != bios_rom)
        f |= M5_CON_BIOS;

    /* NO PAGING. load_gamepak_page() would allocate and change these. */
    if (!m5_snap_taken[M5_SNAP_BEFORE]
        || gamepak_buffer_count != m5_snap[M5_SNAP_BEFORE].buffer_count)
        f |= M5_CON_BUFCOUNT;

    /* A single 32 KB page -- fully resident, so the pager is unreachable. */
    if (gamepak_file_blocks != 1u)
        f |= M5_CON_FILEBLOCKS;

    /* POSITIVE EVIDENCE that execute_arm's prologue ran. Nothing else in gpSP
     * can set this bit. */
    if ((gamepak_sticky_bit[0] & 1u) == 0u)
        f |= M5_CON_STICKY;

    /* No interrupt was raised. DISPSTAT bit 5 (vcount IRQ enable) is clear, so
     * the vcount match at main.c:248-252 raised nothing. */
    if (read_ioreg(REG_IF) != 0u)
        f |= M5_CON_IRQ_FLAGGED;

    if (read_ioreg(REG_IME) != 0u || read_ioreg(REG_IE) != 0u)
        f |= M5_CON_IME;

    /* The containment lever must still be engaged. */
    if (skip_next_frame != 1u)
        f |= M5_CON_SKIPFRAME;

    /* The output surface must be exactly the pointer we installed, unchanged
     * across execution. Compared against the BEFORE snapshot rather than a
     * remembered constant, so this file needs no setter and no second copy of
     * the fixture's buffer. */
    if (!m5_snap_taken[M5_SNAP_BEFORE]
        || gba_screen_pixels != m5_snap[M5_SNAP_BEFORE].screen
        || gba_screen_pixels == 0)
        f |= M5_CON_SCREENPTR;

    /* update_scanline() reorders the OBJ lists when reg[OAM_UPDATED] is set
     * (video.cc:2358-2362), and main.c:175 increments oam_update_count on the
     * render branch. A clear flag is corroborating evidence that the render
     * branch was never entered. */
    if (reg[OAM_UPDATED] != 0u)
        f |= M5_CON_OAM_UPDATED;

    return f;
}
