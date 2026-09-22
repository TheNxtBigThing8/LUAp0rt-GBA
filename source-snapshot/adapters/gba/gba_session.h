#ifndef LUAPORT_GBA_SESSION_H
#define LUAPORT_GBA_SESSION_H

/* LUAport M16-0 -- adapters/gba/gba_session.h
 *
 * THE GBA SESSION BOUNDARY: everything that must happen between one cartridge
 * ending and the next one starting, inside a SINGLE payload lifetime.
 *
 * =========================================================================
 * THIS FILE IS M16-ONLY AND IT ENFORCES THAT ITSELF
 * =========================================================================
 *
 * M13C -- GBA v1 -- is FROZEN. Its artifact, build/m13c/m13cgpsp.bin, must stay
 * reproducible byte for byte. Nothing in this header or its implementation may
 * ever reach an M13C translation unit, so rather than trusting the Makefile to
 * keep them apart, the #error below makes a leak a COMPILE FAILURE at the exact
 * moment it happens instead of a silent hash change discovered later.
 *
 * LUAPORT_SESSION_REUSE is set by $(M16_GPSPFLAGS) and $(M16FLAGS) and by
 * nothing else in the repository.
 */
#ifndef LUAPORT_SESSION_REUSE
#error "gba_session.h is M16-only: build with -DLUAPORT_SESSION_REUSE. \
It must never be reachable from the frozen M13C target."
#endif

/* SELF-CONTAINED BY DESIGN. The relaunch masks below are written in terms of
 * the probe bits themselves rather than as bare numbers, so a bit that is ever
 * renumbered upstream cannot silently change this file's meaning.
 *
 * ***** ALL THREE ARE TYPE-BOUNDARY CLEAN. ***** gba_probe.h, gba_bios.h and
 * gba_rom.h include NOTHING and declare every boundary value as plain
 * `unsigned int`. That is what lets this header be included BOTH from the app
 * (which is compiled with -Iruntime and therefore sees runtime/core.h, where
 * u64 is `unsigned long`) AND from a gpSP-facing adapter (which sees
 * gpsp/common.h, where u64 is `unsigned long long`). A header that pulled in
 * either type universe would make one of those two translation units
 * uncompilable -- which is exactly the trap documented in the Makefile's
 * gpSP-facing adapter rules. */
#include "gba_probe.h"    /* M2_PRE_*  */
#include "gba_bios.h"     /* M3_PRE_*  */
#include "gba_rom.h"      /* M4_PRE_*  */

/* -------------------------------------------------------------------------
 * WHY THIS LAYER EXISTS AT ALL
 * -------------------------------------------------------------------------
 *
 * M13C's _start() is a straight line: bring up the runtime, pick a cartridge,
 * load it, play it, commit the save, tear the runtime down, return -- and
 * returning from _start() IS the payload exiting. There is no backward branch
 * anywhere in it, and that was the correct shape for a launcher that runs one
 * game per invocation.
 *
 * Returning to the LUAp0rt menu instead of exiting turns "one cartridge per
 * process" into "many cartridges per process", and that breaks FIVE pieces of
 * state that are deliberately latched ONCE PER PROCESS LIFETIME:
 *
 *   1. THE SAVE IDENTITY. gba_save_latch() refuses to re-latch. Session 2 would
 *      keep session 1's identity and COMMIT CARTRIDGE B's SRAM INTO CARTRIDGE
 *      A's .sav. This is silent data loss and it is the single most dangerous
 *      item on the list.  -> gba_save_unlatch()
 *
 *   2. THE ARENA. malloc() is a bump allocator whose free() is a no-op, and
 *      gpSP's ROM buffers are 2 MB of a 4 MB arena. Session 2's
 *      m4_rom_buffer_init() would get ZERO buffers and the load would fail.
 *      -> arena_mark() / arena_rewind(), in runtime/shim.c, called by the
 *         session controller because the arena is a RUNTIME service and knows
 *         nothing about GBA.
 *
 *   3. THE ROM FILE HANDLE. gpSP holds the gamepak file open for demand paging
 *      and closes it only in memory_term(), which M13C never calls. One RFILE
 *      pool slot of four would leak per launch.  -> memory_term()
 *
 *   4. THE RESTORE ARM FLAG. gba_restore_arm() sets it and nothing clears it,
 *      so session 2's validate-only pass -- documented as provably passive --
 *      would run ARMED.  -> gba_restore_disarm()
 *
 *   5. THE BACKUP ARRAY. init_memory() provably never writes gamepak_backup[],
 *      so cartridge B would compute its restore baseline over cartridge A's
 *      bytes and dirty detection would become meaningless.
 *      -> m4_rom_backup_init()
 *
 * =========================================================================
 * WHAT THIS LAYER IS NOT
 * =========================================================================
 *
 * ***** IT IS NOT A GENERIC CONSUMER API. ***** There is exactly one consumer
 * in this project -- GBA -- so a consumer_session_start/run/stop triple would
 * be a three-call abstraction with a single implementation, which this codebase
 * rejects on principle. The layering that already exists is the right one and
 * is kept: the app is the generic session controller, this file is the GBA
 * adapter boundary, gpsp/ is the untouched core.
 *
 * ***** IT DOES NOT IMPLEMENT RETURN-TO-PICKER. ***** M16-0 proves the reset
 * MECHANISM. The picker loop, the chord's new meaning and the menu lifecycle
 * are later rungs and nothing here presumes their design.
 *
 * ***** IT TEARS DOWN A GAME SESSION, NEVER THE RUNTIME. ***** Video, pad and
 * the audio PORT stay up across the boundary: a menu cannot be drawn on a
 * framebuffer that was released, and reopening an audio port per game would be
 * a new failure mode for no benefit. Only the audio RING is reset.
 *
 * ***** NOT ONE LINE OF gpsp/ IS MODIFIED. ***** memory_term() is gpSP's own
 * cleanup entry point, already declared at gpsp/gba_memory.h:264 and already
 * linked into M13C's image -- it has simply never been called. M16-0 calls it.
 */

/* ---------------------------------------------------------------- teardown --
 *
 * END THE CURRENT GAME SESSION. Safe to call when no cartridge was ever loaded
 * (every step below is individually idempotent), which is what lets a failure
 * path unwind through the same code as a clean ending.
 *
 * THE ORDER IS LOAD-BEARING and is documented step by step in gba_session.c.
 * In summary:
 *
 *   1. m8_input_apply(0)      REG_P1 back to its neutral 0x3FF
 *   2. gba_audio_reset()      ring + resampler + gpSP's mix bus; PORT STAYS OPEN
 *   3. memory_term()          CLOSES THE ROM FILE, drops the buffer count to 0
 *   4. gba_save_unlatch()     the identity lifecycle ends  *** AFTER COMMIT ***
 *   5. gba_restore_disarm()   back to pre-arm state
 *   6. m12c_sramobs_reset()   T1/T2/T3 observation slots
 *   7. m10map_paging_reset()  the paging observer's baseline
 *
 * ***** IT DOES NOT COMMIT THE SAVE AND IT DOES NOT REWIND THE ARENA. *****
 * Both are the CONTROLLER's decisions and both must happen outside this call:
 * the commit strictly BEFORE it, because step 4 destroys the identity the
 * writer needs; the arena rewind strictly AFTER it, because step 3 is what
 * stops gpSP referencing the buffers that the rewind reclaims. */
void gba_session_end(void);

/* ------------------------------------------------------ relaunch invariants --
 *
 * ***** THIS IS A SEPARATE QUESTION FROM THE FRESH-PROCESS PROBES, AND
 * CONFLATING THE TWO WOULD DESTROY BOTH. *****
 *
 * m2_probe_pre(), m3_bios_probe_pre() and m4_rom_probe_pre() assert VIRGIN .bss:
 * a NULL screen pointer, zero noise tables, an all-zero BIOS region, a zero
 * gamepak_size. Those are the right assertions for the first launch in a
 * process and M16 KEEPS THEM EXACTLY AS THEY ARE, unweakened, on session 1.
 *
 * They are necessarily FALSE on session 2 -- the BIOS is still loaded because
 * reloading it every launch would be waste, the screen is still installed
 * because the menu needs it, the noise tables are still full because
 * init_sound() already ran. Deleting the probes, or softening them to pass in
 * both cases, would throw away the one check that proves .bss really was zeroed
 * at process start.
 *
 * So M16 asks a DIFFERENT question after a teardown: not "is this a virgin
 * process" but "did the session boundary actually release what it claimed to".
 * Every bit below is a FAILURE. A return of 0 means the teardown is complete.
 *
 * ***** NOTHING HERE SILENTLY PASSES. ***** Each bit names one specific piece
 * of state that gba_session_end() is responsible for, and the diagnostic aborts
 * on any of them rather than continuing into a contaminated session. */
unsigned int gba_session_probe_relaunch(void);

#define GBA_SESS_R_BUFCOUNT   (1u << 0)  /* gamepak_buffer_count != 0          */
#define GBA_SESS_R_RFILE      (1u << 1)  /* an RFILE pool slot is still in use */
#define GBA_SESS_R_LATCHED    (1u << 2)  /* save identity still latched        */
#define GBA_SESS_R_ARMED      (1u << 3)  /* restore still armed                */
#define GBA_SESS_R_INPUT      (1u << 4)  /* REG_P1 is not the neutral 0x3FF    */
#define GBA_SESS_R_SRAMOBS    (1u << 5)  /* an observer slot is still taken    */
#define GBA_SESS_R_MINI       (1u << 6)  /* gamepak_mini_materialized still set*/
#define GBA_SESS_R_BASEHASH   (1u << 7)  /* a restore baseline hash survived   */
#define GBA_SESS_R_APPLIED    (1u << 8)  /* restore applied-byte count survived*/

/* ------------------------------------------------ the expected pre-masks ----
 *
 * WHAT THE THREE FRESH-PROCESS PROBES ARE EXPECTED TO RETURN ON A RELAUNCH.
 * The controller compares for EXACT EQUALITY against these, so a bit that is
 * expected is accounted for and ANY OTHER BIT IS STILL A FAILURE. That is the
 * difference between an explicit relaunch invariant and a weakened gate.
 *
 * Each bit is set because the state it names is DELIBERATELY PRESERVED across
 * the session boundary, and each one is justified individually:
 *
 *   M2_PRE_SCREEN_NULL   gba_screen_pixels still points at the app's screen
 *                        buffer. Re-installing it per session would be
 *                        pointless churn; releasing it would leave the menu
 *                        with nothing to draw into.
 *   M2_PRE_NOISE15_ZERO  the two noise tables are still populated because
 *   M2_PRE_NOISE7_ZERO   init_sound() ran in session 1 and its output is
 *                        cartridge-independent.
 *   M3_PRE_BIOS_ZERO     bios_rom still holds the BIOS. It is the same 16 KB
 *                        image for every cartridge.
 *
 * ***** THE BIOS GETS A STRONGER CHECK, NOT A WEAKER ONE. ***** On a relaunch
 * the controller additionally requires m3_bios_probe_content() == 0, which
 * proves the image is still STRUCTURALLY VALID rather than merely non-zero.
 * Session 1 proves "it was clean, then we loaded it"; session 2 proves "it is
 * still the thing we loaded". */
#define GBA_SESS_PRE2_RELAUNCH  (M2_PRE_SCREEN_NULL | \
                                 M2_PRE_NOISE15_ZERO | \
                                 M2_PRE_NOISE7_ZERO)

#define GBA_SESS_PRE3_RELAUNCH  (M3_PRE_BIOS_ZERO)

/* ***** THE ROM PRE-MASK IS THE ONE WITH A REAL FINDING BEHIND IT. *****
 *
 * memory_term() closes the file and releases the buffers, but it does NOT clear
 * gamepak_size and it does NOT unmap the cartridge window -- and neither does
 * init_memory(), whose map_region/map_null calls stop at 0x08000000 and resume
 * at 0xE000000, never covering the gamepak range. So on a relaunch, AFTER a
 * correct and complete teardown, m4_rom_probe_pre() still reports:
 *
 *   M4_PRE_SIZE_SET     gamepak_size is session 1's value
 *   M4_PRE_ROM_MAPPED   memory_map_read[0x08...] still holds session 1 pointers
 *
 * ***** THAT IS SAFE, AND IT IS SAFE FOR A REASON THAT WAS VERIFIED IN THE
 * SOURCE RATHER THAN ASSUMED. ***** load_gamepak_raw() OVERWRITES both before
 * anything can observe them: it recomputes gamepak_size from the new file's
 * measured length, then calls map_null(read, 0x8000000, 0xD000000) to tear the
 * whole window down before re-mapping it page by page. Between the teardown and
 * the next load, no emulated instruction runs -- the CPU is not executing -- so
 * the stale window is never dereferenced.
 *
 * ***** THE TWO BITS THAT MUST *NOT* APPEAR ARE THE ONES THAT MATTER. *****
 * M4_PRE_NO_BUFFERS and M4_PRE_BUF0_NULL are absent from this mask, so the
 * controller's exact-equality comparison FAILS if either shows up. Those are
 * the bits that would fire if the arena rewind had not actually reclaimed the
 * ROM buffers -- which is precisely the blocker M16-0 exists to prove is
 * solved. This mask does not hide a failure; it isolates one. */
#define GBA_SESS_PRE4_RELAUNCH  (M4_PRE_SIZE_SET | M4_PRE_ROM_MAPPED)

/* ***** THE SAME SURVIVING WINDOW, SEEN EARLIER BY THE M3 MAP PROBE. *****
 *
 * m3_bios_probe_map() and m4_rom_probe_pre() test the IDENTICAL condition on
 * the IDENTICAL slot -- memory_map_read[MAP_IDX(0x8000000)] -- so the cartridge
 * window that M4_PRE_ROM_MAPPED accounts for above is ALSO visible to the M3
 * probe, which reports it as M3_MAP_ROM_PRESENT. Since nothing in init_memory()
 * or memory_term() ever unmaps that range, the bit is set from the moment
 * session 1 loads a cartridge until the process ends.
 *
 * The M3 map probe simply runs FIRST, so on a relaunch it is the first gate to
 * observe the survivor. Expecting it here is the same invariant already stated
 * for M4, not a new tolerance.
 *
 * ***** THE TWO BITS THAT ACTUALLY ASSERT THE BIOS ARE NOT IN THIS MASK. *****
 * M3_MAP_BIOS0 and M3_MAP_WINDOW are what prove the BIOS is correctly mapped
 * across all 512 window entries, and they are absent here, so the controller's
 * exact-equality comparison still FAILS if either appears -- on every session,
 * relaunch included. m3_bios_probe_content() is likewise untouched and must
 * still be 0 every time. This mask accounts for one proven survivor; it does
 * not weaken the BIOS check. */
#define GBA_SESS_MAP3_RELAUNCH  (M3_MAP_ROM_PRESENT)

/* ------------------------------------------------------- the RFILE census ---
 *
 * Implemented in adapters/gba/gba_filestream.c, which owns the pool. Both are
 * STRICTLY READ-ONLY and neither alters file behaviour in any way.
 *
 * The leak they exist to catch is invisible until it is fatal: gpSP holds the
 * gamepak file open for demand paging, the pool has four slots, and a launcher
 * that forgot memory_term() would fail on the FIFTH launch with a "pool
 * exhausted" that looks like a corrupt ROM. */
unsigned int gba_rfile_inuse(void);
unsigned int gba_rfile_slots(void);

/* ------------------------------------------------------------ scalar reads --
 *
 * One value at a time, for the UDP log and the on-screen report. A scalar-only
 * accessor, exactly as gba_restore_read() and m12c_sramobs_read() are: no
 * pointer into gpSP state or into this module's state ever crosses the boundary.
 * An unknown selector returns 0. */
unsigned int gba_session_read(unsigned int sel);

#define GBA_SESS_RD_BUFCOUNT     0u  /* gamepak_buffer_count                   */
#define GBA_SESS_RD_RFILE_INUSE  1u  /* RFILE pool slots in use                */
#define GBA_SESS_RD_RFILE_SLOTS  2u  /* RFILE pool ceiling                     */
#define GBA_SESS_RD_LATCHED      3u  /* gba_save_latched()                     */
#define GBA_SESS_RD_ARMED        4u  /* gba_restore armed flag                 */
#define GBA_SESS_RD_REG_P1       5u  /* REG_P1, neutral is 0x3FF               */
#define GBA_SESS_RD_GAMEPAK_SZ   6u  /* gamepak_size                           */
#define GBA_SESS_RD_MINI         7u  /* gamepak_mini_materialized              */
#define GBA_SESS_RD_RESIDENT     8u  /* pages the LRU queue names              */
#define GBA_SESS_RD_FILEBLOCKS   9u  /* gamepak_file_blocks                    */
#define GBA_SESS_RD_LRUHEAD     10u  /* gamepak_lru_head                       */
#define GBA_SESS_RD_SRAMOBS     11u  /* observer samples taken                 */
#define GBA_SESS_RD_BASEHASH    12u  /* restore baseline hash                  */
#define GBA_SESS_RD_APPLIED     13u  /* restore applied byte count             */

/* THE NEUTRAL KEYPAD WORD. gpSP's own reset value, written by init_memory()
 * as write_ioreg(REG_P1, 0x3FF): the GBA keypad is ACTIVE LOW, so all ten
 * buttons released is all ten bits SET. Spelled here so the diagnostic never
 * hard-codes a bare 1023. */
#define GBA_SESS_P1_NEUTRAL  0x3FFu

#endif /* LUAPORT_GBA_SESSION_H */
