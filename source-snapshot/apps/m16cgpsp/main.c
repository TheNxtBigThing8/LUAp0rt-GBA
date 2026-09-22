/* ===========================================================================
 * LUAport M16-2 -- apps/m16cgpsp/main.c
 * THE LAUNCHER, WITH A PICKER THAT COMES BACK.
 * ===========================================================================
 *
 * WHAT THIS IS
 * ------------
 * M13C is the FROZEN launcher: picker -> cartridge -> play -> chord -> commit
 * -> exit. Its _start() is a STRAIGHT LINE and returning from it IS the payload
 * exiting, so ending a game ends LUAport.
 *
 * M16-0 proved, on real hardware, that one payload can run the SAME cartridge
 * three times. M16-1 proved, on real hardware, that one payload can run
 * A -> B -> A with no cartridge, identity, paging or resource leakage.
 *
 * ***** M16-2 ADDS NO NEW MECHANISM. IT ADDS ONE BACKWARD BRANCH. ***** Every
 * primitive below is already hardware-proven:
 *
 *   gba_session_end()              M16-0 PASS, M16-1 PASS  -- used VERBATIM
 *   arena_mark() / arena_rewind()  M16-0 PASS, M16-1 PASS
 *   gba_session_probe_relaunch()   M16-0 PASS, M16-1 PASS
 *   GBA_SESS_PRE{2,3,4}_RELAUNCH   M16-0 PASS, M16-1 PASS
 *   GBA_SESS_MAP3_RELAUNCH         M16-0 PASS (the BIOS-gate finding)
 *   everything else                M13C, copied rather than re-derived
 *
 * ***** WHAT M16-2 DOES NOT TOUCH. *****
 *   apps/m13cgpsp tree          THE FROZEN LAUNCHER. Not one byte.
 *   apps/m16diag tree           M16-0 HARDWARE EVIDENCE. Not one byte.
 *   apps/m16bdiag tree          M16-1 HARDWARE EVIDENCE. Not one byte.
 *   lua/m13c.lua.in             frozen
 *   lua/m16diag.lua.in          evidence
 *   lua/m16bdiag.lua.in         evidence
 *   adapters/gba/gba_session.c  NO CHANGE WAS NEEDED -- see THE BOUNDARY below
 *   adapters/gba/gba_session.h  NO CHANGE WAS NEEDED
 *   gpsp tree                   not one line
 *   the save format, the save policy, the clean verdict, the watchdog
 *
 * ***** THE PICKER MODEL IS INCLUDED, NOT COPIED. ***** apps/m13bgpsp/
 * m13b_picker.inc arrives BYTE-UNCHANGED through -Iapps/m13bgpsp, exactly as it
 * does in M13B and M13C, so tools/m13b_picker_equiv.c's 283 checks prove the
 * picker that ships here too. The re-entry fix this milestone needs is
 * implemented APP-LOCALLY, below, and does not touch that file.
 *
 * ===========================================================================
 * THE ONE STRUCTURAL CHANGE, STATED PLAINLY
 * ===========================================================================
 *
 *   M13C                              M16C
 *   ----                              ----
 *   bring-up                          bring-up                 ONCE
 *   scan + validate                   scan + validate          ONCE
 *   picker                       +--> picker
 *   launch                       |    launch
 *   play                         |    play
 *   commit                       |    commit          <-- SAME POLICY, SAME ORDER
 *   m13c_teardown()  (RUNTIME)   |    gba_session_end()        (SESSION ONLY)
 *   return           (EXIT)      |    arena_rewind(mark)
 *                                |    boundary probes
 *                                +----'
 *                                     m16c_teardown()  (RUNTIME)  ONCE
 *                                     return           (EXIT)
 *
 * ***** THE FOUR-SHOULDER CHORD NOW MEANS "RETURN TO PICKER". ***** The chord
 * itself, the 15-frame hold and the debounce are M13C's, unchanged; only what
 * the resulting `break` falls into has moved.
 *
 * ***** PICKER CIRCLE REMAINS THE PAYLOAD EXIT. ***** It is the only exit that
 * is not the power button and it keeps its own status, M16C_EXIT.
 *
 * ===========================================================================
 * THE SAVE ORDER IS BINDING AND IT IS NOT BROADENED
 * ===========================================================================
 *
 *   gba_restore_finish()
 *   modified = gba_restore_modified()      M12C's POST-RESTORE comparison
 *   clean    = <the SEVEN-TERM conjunction, verbatim>
 *   if (clean && modified) { gba_save_finish(); gba_savefile_commit(); }
 *   -------------------------------------------------------------------
 *   gba_session_end()        <-- step 4 of it is gba_save_unlatch()
 *   arena_rewind(mark)
 *   gba_session_probe_relaunch() == 0
 *   -------------------------------------------------------------------
 *   the picker
 *
 * ***** RETURNING TO THE PICKER IS NOT SAVE PERMISSION. ***** The chord sets
 * end_reason = OPERATOR, which is ONE of seven terms and never a commit trigger
 * on its own. A watchdog, an overrun, a frameskip, a lost map or arena growth
 * all leave `clean` at 0, and an unclean session commits NOTHING on its way out.
 *
 * ***** THE COMMIT MUST PRECEDE THE UNLATCH, AND THAT IS STRUCTURAL HERE. *****
 * gba_session.c:115-126 is explicit: the commit path builds
 * /savedata0/gba/<ID>.sav from the identity gba_save_unlatch() destroys, and an
 * out-of-order controller gets GBA_SAVEFILE_ENOID -- a refusal, not a misfile,
 * but the save is still lost. gba_session_end() is called at step 458, which is
 * AFTER step 456 on every path in this file including the failing ones.
 *
 * ===========================================================================
 * THE SESSION BOUNDARY: WHY gba_session.c NEEDED NO CHANGE
 * ===========================================================================
 *
 * gba_session_end() already does exactly the seven session-scoped things, and
 * it already excludes exactly the three things a save-aware controller must own:
 *
 *   the commit             ours, strictly BEFORE      (step 456)
 *   arena_rewind()         ours, strictly AFTER       (step 458)
 *   m4_rom_backup_init()   session START, not end     (step 451, before the load)
 *
 * It does NOT call plat_video_shutdown(), plat_pad_shutdown() or
 * plat_audio_shutdown() (gba_session.c:156-158) -- which is precisely the
 * "do not tear the runtime down to show a menu" requirement. The only thing
 * M16-1 could not exercise was a REAL commit preceding it, and that is a
 * CONTROLLER ordering obligation, not a change to that file.
 *
 * ===========================================================================
 * GLYPH COVERAGE -- READ runtime/gfx.h:30-46 BEFORE EDITING ANY STRING
 * ===========================================================================
 * font_data covers ASCII 32..90. Available: A-Z 0-9 space . : - / + , ( ) < = >
 * ? !   NOT available (they render as a BLANK CELL, silently): "  #  $  %  &  *
 * ;  @   Lowercase is FOLDED TO UPPERCASE. UNDERSCORE (95) IS OUTSIDE THE TABLE
 * and renders blank, so the save identity <CODE>_<HASH8> reads with a gap on
 * screen and is byte-exact in the UDP log. That is M13C's behaviour and is NOT
 * worked around.
 * ========================================================================= */

#include "core.h"
#include "shim.h"
#include "platform.h"
#include "gfx.h"

#include <stdio.h>

/* For strcmp() only -- the identity-survival comparison across reset #2.
   runtime/libc/string.h declares it, runtime/shim.c defines it, shim.o is
   already on the link line. No new object, no new undefined symbol. */
#include <string.h>

#include "boot.inc"
#include "fault.inc"

#include "m13store.h"
#include "gba_library.h"

/* THE TYPE BOUNDARY. Every one of these headers is deliberately free of u64/s64
   typedefs, which is what lets them sit beside runtime/core.h in one translation
   unit. NO gpSP HEADER IS INCLUDED HERE. See gba_rom.h:6-24. */
#include "gba_rom.h"
#include "gba_probe.h"
#include "gba_bios.h"
#include "gba_save.h"

/* The gameplay half. M13C's include set PLUS gba_r7input.h.
   ***** THE ONE ADDITION IS R7's GAMEPLAY BUTTON-ALIAS LAYER, AND IT IS NOT
   A REPLACEMENT FOR gba_input.h. ***** Both are included because both are
   used: the alias layer supplies the per-frame pad translation, while
   gba_input.h still supplies m8_input_apply(), m8_input_irq_edge() and the
   M8_* bit names this file reads. adapters/gba/gba_input.{c,h} are NOT
   modified by R7 -- they are compiled into the frozen M13C image from the
   same source, so an edit there would move a frozen baseline. */
#include "gba_exec.h"
#include "gba_present.h"
#include "gba_input.h"
#include "gba_r7input.h"
#include "gba_m8map.h"
#include "gba_m10map.h"
#include "audio.h"
#include "gba_audio.h"

/* The persistence half. IDENTICAL include set to M13C's.
   ***** THIS IS WHERE M16C DIVERGES FROM M16-0 AND M16-1 ON PURPOSE. *****
   Those two diagnostics deliberately did NOT link a save writer, because they
   were still LEARNING whether the identity followed the cartridge. Both have now
   PASSED on hardware, so M16-2 carries the real persistent-save stack -- that is
   the whole point of the milestone. */
#include "savedata.h"
#include "gba_savefile.h"
#include "gba_restore.h"
#include "gba_restorefile.h"
#include "gba_sramobs.h"

/* ***** THE SESSION BOUNDARY, UNCHANGED FROM M16-0. ***** This header refuses
   to compile without -DLUAPORT_SESSION_REUSE (gba_session.h:22-25), so it can
   never reach a frozen milestone even by accident. */
#include "gba_session.h"

/* THE FRAMESKIP GATE, READ DIRECTLY -- apps/m10gpsp/main.c:553-564 verbatim.
   adapters/gba/gba_fixture.c:70 defines `u32 skip_next_frame = 0;`. It is
   LUAport-owned and `u32` is `unsigned int` on both sides of the type boundary.
   M16C NEVER WRITES IT; it asserts it every frame. */
extern unsigned int skip_next_frame;

/* ***** gpSP'S CARTRIDGE MAP, DECLARED LOCALLY AND NOT INCLUDED. *****
   gpsp/gba_memory.h:282 declares `extern u8 *memory_map_read[8 * 1024];` and
   gpsp/common.h:94 typedefs u8 as `unsigned char`, so the declaration below is
   TYPE-IDENTICAL to the definition. The header cannot simply be included here:
   gpsp/common.h:100 typedefs u64 as `unsigned long long int` while
   runtime/core.h:32 typedefs it as `unsigned long`, and one translation unit
   holding both fails on the conflicting typedef. This is exactly the idiom line
   181 above already uses for skip_next_frame, and the one
   adapters/gba/gba_session.c:68 uses for memory_term(). M16C WRITES THIS ARRAY
   IN EXACTLY ONE PLACE -- m16c_clear_map_tail() below -- and over exactly 512
   slots that no other writer in the process ever clears. */
extern unsigned char *memory_map_read[8 * 1024];

/* ***** THE STALE CARTRIDGE MAP-TAIL BAND. HARDWARE-CONFIRMED. *****
 *
 * These are the ONLY slots that gpSP's map_rom_entry() can write and that
 * NOTHING in the process ever clears. The bound is DERIVED, not assumed:
 *
 *   map_rom_entry()'s 0x0C loop (gba_memory.c:2254-2263) writes
 *   6144 + idx + mcount, with idx <= B-1 and mcount a multiple of B below 512.
 *     - For B >= 512 only mcount == 0 runs, so slot <= 6144 + B-1, whose
 *       maximum is 6144 + 1023 = 7167, attained ONLY by B = 1024 (a 32 MB
 *       cartridge).
 *     - For B <= 511, slot <= 6144 + (B-1) + 511 <= 7165.
 *   The 0x0A loop maxes at 7165 and the 0x08 loop at 6141.
 *   So 7167 is the true maximum reachable slot.
 *
 *   load_gamepak_raw()'s map_null(read, 0x8000000, 0xD000000)
 *   (gba_memory.c:2771) clears 4096..6655 and STOPS at 6655, because
 *   0xD000000 / 0x8000 == 6656 and the loop bound is exclusive.
 *
 *   init_memory() (gba_memory.c:2393-2454) nulls 2560..4095 and 7168..8191
 *   (0xE000000 / 0x8000 == 7168). IT NEVER TOUCHES 4096..7167.
 *
 * 6656..7167 is therefore the EXACT uncovered band, with no slack on either
 * side. gba_m10map.h:196-199 states the assumption this breaks in its own
 * words -- that these slots "are NULL from .bss zero-initialisation" -- which
 * is true for session #1 ONLY. A relaunching payload never re-zeroes .bss.
 *
 * DO NOT WIDEN THIS RANGE. Slot 4096 in particular MUST be left alone: on a
 * relaunch m4_rom_probe_pre() is EXPECTED to report M4_PRE_ROM_MAPPED, and
 * clearing 4096 would trip m16c_runtime_fatal(M16C_PRE_UNEXPECTED) -- a hard
 * payload stop with no return to the picker. */
#define M16C_MAPTAIL_LO  6656u
#define M16C_MAPTAIL_HI  7167u

/* ***** THE INTERNAL IDENTITY. THE PLAYER NEVER SEES THIS STRING. ***** It WAS
   the on-screen title, and the comment here said so, until the masthead learned
   to spell the brand exactly. The title bar now draws "LUAp0rt GBA" from
   m16c_wordmark, because runtime/gfx.c:28 folds lowercase before the font
   lookup and therefore NO string literal can render the real brand.

   ***** IT IS STILL LIVE, AND THAT IS THE POINT. ***** It is logged once at
   startup (step 441) -- the smallest existing developer diagnostic that can
   carry it -- and m16c-verify greps rodata.bin for "LUAPORT GBA"
   (Makefile:16303). That gate is neither edited nor weakened; this reference is
   what keeps it honest. Still identical to M13C's, and still carrying no
   version stamp: a number here would be the first thing to drift out of date. */
#define M16C_TITLE "LUAPORT GBA"

/* ***** THE STARTUP SUBTITLE. PRESENTATION ONLY, AND DELIBERATELY NOT A VERSION
   STAMP. ***** It says what the payload IS, in the operator's words, on the one
   screen that exists before the library is presented. It carries no number for
   exactly the reason M16C_TITLE carries none -- a stamp on a splash is the first
   string to drift out of date -- and it is read by m16c_draw_splash() and by
   nothing else, so it cannot leak into an error heading or a ledger line. */
#define M16C_SUBTITLE "GAME BOY ADVANCE FOR PLAYSTATION 5"

/* IDENTICAL TO M4's THROUGH M16-1's. The ROM buffers are carved out of this and
   it is NOT raised by this milestone -- the thesis is that session N+1 fits in
   the SAME arena session N used, so growing it would delete the finding. */
#define ARENA_SIZE (4 * 1024 * 1024)

/* ------------------------------------------------------------- statuses ---
 *
 * -601..-635, a band DISJOINT from every existing image: M11/M12 end at -284,
 * M13C occupies -401..-428, M16-0 -501..-519, M16-1 -501..-528 and M14 -501..
 * -528. Nothing anywhere in the repository uses -6xx. Steps 440-459 are
 * likewise disjoint from M13C's 360-377 and M16-1's 400-409, so a number read
 * off a photograph can never be attributed to the wrong image.
 *
 * ***** THE BAND IS SPLIT BY FAILURE CLASS, NOT BY EXECUTION ORDER. ***** That
 * is deliberate and it is the one place this file departs from M13C's numbering
 * style: in a looping launcher the SINGLE most important property of a status is
 * whether it ends the payload or merely ends the game, so the number itself
 * carries that. -601..-618 end the payload. -619..-623 are boundary failures and
 * ALSO end the payload. -624..-635 abandon the cartridge and return to the
 * picker. */
#define M16C_OK                  0

/* ---- RUNTIME-FATAL: payload-scoped, LUAport cannot safely continue -------- */
#define M16C_NO_MMAP          -601
#define M16C_BOOT_FAILED      -602
#define M16C_NO_ARENA         -603
#define M16C_NO_SYMS          -604   /* getdents/open could not be resolved    */
#define M16C_SD_INIT_BAD      -605   /* plat_savedata_init() refused           */
#define M16C_SCAN_FAILED      -606   /* the ROM directory would not enumerate  */
#define M16C_NO_ROMS          -607   /* enumerated fine, zero .gba present     */
#define M16C_NO_VIDEO         -608
#define M16C_NO_PAD           -609
#define M16C_EXIT             -610   /* CIRCLE at the picker -- A CLEAN EXIT   */
#define M16C_PICKER_TIMEOUT   -611   /* nobody chose anything inside the bound */
#define M16C_NO_SELECTABLE    -612   /* every entry was rejected by the ROM gate*/
#define M16C_TIME_INIT_BAD    -613   /* HOISTED: the clock is payload-scoped   */
#define M16C_CLOCK_DEAD       -614   /* HOISTED: init said OK and it read 0    */
#define M16C_NO_AUDIO         -615   /* the audio PORT is payload-scoped       */
#define M16C_BIOS_FAILED      -616   /* the BIOS is loaded ONCE, per payload   */
#define M16C_COMMIT_FAILED    -617   /* a REQUIRED commit did not complete     */
#define M16C_RO_NOT_BACK      -618   /* THE READ-ONLY MOUNT DID NOT COME BACK  */

/* ---- RUNTIME-FATAL: the session boundary was contaminated ----------------
 *
 * ***** THESE FIVE FAIL CLOSED AND THEY FAIL HARD. ***** A boundary that did
 * not fully release is the one condition under which starting another cartridge
 * would carry session N's state into session N+1 -- and in the case of
 * M16C_IDENTITY_STALE that means committing the NEXT cartridge's memory into
 * THIS one's save file. There is no safe way to continue, so the payload stops.
 */
#define M16C_BOUNDARY_DIRTY   -619   /* gba_session_probe_relaunch() != 0      */
#define M16C_ARENA_LEAK       -620   /* the watermark did not return           */
#define M16C_ARENA_REWIND_BAD -621   /* arena_rewind() refused a mark we made  */
#define M16C_IDENTITY_STALE   -622   /* THE DATA-LOSS ONE -- still latched     */
#define M16C_SESSION_DIRTY    -623   /* the per-session reset did not take     */

/* ---- SESSION-FATAL: abandon this cartridge, return to the picker ---------
 *
 * ***** EVERY ONE OF THESE STILL RUNS THE COMPLETE BOUNDARY BEFORE RETURNING.
 * ***** M16-1's finding makes that non-negotiable for the two ROM failures in
 * particular: gpSP leaves the PREVIOUS mapping intact when a load fails, so
 * returning to the picker without memory_term() would leave the last
 * cartridge's bytes mapped under the next cartridge's identity. */
#define M16C_NAME_TOO_LONG    -624   /* the chosen path would not fit          */
#define M16C_NO_BUFFERS       -625   /* m4_rom_buffer_init() gave fewer than 2 */
#define M16C_ROM_BAD_SOURCE   -626   /* would not open / failed the source gate*/
#define M16C_ROM_LOAD_FAIL    -627
#define M16C_NO_LATCH         -628   /* gba_save_latch() did not take          */
#define M16C_ASSERT_FAILED    -629   /* some probe bitmask was non-zero        */
#define M16C_MAP_LOST         -630   /* mapping did NOT survive reset #2       */
#define M16C_ID_DRIFT         -631   /* save identity CHANGED across reset #2  */
#define M16C_BUFFERS_LOST     -632   /* buffer count fell below 2 after reset  */
#define M16C_EXEC_UNSAFE      -633   /* the pre-execution gate failed          */
#define M16C_RUN_UNSTABLE     -634   /* watchdog / overrun / frameskip / map   */
#define M16C_PRE_UNEXPECTED   -635   /* a relaunch pre-probe returned an
                                        unaccounted bit -- SESSION-fatal on the
                                        first session, boundary-fatal after     */

/* ---- THE USER-FACING HEADINGS ------------------------------------------
 *
 * M13C's vocabulary, verbatim, plus three the loop needs. An operator can act on
 * "BIOS NOT FOUND" and cannot act on "-616 at step 451". */
#define M16C_H_MEMORY     "SYSTEM MEMORY UNAVAILABLE"
#define M16C_H_STORAGE    "SAVE STORAGE UNAVAILABLE"
#define M16C_H_LIBRARY    "ROM LIBRARY UNREADABLE"
#define M16C_H_VIDEO      "VIDEO INIT FAILED"
#define M16C_H_PAD        "CONTROLLER NOT AVAILABLE"
#define M16C_H_AUDIO      "AUDIO INIT FAILED"
#define M16C_H_BIOS       "BIOS NOT FOUND"
#define M16C_H_ROM_OPEN   "ROM COULD NOT BE OPENED"
#define M16C_H_ROM_BAD    "ROM INVALID"
#define M16C_H_SAVE_BAD   "SAVE CORRUPT"
#define M16C_H_SAVE_WRITE "SAVE WRITE FAILED"
#define M16C_H_NOTREADY   "EMULATOR NOT READY"
#define M16C_H_SESSION    "SESSION ENDED UNEXPECTEDLY"
#define M16C_H_NOROMS     "NO GBA ROMS FOUND"
#define M16C_H_TIMING     "TIMING INIT FAILED"
#define M16C_H_MAPPING    "ROM MAPPING FAILED"
#define M16C_H_WATCHDOG   "WATCHDOG EXIT"
/* ***** THE BOUNDARY GETS ITS OWN HEADING BECAUSE ITS REMEDY IS DIFFERENT.
   ***** Everything else on this list is "try again". This one is "LUAport
   stopped itself because continuing could destroy a save". */
#define M16C_H_BOUNDARY   "SESSION DID NOT RELEASE"

/* ---------------------------------------------------------- the ROM source --
 * M13C's source layer, unchanged. gba_lib_scan() takes a directory string, so a
 * future library layer replaces exactly these two lines. */
#define M16C_ROM_DIR   "/temp0"
#define M16C_ROM_LABEL "/TEMP0"

/* ================================= THE DIAGNOSTIC PRESENTATION SWITCH =======
 *
 * ***** POLISH-0. ONE COMPILE-TIME FLAG, DEFAULT OFF, PRESENTATION ONLY. *****
 *
 * M17 IS COMPLETE AND PASSED ON HARDWARE. Its Tier-1B audio envelope was read
 * off the SESSION COMPLETE screen and the numbers it produced are evidence that
 * already exists. What remains is a launcher that shows a player twelve rows of
 * ring telemetry every time they finish a game -- which is exactly the
 * diagnostic-harness feel this polish pass exists to remove, and exactly the
 * argument this file already makes for itself at the SRAM capture screen: a
 * player must not be shown forensics before being told their save was written.
 *
 * ***** WHAT THIS FLAG DOES AND, MORE IMPORTANTLY, WHAT IT DOES NOT. *****
 * It gates DRAW CALLS. Nothing else. It is deliberately NOT a measurement flag:
 *
 *   - gba_audio_stats() is still called exactly once, where it always was
 *   - aud.overruns is still a term of the seven-term clean verdict
 *   - aud_drift_frames / aud_drift_ms are still computed, unconditionally
 *   - plat_audio_submitted() / plat_audio_errors() still count
 *   - the printf/klog reporting block still emits every one of these figures
 *
 * So a build with the flag OFF measures and LOGS precisely what a build with it
 * ON measures and logs. The ONLY difference is whether a player sees it. That
 * separation is the whole reason this is a switch rather than a deletion: the
 * instrumentation stays available for a future audio investigation at the cost
 * of one #define, and nobody has to reconstruct it from a diff.
 *
 * ***** SET IT TO 1 TO GET M17's SCREEN BACK, UNCHANGED. ***** The gated block
 * below is byte-for-byte the block M17 photographed, including its layout, its
 * colours and its deliberate refusal to colour UNDERRUN as a fault. */
#ifndef M16C_SHOW_AUDIO_DIAG
#define M16C_SHOW_AUDIO_DIAG 0
#endif

/* ----------------------------------------------------------- the bounds ----
 *
 * ***** THE PICKER BOUND IS PER ENTRY, NOT CUMULATIVE. ***** 36,000 frames is
 * ~10 minutes at 60 Hz, carried from M13B/M13C unchanged. In a looping launcher
 * the counter is reset on EVERY picker entry (see m16c_picker_reentry), so an
 * operator who plays three games does not find the picker timing out sooner each
 * time. */
#define M16C_PICKER_MAX_FRAMES 36000u

/* ***** THE STARTUP IDENTITY SCREEN REPLACES FRAMES THAT WERE ALREADY BEING
   PRESENTED, AND ADDS NO NEW WAITING. ***** The region at step 446 already fills
   both framebuffers and presents M16C_BLACK_FRAMES blind black frames to get the
   scanout running before the pad is opened; those frames were spent showing the
   operator nothing. This constant governs the same kind of loop in the same
   place, drawing an identity screen instead of a blank one.

   It is DRAW-ONLY: no pad is read (plat_pad_init() has not even been called yet
   at this point in step 446), there is no skip, no sleep and no blocking call --
   the loops' only exit is their own frame counts, so they cannot hang a boot. It
   is also INDEPENDENT OF THE LIBRARY: gba_lib_scan() ran at step 445 and its
   result is deliberately NOT shown here, because the picker states the ROM count
   one frame later and a splash that had to answer "what do I say when there are
   zero?" would be a second place to get the library's story wrong.

   ***** THE THREE PHASES ARE FRAME COUNTS, NOT TIMES, BECAUSE THE PRESENT IS
   VSYNC-BLOCKING. ***** plat_video_init() sets sceVideoOutSetFlipRate(h, 0) --
   "every vsync" (runtime/platform.c:301) -- and plat_video_present() then blocks
   on sceKernelWaitEqueue once per flip (runtime/platform.c:121-127). At 1080p60
   that is 60.0 Hz exactly, so a frame count IS a duration and no clock has to be
   read. The unpaced fallback (runtime/platform.c:128-141) sleeps 16,000 us =
   62.5 Hz, which makes every phase ~4% SHORTER, never longer -- both paths land
   inside the intended feel, so this screen does not depend on which branch the
   console takes.

       18 frames   0.300 s @ 60 Hz   0.288 s unpaced    fade in
      150 frames   2.500 s @ 60 Hz   2.400 s unpaced    hold, full brightness
       18 frames   0.300 s @ 60 Hz   0.288 s unpaced    fade out
      ---------   -------
      186 frames   3.100 s

   ***** R3 RAISED THE HOLD FROM 108 TO 150, AND ONLY THE HOLD. ***** The R3
   hardware test showed the completed screen reading as correct but passing
   before it could be taken in: 1.8 s is long enough to SEE the splash, not
   long enough to LOOK at it now that the portrait, the handheld and the
   slogan all want attention within the same still. 150 frames is 2.5 s.
   The two 18-frame ramps are deliberately UNCHANGED -- they are endpoint-
   exact for the reason stated below, and lengthening a fade would have
   changed how the screen ARRIVES rather than how long it STAYS.

   ***** 18 IS CHOSEN SO THE RAMP LANDS EXACTLY ON BOTH ENDPOINTS. ***** The
   ramp is (i * 256) / (FRAMES - 1), so with 18 frames the divisor is 17 and i
   sweeps 0..17 -- giving k = 0 on the first frame (exact black, see
   m16c_fade_surface) and k = 256 on the last (the untouched surface). A count
   whose divisor did not divide the sweep would leave the screen either not
   quite black or not quite lit at the ends, which is the one artefact this
   screen exists to avoid. */
#define M16C_SPLASH_FADE_IN       18u
#define M16C_SPLASH_HOLD         150u
#define M16C_SPLASH_FADE_OUT      18u

#define M16C_BLACK_FRAMES          8u
#define M16C_LOADING_FRAMES        8u

/* ***** R4 RETIRED M16C_INFO_FRAMES (120) AND REPLACED IT WITH A BOUND THAT
   CANNOT LAUNCH. ***** The details screen used to be a 120-frame draw-only
   loop with NO pad read at all: it presented the panel for ~2 s and then fell
   through into the session whatever the operator did. That is what made the
   launch automatic, and it is the single behaviour R4 exists to remove.

   The screen is now a CONFIRMATION, so its bound changes meaning completely:

     BEFORE   120 frames, expiry == LAUNCH   (the normal path -- a timer)
     AFTER  36000 frames, expiry == CANCEL   (the fail-safe -- never a timer)

   ***** EXPIRY IS A CANCEL, AND THAT IS THE WHOLE POINT. ***** A bound whose
   expiry launched would be the automatic launch wearing a longer number. The
   operator's normal experience is therefore "the panel stays up until I press
   something", because 36,000 frames is ~10 minutes at 60 Hz and nobody reads a
   six-row panel for ten minutes. It exists ONLY so an unattended console with a
   cartridge open and a save identity latched does not sit there forever -- the
   same reason M16C_PICKER_MAX_FRAMES exists, carried here unchanged in value.

   ***** IT IS NOT A DEBOUNCE AND NOTHING SLEEPS. ***** The held-button
   protection below is release/edge based (see M16C_DETAILS_ARM_MASK); this
   constant plays no part in it. */
#define M16C_DETAILS_MAX_FRAMES 36000u

/* What the confirmation screen decided. NONE is also what a timeout leaves
   behind, which is deliberate: the post-loop test is `!= LAUNCH`, so the
   fail-safe and the operator's CIRCLE converge on ONE cancel path rather than
   two that could drift apart. */
#define M16C_DET_NONE     0
#define M16C_DET_LAUNCH   1
#define M16C_DET_CANCEL   2
/* ***** M17 TIER-1B RAISED THIS, AND ONLY THIS, FROM 165. ***** 165 frames is
   2.76 s at the GBA's native rate: long enough to READ "SESSION COMPLETE",
   far too short to PHOTOGRAPH a ten-row diagnostic block. The evidence path
   for this project is a PS5 screenshot, so a screen that cannot be captured
   is a screen that produces no evidence -- which is precisely how the first
   M17 Tier-1 hardware run lost its numbers.
   600 frames is ~10.05 s. NOTHING IS FORCED ON THE OPERATOR: the CIRCLE test
   at the top of the summary loop already breaks out early and is unchanged,
   so anyone who does not want the diagnostic dismisses it instantly. This
   constant has exactly ONE reader (the clean-summary loop at step 457), so
   raising it cannot affect the picker, the fail screens, the capture screen,
   the session boundary or the watchdog.

   ***** POLISH-0 MAKES THE FIGURE FOLLOW THE SCREEN IT WAS RAISED FOR. *****
   600 frames exists for ONE REASON, stated above: a ten-row diagnostic block
   cannot be photographed in 2.76 s. With M16C_SHOW_AUDIO_DIAG == 0 there is no
   ten-row block, and holding a FOUR-LINE confirmation for 10.05 s is not a
   confirmation, it is a wait. The pre-M17 figure of 165 is therefore restored
   for the normal-user build and 600 is kept EXACTLY for the diagnostic one, so
   turning the flag on reproduces M17's screen AND M17's capture window
   together. Neither value is new and the early-out is untouched: the CIRCLE
   test at the top of the loop still dismisses the screen instantly, so nothing
   here is forced on the operator in either build. */
#if M16C_SHOW_AUDIO_DIAG
#define M16C_SUMMARY_FRAMES      600u
#else
#define M16C_SUMMARY_FRAMES      165u
#endif
#define M16C_FAIL_FRAMES        1800u
#define M16C_CAPTURE_FRAMES     1800u

/* -------------------------------------------------- THE EMERGENCY WATCHDOG --
 *
 * ***** NOT REDESIGNED, NOT RESIZED, NOT RE-DERIVED. ***** M13C's derivation
 * stands in full and is not restated here: 240 minutes at an integer 60 Hz
 * ceiling = 864,000 iterations, one iteration is exactly one emulated frame
 * because execute_arm()'s only return path is the frame wrap, and the backstop
 * is an EMERGENCY rather than a play limit.
 *
 * ***** WHAT M16-2 ADDS IS ONE SENTENCE. ***** A watchdog exit is SESSION-fatal
 * here rather than payload-fatal, and it still:
 *   - leaves end_reason at WATCHDOG, so `clean` is 0
 *   - commits NOTHING
 *   - names itself on screen with M16C_H_WATCHDOG
 *   - runs the complete session boundary before the picker comes back
 * The established rule that watchdog termination is not a clean operator exit
 * and gains no save-commit permission is preserved EXACTLY.
 *
 * ***** THE PROGRESS-BASED HANG DETECTOR IS STILL DEFERRED. ***** It is M17's
 * and it is deliberately NOT implemented here. */
#define M16C_WD_MINUTES          240u
#define M16C_WD_SECONDS          ((unsigned)(M16C_WD_MINUTES * 60u))
#define M16C_WD_FPS_CEIL          60u
#define M16C_PLAY_MAX_ITERS \
    ((unsigned)(M16C_WD_SECONDS * M16C_WD_FPS_CEIL))

/* THE ARITHMETIC IS CHECKED BY THE COMPILER, NOT BY THE COMMENT ABOVE. */
_Static_assert(M16C_WD_SECONDS == 14400u,
               "the watchdog target drifted from 240 minutes");
_Static_assert(M16C_PLAY_MAX_ITERS == 864000u,
               "the watchdog drifted from 14,400 s * 60 Hz = 864,000 iterations");
_Static_assert(M16C_PLAY_MAX_ITERS >= 10u * 60u * 60u,
               "the watchdog fell below ten minutes of gameplay");

#define M16C_PLAY_WARMUP          10u
#define M16C_MAP_PROBE_EVERY     600u

/* ***** THE CHORD AND THE HOLD ARE M13C's, UNCHANGED. ***** All four shoulders,
   HELD for 15 frames. The hold is what stops a brush against the pad from ending
   the session; the chord is one no cartridge asks the player to make. ONLY THE
   MEANING OF THE RESULTING BREAK HAS MOVED -- it now falls into the session
   boundary and the picker instead of into a runtime teardown. */
#define M16C_EXIT_CHORD        PAD_CHORD
#define M16C_EXIT_HOLD          15u

/* ***** THE RE-ENTRY RELEASE GATE. THE ONE GENUINELY NEW INPUT RULE. *****
 *
 * M13C sets prev_buttons = 0 before the picker loop, so on the FIRST frame
 * `pressed = pad.buttons & ~0` reports every button that HAPPENS TO BE HELD as a
 * fresh press. In M13C that is harmless: the picker is entered once, from a cold
 * start, with nobody touching the pad.
 *
 * ***** IN M16-2 THE PICKER IS RE-ENTERED DIRECTLY OUT OF A FOUR-SHOULDER HOLD.
 * ***** On the return frame PAD_L1 and PAD_R1 would BOTH fire as presses, moving
 * the cursor -16 and +16 in the same frame -- net zero in the middle of a long
 * list, but a real jump at either end -- and a held PAD_CIRCLE would exit the
 * payload outright. So the picker is gated until the pad is quiet.
 *
 * 12 consecutive clear frames is ~0.2 s: long enough to ride out contact bounce
 * on four shoulder switches releasing at slightly different instants, short
 * enough that an operator who has already let go never notices it. The 600-frame
 * ceiling (~10 s) exists so a STUCK button cannot park the launcher forever --
 * and expiry is NOT fatal, because priming prev_buttons from the live pad read
 * makes a held button harmless whether it was ever released or not. */
#define M16C_RELEASE_SETTLE       12u
#define M16C_RELEASE_MAX_FRAMES  600u

/* The buttons that must be quiet before the picker accepts input. The chord is
   what the operator was just holding; CROSS and CIRCLE are the two that would
   do something IRREVERSIBLE (relaunch, or exit the payload) on a phantom edge. */
#define M16C_RELEASE_MASK  (PAD_CHORD | PAD_CROSS | PAD_CIRCLE)

/* ***** THE DETAILS SCREEN'S ARMING MASK -- THE SAME IDEA, A NARROWER SET.
 * *****
 *
 * The confirmation screen is reached by PRESSING CROSS in the picker, so the
 * button that opens it is the same button that would confirm it. The screen
 * therefore refuses to act on ANY input until CROSS and CIRCLE have both been
 * observed clear for M16C_RELEASE_SETTLE consecutive frames -- the identical
 * 12-frame settle the picker re-entry uses, reused rather than re-tuned.
 *
 * ***** THE CHORD IS DELIBERATELY NOT IN THIS MASK. ***** PAD_CHORD is in
 * M16C_RELEASE_MASK because the picker is re-entered directly out of a
 * four-shoulder hold and those buttons would page the list. Nothing of the kind
 * is true here: this screen is entered from a CROSS press during normal picker
 * use, the chord means nothing to it, and a shoulder button resting under a
 * finger would block arming forever for no reason. The two buttons in this mask
 * are exactly the two this screen acts on.
 *
 * ***** ARMING IS THE COURTESY; THE PRIMED EDGE IS THE CORRECTNESS. ***** As at
 * the picker, prev-buttons is primed from a LIVE pad read on entry, so a held
 * button produces no press edge even if this gate never closes. */
#define M16C_DETAILS_ARM_MASK  (PAD_CROSS | PAD_CIRCLE)

/* How the session ended. ***** OPERATOR IS THE ONLY CLEAN ENDING. ***** Every
   other value is a failure and none of them may commit or report success.

   ***** 0 IS RETIRED AND CAN NEVER BE PRODUCED. ***** It was M13B-5's DURATION
   exit. The value is left VACANT rather than reused so an old log reading
   "END REASON 0" stays instantly identifiable as pre-M13C output. */
#define M16C_END_RETIRED         0u
#define M16C_END_OPERATOR        1u
#define M16C_END_WATCHDOG        2u
#define M16C_END_OVERRUN         3u
#define M16C_END_SKIPFRAME       4u
#define M16C_END_MAPFAIL         5u
#define M16C_END_ARENA           6u

/* ---- THE COMMIT-SIDE SRAM CAPTURE VERDICT ---------------------------------
 * M13C's, verbatim. ***** THIS IS AN OBSERVATION AND IT GRADES NOTHING. *****
 * No value here can change ext->status, fail a session or alter one byte of what
 * is written. */
#define M16C_CAP_NOTTAKEN        0u
#define M16C_CAP_TRUNCATED       1u
#define M16C_CAP_REFUTED         2u
#define M16C_CAP_FAMILY          3u
#define M16C_CAP_INCONCLUSIVE    4u

/* ---------------------------------------------------- the GBA screen buffer --
 * gba_probe.h:57-62: 240 * 161 16-bit pixels. THE TRAILING SCANLINE IS NOT
 * PADDING -- gpsp/common.h:118-119 reserves it for winobj rendering. THIS IS THE
 * LIVE RENDER TARGET AND M16C ALLOCATES NO SECOND ONE. It is PAYLOAD-SCOPED:
 * installed once with m2_set_screen() and deliberately preserved across every
 * session boundary, which is exactly what M2_PRE_SCREEN_NULL accounts for in
 * GBA_SESS_PRE2_RELAUNCH. */
#define M16C_SCREEN_PIXELS (240 * 161)

/* ------------------------------------------------------- the picker model --
 *
 * ***** THIS INCLUDE MUST PRECEDE THE STATE BLOCK BELOW. ***** m13b_picker.inc
 * is the SOLE OWNER of `struct m13b_item`, and m16c_items[] is an array OF that
 * type -- C requires the element type to be COMPLETE at the point the array is
 * defined.
 *
 * ***** IT IS THE M13B FILE, BYTE UNCHANGED, REACHED THROUGH -Iapps/m13bgpsp.
 * ***** Not a copy, and NOT MODIFIED by this milestone. The re-entry fix lives
 * in m16c_picker_reentry() below, in this file, where it belongs: it is a
 * property of how a LOOPING launcher enters the picker, not a property of the
 * picker model, and putting it in the .inc would change code that three
 * hardware-passed images share. */
#include "m13b_picker.inc"

/* ================================================================= state == */

static struct m13store    m16c_store;
static struct gba_lib     m16c_lib;
static u8                 m16c_dirbuf[M13STORE_DIRBUF];
static u16                m16c_gba_screen[M16C_SCREEN_PIXELS];

/* THE UI SURFACE. 480*270*4 = 518,400 bytes of .bss, CALLER-OWNED: runtime/gfx.c
   takes `u32 *scr` and allocates nothing. Composing at 480x270 and scaling 4x in
   blit_ui() keeps the per-frame cost to one integer replicate.
   ***** blit_ui() COVERS THE WHOLE 1920x1080 FRAMEBUFFER. ***** That is what
   lets the picker come back over a framebuffer the previous session left filled
   with GBA_PRESENT_BORDER, with no extra clear. */
static u32                m16c_surface[UI_W * UI_H];

/* Parallel to the library table and indexed by the SAME SCAN INDEX, never by a
   sorted display position. PAYLOAD-SCOPED: built once at step 447 and reused on
   every picker return -- M16-2 deliberately does NOT rescan when a game ends. */
static struct m13b_item   m16c_items[GBA_LIB_MAX_ENTRIES];

/* ***** THE LATCHED PATH. ***** Copied ONCE PER SESSION, from gba_lib_path(), by
   m16c_capture(), which REFUSES rather than truncates. Nothing else writes it
   and no display buffer can reach it. A LABEL MAY BE SHORTENED; A PATH MAY NOT. */
static char               m16c_chosen[M13STORE_PATH_MAX];
static char               m16c_chosen_name[M13STORE_PATH_MAX];

static unsigned           m16c_valid;
static unsigned           m16c_rejected;

/* ---- the display state, file scope so the failure screens can reach it ----- */
static u32               *m16c_fb0;
static u32               *m16c_fb1;
static unsigned           m16c_active;
static u32                m16c_frame_id;
static unsigned           m16c_video_up;
static unsigned           m16c_pad_up;
static unsigned           m16c_audio_up;

/* ---- THE PAYLOAD LEDGER ---------------------------------------------------
 *
 * ***** THE EVIDENCE THAT THE PAYLOAD DID NOT RESTART. ***** m16c_frame_id above
 * is the strongest single proof -- it is monotonic across the whole process, so
 * a log whose frame ids keep climbing through three games cannot have come from
 * three separate launches. These counters are the readable summary of the same
 * fact and they are printed at payload exit. */
static unsigned           m16c_sessions_started;
static unsigned           m16c_sessions_clean;
static unsigned           m16c_sessions_failed;
static unsigned           m16c_commits_done;
static int                m16c_last_session_status;
static u64                m16c_arena_baseline;   /* after video + pad         */
static u64                m16c_mark_first;       /* session 1's arena mark    */
static int                m16c_mark_first_taken;

/* ---- THE STALE MAP-TAIL CLEAR --------------------------------------------
 *
 * ***** THE M16-2 CORRECTION. HARDWARE ROOT CAUSE:
 *       ERROR -633 / STEP 453 / M10MAP 0x00000080 SLOT 6670 STR 256 RES 64/64
 *
 * 0x80 is M10MAP_FAULTSLOT: a slot that no resident page owns was non-NULL.
 * Slot 6670 is page 526 -- above the 16 MB boundary, so an 8 MB cartridge
 * (stride 256, max reachable slot 6655) CANNOT own it. It was left behind by
 * the previous 32 MB cartridge, whose buffers memory_term() had already freed.
 * M10MAP_OOB was CLEAR, which is the signature of arena_rewind() handing the
 * new session the same addresses, so the dangling pointers still land inside
 * allocated buffers and read as plausible entries.
 *
 * THIS COUNTER IS EVIDENCE, NOT BOOKKEEPING. Sessions 1 and 2 of the control
 * sequence must report 0; session 3 must report non-zero. It is also rendered
 * on the STEP-453 screen as the TL field, so that if -633 ever fires again the
 * operator can tell "the clear never ran" (TL 0) apart from "the clear ran and
 * something re-polluted the band" (TL non-zero). */
static unsigned           m16c_maptail_cleared;  /* stale slots cleared, this session */

/* ***** THE PRIMITIVE. IT IS DELIBERATELY THE DUMBEST THING THAT WORKS. *****
 *
 * It allocates nothing, calls no gpSP function, opens no file, reads no buffer
 * state, and touches no cartridge-map slot outside 6656..7167. It has no error
 * path and cannot fail. It is idempotent: on a first session it clears 0 slots
 * and is a no-op.
 *
 * IT IS VERIFIED, NOT TRUSTED. m10map_probe() is completely unchanged and its
 * gate stays fail-closed, so if this clear is ever wrong or insufficient the
 * -633 still fires. Nothing here masks a bit or special-cases a probe. */
static unsigned m16c_clear_map_tail(void)
{
    unsigned s;
    unsigned n = 0u;

    for (s = M16C_MAPTAIL_LO; s <= M16C_MAPTAIL_HI; s++) {
        if (memory_map_read[s] != 0) {
            memory_map_read[s] = 0;
            n++;
        }
    }
    return n;
}

/* ---- THE PER-SESSION RECORD, AND THE RESET PROTECTION ---------------------
 *
 * ***** THIS EXISTS TO MAKE ONE SPECIFIC BUG IMPOSSIBLE. *****
 *
 * end_reason is the single most dangerous piece of session state in a looping
 * launcher. It is pre-loaded with WATCHDOG so that falling out of the frame loop
 * naturally is recorded as WATCHDOG rather than as a completed session -- and it
 * is ALSO the term that makes `clean` true. A stale M16C_END_OPERATOR surviving
 * from a previous session into a session that ended badly would let an UNCLEAN
 * run commit. That is silent data loss, and it is the one failure mode this
 * milestone could plausibly introduce.
 *
 * TWO INDEPENDENT PROTECTIONS, BOTH IMPLEMENTED:
 *
 *   1. NATURAL BLOCK SCOPE. Every session-scoped variable in _start() is
 *      declared INSIDE the for(;;) body, so C re-initialises it on each
 *      iteration. There is no function-scope end_reason to go stale.
 *
 *   2. THIS RECORD, RESET AND THEN VERIFIED. m16c_session_reset() zeroes it and
 *      sets end_reason := WATCHDOG; m16c_session_is_fresh() checks that it
 *      really took; and the local end_reason is SEEDED FROM THE VERIFIED RECORD
 *      rather than from a literal, so the check and the value cannot drift
 *      apart. A failure is M16C_SESSION_DIRTY and is RUNTIME-fatal. */
struct m16c_sess {
    unsigned end_reason;
    unsigned clean;
    unsigned modified;
    unsigned commit_state;      /* 0 NOT NEEDED, 1 UPDATED, 2 FAILED          */
    unsigned restore_state;     /* 0 NONE, 1 LOADED, 2 REJECTED               */
    unsigned frames_emulated;
    unsigned frames_presented;
    unsigned elapsed_ms;
    unsigned romhash;
    unsigned commit_bytes;
    unsigned restored_bytes;
    char     save_id[GBA_SAVE_ID_MAX];
    char     code[8];
};

static struct m16c_sess m16c_rec;

static void m16c_session_reset(void)
{
    unsigned char *p = (unsigned char *)&m16c_rec;
    unsigned       n = (unsigned)sizeof m16c_rec;
    unsigned       k;

    for (k = 0; k < n; k++) p[k] = 0u;

    /* THE ONE NON-ZERO FIELD, AND IT IS NON-ZERO ON PURPOSE. */
    m16c_rec.end_reason = M16C_END_WATCHDOG;
}

static int m16c_session_is_fresh(void)
{
    return m16c_rec.end_reason      == M16C_END_WATCHDOG &&
           m16c_rec.clean           == 0u &&
           m16c_rec.modified        == 0u &&
           m16c_rec.commit_state    == 0u &&
           m16c_rec.restore_state   == 0u &&
           m16c_rec.frames_emulated == 0u &&
           m16c_rec.frames_presented== 0u &&
           m16c_rec.elapsed_ms      == 0u &&
           m16c_rec.romhash         == 0u &&
           m16c_rec.commit_bytes    == 0u &&
           m16c_rec.restored_bytes  == 0u &&
           m16c_rec.save_id[0]      == '\0' &&
           m16c_rec.code[0]         == '\0';
}

/* ================================================================ helpers == */

/* A bounded line builder. 64 bytes covers the 60 columns a 480-pixel row can
   hold, with slack. It ALWAYS NUL-terminates and it DROPS rather than
   overruns. M13C's, verbatim. */
struct m16c_line { char b[64]; unsigned n; };

static void ln_reset(struct m16c_line *l) { l->n = 0u; l->b[0] = '\0'; }

static void ln_putc(struct m16c_line *l, char c)
{
    if (l->n + 1u >= (unsigned)sizeof(l->b)) return;
    l->b[l->n++] = c;
    l->b[l->n]   = '\0';
}

static void ln_puts(struct m16c_line *l, const char *s)
{
    if (!s) return;
    while (*s) ln_putc(l, *s++);
}

static void ln_udec(struct m16c_line *l, unsigned v)
{
    char t[12];
    unsigned n = 0;
    if (v == 0u) { ln_putc(l, '0'); return; }
    while (v && n < (unsigned)sizeof(t)) { t[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n) ln_putc(l, t[--n]);
}

static void ln_dec(struct m16c_line *l, int v)
{
    if (v < 0) { ln_putc(l, '-'); ln_udec(l, (unsigned)(-v)); }
    else       ln_udec(l, (unsigned)v);
}

/* ***** M17 TIER-1B: 64-BIT DECIMAL, BECAUSE THE AUDIO FIGURES ARE u64 AND
   ln_udec() IS 32-BIT. ***** produced/consumed count OUTPUT FRAMES at
   GBA_AUDIO_DST_HZ, so a 32-bit column wraps after 24.8 hours. The
   240-minute watchdog puts that out of reach within ONE session -- but
   plat_audio_submitted() is PAYLOAD-lifetime and accumulates across every
   game the operator runs, so the screen must not silently depend on a bound
   the payload does not actually enforce. A truncated diagnostic that reads
   plausibly is exactly the failure class this milestone exists to detect,
   so the display does the arithmetic honestly instead.

   20 digits covers the full u64 range, and the builder still DROPS rather
   than overruns, so the 64-byte line cannot be exceeded either way. */
static void ln_u64dec(struct m16c_line *l, u64 v)
{
    char t[20];
    unsigned n = 0;

    if (v == 0u) { ln_putc(l, '0'); return; }
    while (v && n < (unsigned)sizeof(t)) {
        t[n++] = (char)('0' + (unsigned)(v % 10u));
        v /= 10u;
    }
    while (n) ln_putc(l, t[--n]);
}

/* Signed 64-bit, for the submission-side drift, and it ALWAYS carries an
   explicit sign so a screenshot can never be read as unsigned. The magnitude
   is formed on the UNSIGNED side: negating the most negative s64 is
   undefined behaviour, which is the same reason the Tier-1 printf prints its
   sign as a separate character rather than using a signed conversion. */
static void ln_s64dec(struct m16c_line *l, s64 v)
{
    if (v < 0) { ln_putc(l, '-'); ln_u64dec(l, (u64)(-(v + 1)) + 1u); }
    else       { ln_putc(l, '+'); ln_u64dec(l, (u64)v); }
}

/* Used by exactly one screen -- the commit-side SRAM capture, which is itself
   only a diagnostic and which must stay readable with NO UDP AT ALL
   (runtime/shim.c:21-22 returns early unless a socket resolved). THE SUCCESS
   PATH CARRIES NO HASH, exactly as in M13C. */
static void ln_hex32(struct m16c_line *l, unsigned v)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;

    ln_puts(l, "0x");
    for (i = 28; i >= 0; i -= 4)
        ln_putc(l, digits[(v >> i) & 0xFu]);
}

/* ***** THE SIZE IS ROUNDED ONLY WHEN THE ROUNDING IS EXACT. ***** A TRUNCATED
   upload is 7,340,032 bytes and printing that as "7 MB" would hide the one fact
   that explains why the game will not boot. */
static void ln_size(struct m16c_line *l, unsigned bytes)
{
    if (bytes == 0u)                        { ln_puts(l, "NOT MEASURED"); return; }
    if ((bytes % (1024u * 1024u)) == 0u)    { ln_udec(l, bytes / (1024u * 1024u));
                                              ln_puts(l, " MB");   return; }
    if ((bytes % 1024u) == 0u)              { ln_udec(l, bytes / 1024u);
                                              ln_puts(l, " KB");   return; }
    ln_udec(l, bytes);
    ln_puts(l, " BYTES");
}

/* ***** THE CAPTURE. REFUSAL, NOT TRUNCATION. ***** Returns 1 only when the
   COMPLETE string was copied. On refusal it writes "" and copies nothing, so a
   caller that ignores the return value gets an empty path -- which every layer
   below rejects -- rather than a shortened one that would open a NEIGHBOUR of
   the file the operator chose. */
static int m16c_capture(char *dst, unsigned cap, const char *src)
{
    unsigned n = 0u, i;

    if (!dst || cap == 0u) return 0;
    dst[0] = '\0';
    if (!src) return 0;

    while (src[n] != '\0') n++;
    if (n + 1u > cap) return 0;

    for (i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = '\0';
    return 1;
}

/* Bitmask reporting: a SET BIT IS A FAILED ASSERTION, so success is a single
   `== 0` test. ***** THIS GOES TO THE LOG ONLY. ***** */
static unsigned report(const char *label, unsigned mask,
                       const char *(*bitname)(unsigned))
{
    unsigned i;

    if (mask == 0u) {
        printf("M16C: %s PASS\n", label);
        return 0u;
    }
    printf("M16C: %s FAIL mask=0x%08X\n", label, mask);
    for (i = 0; i < 32u; i++) {
        if (mask & (1u << i))
            printf("M16C:   bit %u -- %s\n", i, bitname ? bitname(i) : "(?)");
    }
    return mask;
}

static const char *srcbit(unsigned b)
{
    switch (b) {
    case 0:  return "EMPTY -- 0 bytes or unmeasurable";
    case 1:  return "TOO BIG -- over 32 MB";
    case 2:  return "EXACTLY 1 MB -- the 4 MB mirror path";
    default: return "(unnamed source bit)";
    }
}

static const char *prebit(unsigned b)
{
    switch (b) {
    case 0:  return "NO BUFFERS -- gamepak_buffer_count == 0 (FATAL)";
    case 1:  return "gamepak_buffers[0] is NULL";
    case 2:  return "gamepak_size is already set before any load";
    case 3:  return "the gamepak window is already mapped";
    default: return "(unnamed pre bit)";
    }
}

static const char *metabit(unsigned b)
{
    switch (b) {
    case 0:  return "gamepak_size != round_up(file, 32K)";
    case 1:  return "gamepak_file_blocks wrong";
    case 2:  return "gamepak_mirror_1m set";
    case 3:  return "gamepak_mini_materialized set";
    case 4:  return "gamepak_buffer_count < 1";
    case 5:  return "gamepak_buffers[0] is NULL";
    case 6:  return "gamepak_header_nonstandard set";
    case 7:  return "page 0 not recorded in the LRU queue";
    default: return "(unnamed meta bit)";
    }
}

static const char *contentbit(unsigned b)
{
    switch (b) {
    case 0:  return "the resident page is entirely zero";
    case 1:  return "byte[3] != 0xEA";
    case 2:  return "byte[0xB2] != 0x96";
    case 3:  return "the short-read tail is not 0xFF";
    default: return "(unnamed content bit)";
    }
}

static const char *mapbit(unsigned b)
{
    switch (b) {
    case 0:  return "0x08000000 != gamepak_buffers[0]";
    case 1:  return "0x0A000000 mirror missing";
    case 2:  return "0x0C000000 mirror missing";
    case 3:  return "some entry in 0x08..0x0C differs";
    case 4:  return "0x0D000000 is NOT NULL -- EEPROM window overmapped";
    case 5:  return "memory_map_read[0] != bios_rom -- BIOS window lost";
    default: return "(unnamed map bit)";
    }
}

static const char *biospre_bit(unsigned b)
{
    switch (b) {
    case 0:  return "bios_rom was NOT all zero before loading";
    default: return "(unnamed bios pre bit)";
    }
}

static const char *bioscontent_bit(unsigned b)
{
    switch (b) {
    case 0:  return "bios_rom still entirely zero -- nothing was read";
    case 1:  return "bios_rom entirely 0xFF -- blank or erased";
    case 2:  return "bios_rom[0] != 0x18";
    default: return "(unnamed bios content bit)";
    }
}

static const char *biosmap_bit(unsigned b)
{
    switch (b) {
    case 0:  return "memory_map_read[0] != bios_rom";
    case 1:  return "some entry of the 16 MB BIOS window differs";
    case 2:  return "the gamepak window is NOT NULL -- a ROM is already mapped";
    default: return "(unnamed bios map bit)";
    }
}

static const char *m2pre_bit(unsigned b)
{
    switch (b) {
    case 0:  return "gba_screen_pixels is not NULL";
    case 1:  return "noise_table15 is not zero";
    case 2:  return "noise_table7 is not zero";
    default: return "(unnamed m2 pre bit)";
    }
}

/* ***** THE RELAUNCH INVARIANT BITS. ***** Every one names a specific piece of
   state gba_session_end() is responsible for, and EVERY ONE IS FATAL. bit 2 is
   the data-loss one: a surviving latch means the NEXT cartridge would commit
   into THIS one's save file. gba_session.h:158-166. */
static const char *relaunch_bit(unsigned b)
{
    switch (b) {
    case 0:  return "gamepak_buffer_count != 0 -- the buffers were not released";
    case 1:  return "an RFILE pool slot is STILL IN USE -- the ROM file is open";
    case 2:  return "***** THE SAVE IDENTITY IS STILL LATCHED -- DATA LOSS RISK";
    case 3:  return "the restore module is still ARMED";
    case 4:  return "REG_P1 is not the neutral 0x3FF";
    case 5:  return "an SRAM observer slot is still taken";
    case 6:  return "gamepak_mini_materialized survived";
    case 7:  return "a restore baseline hash survived";
    case 8:  return "a restore applied-byte count survived";
    default: return "(unnamed relaunch bit)";
    }
}

/* ---- the save vocabulary, in the operator's words --------------------------
 * ***** UNKNOWN AND PROVISIONAL ARE PRINTED AS THEMSELVES. ***** gba_save.h:
 * 94-98: BACKUP_UNKN collapses to BACKUP_SRAM on the first backup-region access,
 * so a save-less cartridge and an unexercised one are INDISTINGUISHABLE before
 * the game runs. Substituting a plausible guess would tell the operator
 * something this image does not know. */
static const char *m16c_class_text(unsigned c)
{
    switch (c) {
    case GBA_SAVE_SRAM:      return "SRAM";
    case GBA_SAVE_FLASH64:   return "FLASH 64K";
    case GBA_SAVE_FLASH128:  return "FLASH 128K";
    case GBA_SAVE_EEPROM512: return "EEPROM 512B";
    case GBA_SAVE_EEPROM8K:  return "EEPROM 8K";
    default:                 return "UNKNOWN";
    }
}

static const char *m16c_conf_text(unsigned c)
{
    switch (c) {
    case GBA_SAVE_CONF_PROVISIONAL: return "PROVISIONAL";
    case GBA_SAVE_CONF_FINAL:       return "FINAL";
    default:                        return "UNKNOWN";
    }
}

/* EVERY CODE IN gba_savefile.h:106-118 IS COVERED, and the default names the
   number instead of inventing a reason for it. */
static const char *m16c_commit_text(int rc)
{
    switch (rc) {
    case GBA_SAVEFILE_OK:        return "OK -- WRITTEN";
    case GBA_SAVEFILE_ENOTREADY: return "ENOTREADY -- NO SAVEDATA LAYER";
    case GBA_SAVEFILE_ENOID:     return "ENOID -- NO SAVE IDENTITY";
    case GBA_SAVEFILE_EUNKNOWN:  return "EUNKNOWN -- CLASS UNKNOWN";
    case GBA_SAVEFILE_ENOTDIRTY: return "ENOTDIRTY -- WRITER SAW NO CHANGE";
    case GBA_SAVEFILE_EWINDOW:   return "EWINDOW -- MOUNT REFUSED";
    case GBA_SAVEFILE_EMKDIR:    return "EMKDIR -- DIRECTORY FAILED";
    case GBA_SAVEFILE_EOPEN:     return "EOPEN -- COULD NOT CREATE .SAV";
    case GBA_SAVEFILE_EWRITE:    return "EWRITE -- SHORT WRITE";
    case GBA_SAVEFILE_EHDR:      return "EHDR -- HEADER NOT WRITTEN";
    case GBA_SAVEFILE_ECOMMIT:   return "ECOMMIT -- UNMOUNT INCOMPLETE";
    case GBA_SAVEFILE_ERESTORE:  return "ERESTORE -- MOUNT DID NOT RETURN";
    default:                     return "UNRECOGNISED CODE";
    }
}

/* ======================================================= THE M16C PALETTE ===
 *
 * ***** THE LAUNCHER'S PALETTE, COPIED FROM M13C RATHER THAN RE-TUNED. *****
 * runtime/gfx.h's eight COL_* names are the DMG green wash compiled into eight
 * hardware-passed images, so that header IS NOT TOUCHED. M13C declared its own
 * indigo scheme under its own prefix and measured every contrast ratio against
 * the background; M16-2 is the SAME launcher with a loop in it, so it uses the
 * SAME colours under its own prefix rather than inventing a ninth scheme.
 *
 * WCAG 2.1 relative luminance against M16C_C_BG (#0D0F18, L = 0.0047):
 *
 *     M16C_C_TEXT    #F4F5FF   L 0.906    17.2:1   body text
 *     M16C_C_SEL     #9B8CFF   L 0.330     6.9:1   selection
 *     M16C_C_INFO    #59C7FF   L 0.502    10.1:1   measured metadata
 *     M16C_C_OK      #63E6A5   L 0.620    12.2:1   confirmed good
 *     M16C_C_WARN    #FFD166   L 0.680    13.3:1   attention / unknown
 *     M16C_C_ERR     #FF6B7A   L 0.332     7.0:1   genuine failure
 *     M16C_C_MUTED   #8A8FB0   L 0.282     6.1:1   labels and legends
 *     M16C_C_INDIGO  #6657C8   L 0.138     3.4:1   RULES ONLY -- never text
 *
 * ***** INDIGO IS THE ONE COLOUR THAT IS NOT TEXT-LEGAL, AND IT IS NEVER USED
 * FOR TEXT. ***** Every draw_hline() takes it; no draw_str() does. */
#define M16C_C_BG       0xFF0D0F18u
#define M16C_C_TEXT     0xFFF4F5FFu
#define M16C_C_INDIGO   0xFF6657C8u
#define M16C_C_SEL      0xFF9B8CFFu
#define M16C_C_INFO     0xFF59C7FFu
#define M16C_C_OK       0xFF63E6A5u
#define M16C_C_WARN     0xFFFFD166u
#define M16C_C_ERR      0xFFFF6B7Au
#define M16C_C_MUTED    0xFF8A8FB0u

/* ============================================================== presentation */

static void m16c_flip(void)
{
    u32 *fb = (m16c_active & 1u) ? m16c_fb1 : m16c_fb0;

    if (fb) blit_ui(fb, m16c_surface);
    (void)plat_video_present((int)(m16c_active & 1u), m16c_frame_id++);
    m16c_active ^= 1u;
}

/* ---- THE BRIGHTNESS SCALE, APPLIED TO THE FINISHED SURFACE -----------------
 *
 * ***** THE COMPLETED SURFACE IS THE FADE BOUNDARY. ***** k is a 0..256
 * numerator: 0 is black, 256 is the drawing exactly as it was composed. This
 * runs AFTER a screen has finished drawing and BEFORE it is flipped, so it
 * dims whatever is on the surface without any knowledge of what drew it.
 *
 * ***** THAT IGNORANCE IS THE WHOLE POINT, AND IT IS WHY THIS IS NOT A PALETTE
 * SCALE. ***** For today's four-colour text splash, scaling the four palette
 * constants at the draw call would be arithmetically identical and ~32,000x
 * cheaper. It was rejected on purpose. A palette scale only dims elements whose
 * colour the scaler was told about, so every element added later has to be
 * threaded through it by hand, and an element that is missed does not fail to
 * build -- it silently refuses to fade, and nobody finds out until it is on a
 * television. Scaling the finished surface makes it STRUCTURALLY IMPOSSIBLE to
 * miss an element: anything that reached the surface is dimmed, including
 * artwork that does not exist yet and that will need no fade code of its own.
 *
 * COST. 480*270 = 129,600 pixels. blit_ui() already WRITES 1920*1080 =
 * 2,073,600 pixels on this same frame, so the pass is 6.25% of work the flip
 * is doing anyway, and only 34 of the 144 splash frames do it at all (k >= 256
 * early-outs every hold frame and both ramp endpoints). It is also nowhere near
 * a game: gameplay presents via gba_present_blit() straight into the hardware
 * buffer and never touches m16c_surface, so this function cannot reach it.
 *
 * NO STATE. k arrives by value from a loop counter; nothing here is static,
 * nothing is allocated, no table is built and the palette macros are read-only
 * compile-time constants that this function cannot write. When the loops end
 * there is therefore nothing to reset -- the next screen's ui_fill() rewrites
 * all 129,600 pixels regardless, so no dimmed pixel can outlive its own frame.
 *
 * EXACTNESS. At k = 0 every channel is (c * 0) >> 8 = 0, so the pixel becomes
 * 0xFF000000 -- opaque black, exactly, for ANY input colour, with no rounding
 * term. Alpha is REBUILT as 0xFF rather than scaled: this surface is scanned
 * out directly as ARGB8888 (runtime/platform.c:273), so a scaled alpha would
 * fade the picture toward transparent instead of toward black. The widest
 * intermediate is 255 * 256 = 65,280, which is nowhere near a u32 overflow. */
static void m16c_fade_surface(unsigned k)
{
    unsigned i;

    /* Full brightness is the common case -- every hold frame lands here. */
    if (k >= 256u) return;

    for (i = 0; i < (unsigned)(UI_W * UI_H); i++) {
        u32 c = m16c_surface[i];
        u32 r = (((c >> 16) & 0xFFu) * k) >> 8;
        u32 g = (((c >>  8) & 0xFFu) * k) >> 8;
        u32 b = (( c        & 0xFFu) * k) >> 8;

        m16c_surface[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* ***** m16c_masthead() NO LONGER LIVES HERE. ***** It now spells the brand
 * with the EXACT wordmark, so it has to be defined AFTER m16c_wordmark and
 * after m16c_draw_glyphs(); it sits immediately past m16c_draw_splash(). The
 * earliest call site is the failure screen, far below both, so the move needs
 * no forward declaration -- and specifically no tentative declaration of a
 * static const table, which is what a forward declaration here would have
 * cost. */

/* ======================================================= R3 splash artwork ==
 *
 * Everything from here to m16c_draw_splash() exists to draw ONE screen, once,
 * before the picker. It is APP-LOCAL on purpose: runtime/gfx.c is shared with
 * three hardware-passed images and a frozen M13C hash, so a decorative splash
 * does not get to add a primitive there. Every shape below is composed from
 * the primitives that already exist -- fill_rect(), draw_hline(), draw_str().
 *
 * ***** THE ONLY STORED ARTWORK IS THE DOG. ***** Border, stars, gradient,
 * grid, light trails, handheld and the sunset inside its screen are all
 * procedural and cost zero bytes of .rodata. The portrait is 7,424 bytes, the
 * wordmark 88 and the version stamp 48; nothing else is stored. The reference
 * mockup is NOT embedded and never will be -- it is a review target, not an
 * asset.
 *
 * ***** NOTHING HERE BYPASSES m16c_surface. ***** Every helper writes into the
 * surface and only into the surface, so the already-hardware-proven R2 fade
 * (m16c_fade_surface) scales the finished composition exactly as it scaled the
 * four flat colours it was proven on. There is deliberately no separate fade
 * path for the dog or the wordmark -- that is the entire reason R2 faded a
 * completed surface instead of a palette. */

#define M16C_A_SKY0     0xFF070610u    /* gradient, top of frame              */
#define M16C_A_SKY1     0xFF160E30u    /* gradient, at the horizon            */
#define M16C_A_GND0     0xFF120C24u    /* gradient, just below the horizon    */
#define M16C_A_GND1     0xFF05040Cu    /* gradient, bottom of frame           */
#define M16C_A_BORDER   0xFF8A6CF0u    /* inner frame, bright violet          */
#define M16C_A_BORDLO   0xFF3A2C78u    /* outer frame, dim                    */
#define M16C_A_STAR     0xFFC6CEFFu
#define M16C_A_STARLO   0xFF5A63A8u
#define M16C_A_GRID     0xFF6A4CD8u
#define M16C_A_GRIDLO   0xFF2A1F60u
#define M16C_A_TRAIL0   0xFF9B7CFFu
#define M16C_A_TRAIL1   0xFF4C86FFu
#define M16C_A_TRAIL2   0xFF2E2A70u
#define M16C_A_GLOW     0xFF3A2C88u    /* wordmark drop-shadow                */
#define M16C_A_DEVBODY  0xFF0E1020u
#define M16C_A_DEVEDGE  0xFF9878FFu
#define M16C_A_DEVPAD   0xFF2A2350u
#define M16C_A_SCRN0    0xFF1B0E38u
#define M16C_A_SUN      0xFFE07CE8u
#define M16C_A_SUNLO    0xFF7A3CA8u
#define M16C_A_RIDGE    0xFF3A1E60u

#define M16C_ART_HORIZON   176         /* the one y every ground element uses */
#define M16C_ART_STARS      44
#define M16C_ART_GRIDROWS   11
#define M16C_WM_N           11         /* glyphs in "LUAp0rt GBA"             */
#define M16C_WM_SCALE        3         /* 11 * 8 * 3 = 264 px wide            */
#define M16C_VER_N           6         /* glyphs in "v0.0.1"                  */

/* ---- THE EXACT WORDMARK ----------------------------------------------------
 *
 * ***** THIS IS WHY THE SPLASH CAN SPELL "LUAp0rt GBA" AND draw_str() CANNOT.
 * ***** runtime/gfx.c:28 folds lowercase to uppercase before the font lookup,
 * so ANY string routed through draw_char/draw_str/draw_centered renders
 * "LUAPORT GBA". This table is consumed by index, as raw bitmaps, by
 * m16c_draw_glyphs() below -- there is no string and no draw_char() call on
 * that path, so the fold is not merely avoided, it is UNREACHABLE. The splash
 * cannot spell "LUAPORT" even by accident.
 *
 * ***** THIS TABLE NOW DRAWS THE FUNCTIONAL MASTHEAD TOO. ***** It was the
 * splash's alone when it was written. m16c_masthead() -- defined immediately
 * past m16c_draw_splash(), for exactly the ordering reason -- now renders it at
 * SCALE 1 on every working screen, so the launcher and the splash spell the
 * brand from one table.
 *
 * M16C_TITLE stays exactly "LUAPORT GBA" but is NO LONGER DRAWN ANYWHERE. It is
 * now the INTERNAL identity, kept live by the startup log at step 441, so
 * m16c-verify's required-string grep over rodata.bin is still satisfied and
 * still means something. The visible wordmark and the internal verifier string
 * were always different MECHANISMS; they are now different AUDIENCES as well.
 *
 * Bit 0x80 is the LEFTMOST column and row 0 is the TOP, matching font_data's
 * convention (runtime/gfx.c:32-40). L, U, A, G and B are transcribed from
 * runtime/tables.h so the wordmark matches house letterforms; '0' is
 * tables.h's zero, which carries an interior diagonal (0x46,0x4A,0x52,0x62)
 * and is therefore visibly NOT the letter 'O' (0x42,0x42,0x42,0x42). The
 * lowercase p, r and t are authored here -- the shared font has no lowercase
 * and this splash does not get to add any. 'p' descends into rows 6-7. */
static const u8 m16c_wordmark[M16C_WM_N][8] = {
    { 0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00 },   /* L  */
    { 0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00 },   /* U  */
    { 0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00 },   /* A  */
    { 0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40 },   /* p  -- descender         */
    { 0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00 },   /* 0  -- zero, not letter O */
    { 0x00,0x00,0x5C,0x60,0x40,0x40,0x00,0x00 },   /* r  */
    { 0x00,0x20,0x70,0x20,0x20,0x38,0x00,0x00 },   /* t  */
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },   /*    */
    { 0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00 },   /* G  */
    { 0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00 },   /* B  */
    { 0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00 }    /* A  */
};

/* ---- THE VERSION STAMP -----------------------------------------------------
 *
 * ***** THIS IS A BITMAP FOR THE SAME REASON THE WORDMARK IS. ***** The shipping
 * version is "v0.0.1" with a LOWERCASE v, and runtime/gfx.c:28 folds lowercase
 * before the font lookup, so draw_str() would render "V0.0.1". Routed through
 * m16c_draw_glyphs() by index, the fold is unreachable and the lowercase v is
 * guaranteed. It is also not a C string literal, so it never reaches .rodata as
 * text and cannot interact with m16c-verify's literal greps either way.
 *
 * '0' is the same zero the wordmark uses -- tables.h's, with the interior
 * diagonal -- so the two stamps agree and neither can be misread as letter O.
 * The period is a 2x2 dot on the baseline rather than a single pixel, which
 * survives the 4x scale-out to 1920x1080 as a dot rather than a speck. */
static const u8 m16c_version[M16C_VER_N][8] = {
    { 0x00,0x00,0x42,0x42,0x24,0x18,0x00,0x00 },   /* v  -- x-height, no fold */
    { 0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00 },   /* 0  -- zero, not letter O */
    { 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00 },   /* .                       */
    { 0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00 },   /* 0                       */
    { 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00 },   /* .                       */
    { 0x10,0x30,0x10,0x10,0x10,0x38,0x00,0x00 }    /* 1                       */
};

/* x is halved so it fits a u8; stars land on even columns, which is invisible
   at a 1-pixel dot. y is stored whole and never exceeds the horizon. */
static const u8 m16c_art_star[M16C_ART_STARS][2] = {
    { 26, 44},{ 48, 28},{ 70, 62},{ 88, 36},{106, 90},{124, 54},{142, 22},
    {158, 78},{ 34,110},{ 60, 96},{ 80,128},{102,142},{118,116},{134,132},
    {150,104},{172,120},{ 18,152},{ 44,164},{ 64, 18},{ 92, 74},{112, 30},
    {128,158},{146,148},{164, 44},{ 30, 80},{ 54,136},{ 74,102},{ 96,110},
    {114, 68},{132, 92},{152,160},{170, 86},{ 22,124},{ 40, 50},{ 58,158},
    { 78, 24},{ 98,168},{116,146},{136, 60},{156,128},{174,152},{182, 66},
    {186,108},{178, 34}
};

/* Offsets BELOW M16C_ART_HORIZON, widening with distance so the floor reads as
   perspective without a per-pixel projection. Stored as offsets because
   176+93 = 269 would not fit a u8 as an absolute row. */
static const u8 m16c_art_gridrow[M16C_ART_GRIDROWS] = {
    2, 5, 9, 14, 20, 28, 38, 50, 64, 80, 93
};

/* Half-widths of the sun inside the handheld's screen, bottom row first. The
   caller scales these 3/2 for the widened screen -- the table is the profile,
   not the size. */
static const u8 m16c_art_sun[7] = { 7, 7, 6, 6, 5, 4, 2 };

/* ***** GENERATED, COMMITTED ARTWORK. ***** apps/m16cgpsp/m16c_splash_art.inc
 * is produced by tools/mkart.py from assets/nbt_dog_portrait.png and is a
 * PREREQUISITE of m16cmain.o in the Makefile, exactly as m13b_picker.inc is --
 * without that edge, regenerating the portrait would leave a stale object
 * behind. `make m16c` never runs mkart.py: it compiles the committed text, so
 * a normal build needs no PNG decoder and no image library. */
#include "m16c_splash_art.inc"

/* t is 0..256. Integer only, and every term is non-negative so the >> 8 is
   well defined without relying on an arithmetic shift of a negative value. */
static u32 m16c_mix(u32 a, u32 b, int t)
{
    int u = 256 - t;
    int r = ((int)((a >> 16) & 0xFFu) * u + (int)((b >> 16) & 0xFFu) * t) >> 8;
    int g = ((int)((a >>  8) & 0xFFu) * u + (int)((b >>  8) & 0xFFu) * t) >> 8;
    int c = ((int)( a        & 0xFFu) * u + (int)( b        & 0xFFu) * t) >> 8;

    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)c;
}

/* Wide bands, not 270 per-row steps. The R2 fade collapses neighbouring values
   hard at low k -- at k=15 both 0x0D and 0x10 floor to 0 -- so a finely
   stepped gradient would band visibly during the ramp while a coarse one
   cannot. Fewer bands is also less code. */
static void m16c_art_backdrop(void)
{
    int i, y0, y1;

    for (i = 0; i < 9; i++) {
        y0 = (i * M16C_ART_HORIZON) / 9;
        y1 = ((i + 1) * M16C_ART_HORIZON) / 9;
        fill_rect(m16c_surface, 0, y0, UI_W, y1 - y0,
                  m16c_mix(M16C_A_SKY0, M16C_A_SKY1, (i * 256) / 8));
    }
    for (i = 0; i < 5; i++) {
        y0 = M16C_ART_HORIZON + (i * (UI_H - M16C_ART_HORIZON)) / 5;
        y1 = M16C_ART_HORIZON + ((i + 1) * (UI_H - M16C_ART_HORIZON)) / 5;
        fill_rect(m16c_surface, 0, y0, UI_W, y1 - y0,
                  m16c_mix(M16C_A_GND0, M16C_A_GND1, (i * 256) / 4));
    }
}

static void m16c_art_stars(void)
{
    int i, x, y;

    for (i = 0; i < M16C_ART_STARS; i++) {
        x = (int)m16c_art_star[i][0] * 2;
        y = (int)m16c_art_star[i][1];

        if ((i % 5) == 0) {
            fill_rect(m16c_surface, x - 1, y, 3, 1, M16C_A_STARLO);
            fill_rect(m16c_surface, x, y - 1, 1, 3, M16C_A_STARLO);
        }
        fill_rect(m16c_surface, x, y, 1, 1,
                  ((i & 1) || (i % 5) == 0) ? M16C_A_STAR : M16C_A_STARLO);
    }
}

/* One sweep of the light trails. A parabola, not a curve table: y is ybot at
   the centre column and ytop at both edges, so 240*240 is the exact divisor
   and the widest intermediate is 70 * 57,600 = 4.03M -- comfortably inside an
   int. Rows above the surface are clipped by fill_rect, not by this loop. */
static void m16c_art_trail(int ytop, int ybot, int thick, u32 color)
{
    int x, dx, y;

    for (x = 0; x < UI_W; x++) {
        dx = x - (UI_W / 2);
        y = ybot - ((ybot - ytop) * dx * dx) / (240 * 240);
        fill_rect(m16c_surface, x, y, 1, thick, color);
    }
}

static void m16c_art_grid(void)
{
    int i, x, y, xb;

    for (i = 0; i < M16C_ART_GRIDROWS; i++) {
        y = M16C_ART_HORIZON + (int)m16c_art_gridrow[i];
        fill_rect(m16c_surface, 0, y, UI_W, 1,
                  m16c_mix(M16C_A_GRIDLO, M16C_A_GRID,
                           (i * 256) / (M16C_ART_GRIDROWS - 1)));
    }

    /* Columns fan from a vanishing point at the centre of the horizon. */
    for (i = -7; i <= 7; i++) {
        xb = (UI_W / 2) + i * 96;
        for (y = M16C_ART_HORIZON + 1; y < UI_H; y++) {
            x = (UI_W / 2) + ((xb - (UI_W / 2)) * (y - M16C_ART_HORIZON))
                             / (UI_H - M16C_ART_HORIZON);
            fill_rect(m16c_surface, x, y, 1, 1, M16C_A_GRIDLO);
        }
    }

    fill_rect(m16c_surface, 8, M16C_ART_HORIZON, UI_W - 16, 1, M16C_A_GRID);
}

/* ***** A GENERIC HANDHELD, AND IT HAS NO ANALOG STICKS. ***** That is
 * guaranteed structurally rather than by review: the runtime exposes no
 * circle, ellipse or arc primitive (runtime/gfx.h:48-71), every shape below is
 * an axis-aligned fill_rect, and a stick is universally read as a round cap.
 * The directional control is two crossed rectangles -- unambiguously a D-pad.
 * Two face buttons, no four-button diamond, no trademark, no logo. */
static void m16c_art_handheld(void)
{
    const int x = 164, y = 166, w = 152, h = 64;
    int i, hw;

    fill_rect(m16c_surface, x +  12, y - 4, 26, 5, M16C_A_DEVBODY);
    fill_rect(m16c_surface, x + 114, y - 4, 26, 5, M16C_A_DEVBODY);
    fill_rect(m16c_surface, x +  12, y - 4, 26, 1, M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x + 114, y - 4, 26, 1, M16C_A_DEVEDGE);

    /* Two overlapping rects knock the corners off, so the body does not read
       as a slab. There is no rounded-rect primitive and this does not add one. */
    fill_rect(m16c_surface, x,     y + 2, w,     h - 4, M16C_A_DEVBODY);
    fill_rect(m16c_surface, x + 2, y,     w - 4, h,     M16C_A_DEVBODY);

    fill_rect(m16c_surface, x + 2,     y,         w - 4, 1,     M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x + 2,     y + h - 1, w - 4, 1,     M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x,         y + 2,     1,     h - 4, M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x + w - 1, y + 2,     1,     h - 4, M16C_A_DEVEDGE);

    /* ***** THE THREE ZONES STILL DO NOT OVERLAP, BY CONSTRUCTION. ***** The
       screen spans x+42..x+110 (206..274), which leaves 164..206 for the D-pad
       and 274..316 for the face buttons -- two equal 42 px wings. Every rect
       here is opaque and the draw order is last-wins, so a screen widened past
       its wing would not look wider, it would simply be overpainted by the
       controls. The wings grew with the body, so the controls grew with it too
       rather than sitting small in a larger shell.

       EVERY COORDINATE BELOW IS RELATIVE TO x/y/w/h. It used to be absolute,
       which meant the body could be moved while the screen, D-pad, buttons and
       grille silently stayed behind. They are now derived, so the device can
       only ever move as one piece. */
    fill_rect(m16c_surface, x + 42, y + 12, 68, 40, M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x + 43, y + 13, 66, 38, M16C_A_SCRN0);

    /* Sunset on the device screen: ridges, sun, horizon, water glimmer.
       Screen interior is 207..272 x 179..216; the horizon sits at y+38 = 204,
       which is 25/38 of the way down it -- the 80x60 device put it at 16/24,
       so the sky-to-water proportion is carried over rather than re-invented. */
    fill_rect(m16c_surface, x + 46, y + 36, 15, 2, M16C_A_RIDGE);
    fill_rect(m16c_surface, x + 49, y + 34,  7, 2, M16C_A_RIDGE);
    fill_rect(m16c_surface, x + 91, y + 36, 15, 2, M16C_A_RIDGE);
    fill_rect(m16c_surface, x + 94, y + 34,  7, 2, M16C_A_RIDGE);
    /* x+76 = 240 is both the screen's centre and the surface's, so the sun is
       symmetric about the interior: the span is cx-hw .. cx+hw-1. Half-widths
       are scaled 3/2 because the screen widened 36 -> 66 while the table stayed
       7 rows tall; the sun would otherwise read as a distant dot. */
    for (i = 0; i < 7; i++) {
        hw = ((int)m16c_art_sun[i] * 3) / 2;
        fill_rect(m16c_surface, x + 76 - hw, y + 37 - i, hw * 2, 1,
                  m16c_mix(M16C_A_SUNLO, M16C_A_SUN, ((6 - i) * 256) / 6));
    }
    fill_rect(m16c_surface, x + 43, y + 38, 66, 1, M16C_A_SUN);
    fill_rect(m16c_surface, x + 63, y + 41, 26, 1, M16C_A_SUNLO);
    fill_rect(m16c_surface, x + 67, y + 44, 18, 1, M16C_A_SUNLO);
    fill_rect(m16c_surface, x + 71, y + 47, 10, 1, M16C_A_SUNLO);

    /* D-pad: a plus made of two crossed rects, inside the 164..206 wing. Both
       arms are centred on (x+21, y+32) = (185, 198). */
    fill_rect(m16c_surface, x + 11, y + 29, 21,  7, M16C_A_DEVPAD);
    fill_rect(m16c_surface, x + 18, y + 22,  7, 21, M16C_A_DEVPAD);
    fill_rect(m16c_surface, x + 20, y + 31,  3,  3, M16C_A_DEVEDGE);

    /* Exactly two face buttons, inside the 274..316 wing. Offset diagonally,
       NOT a four-button diamond and NOT a round cap of any kind. */
    fill_rect(m16c_surface, x + 116, y + 24, 9, 9, M16C_A_DEVPAD);
    fill_rect(m16c_surface, x + 130, y + 34, 9, 9, M16C_A_DEVPAD);
    fill_rect(m16c_surface, x + 117, y + 25, 6, 1, M16C_A_DEVEDGE);
    fill_rect(m16c_surface, x + 131, y + 35, 6, 1, M16C_A_DEVEDGE);

    /* Grille, below both control clusters and clear of the screen. */
    for (i = 0; i < 3; i++) {
        fill_rect(m16c_surface, x +  12, y + 52 + i * 3, 20, 1, M16C_A_DEVPAD);
        fill_rect(m16c_surface, x + 120, y + 52 + i * 3, 20, 1, M16C_A_DEVPAD);
    }
    fill_rect(m16c_surface, x + 144, y + 4, 2, 2, M16C_C_OK);
}

/* Consumes a glyph table BY INDEX. No string, no draw_char, so the lowercase
   fold cannot reach this path -- see the note on the tables above. ONE loop
   serves both the wordmark and the version stamp: both exist only because
   runtime/gfx.c cannot produce the letterforms they need, and a second copy of
   this loop would be the same code twice for no gain. */
static void m16c_draw_glyphs(const u8 (*g)[8], int n, int x, int y, int s,
                             u32 color)
{
    int i, r, c;
    u8 bits;

    for (i = 0; i < n; i++) {
        for (r = 0; r < 8; r++) {
            bits = g[i][r];
            if (!bits) continue;
            for (c = 0; c < 8; c++) {
                if (bits & (0x80u >> c))
                    fill_rect(m16c_surface, x + (i * 8 + c) * s, y + r * s,
                              s, s, color);
            }
        }
    }
}

/* ---- THE TWO BUTTON SYMBOLS ------------------------------------------------
 *
 * ***** THESE ARE SHAPES, NOT CHARACTERS. ***** runtime/gfx.c's font covers
 * ASCII 32..90 and nothing else (runtime/gfx.c:22-29), so SQUARE and TRIANGLE
 * cannot be spelled -- there is no codepoint to spell them with and adding one
 * would mean a second font. They are drawn instead, out of the SAME fill_rect()
 * this file already uses ~60 times for the splash art, so neither helper adds a
 * primitive, an asset, a table or an undefined symbol.
 *
 * ***** THE 6x6 INK BOX IS THE FONT'S, MEASURED, NOT GUESSED. ***** Every
 * letterform in runtime/tables.h occupies columns 1..6 and rows 0..5 of its 8x8
 * cell: 'O' is 3C 42 42 42 42 3C and 'X' is 42 24 18 18 24 42, where 0x42
 * lights columns 1 and 6 and 0x3C lights 2..5. Both helpers therefore take the
 * INK-BOX origin -- the cell origin plus one -- and fill exactly 6x6. A symbol
 * drawn this way has the same width, the same stroke and the same baseline as
 * the X and the O on the row above it, so the two rows align by construction
 * rather than by a fudge factor that a later font change would silently break.
 *
 * fill_rect clips every edge itself (runtime/gfx.c:70-78), so neither helper
 * needs a bounds test. */
static void m16c_sym_square(int x, int y, u32 color)
{
    fill_rect(m16c_surface, x,     y,     6, 1, color);   /* top    */
    fill_rect(m16c_surface, x,     y + 5, 6, 1, color);   /* bottom */
    fill_rect(m16c_surface, x,     y + 1, 1, 4, color);   /* left   */
    fill_rect(m16c_surface, x + 5, y + 1, 1, 4, color);   /* right  */
}

/* The two sloped edges step one column every two rows -- 2,2,1,1,0 out to the
   left and 3,3,4,4,5 out to the right -- which is the same stepped diagonal the
   font itself uses for '/' (02 04 08 10 20 40). Row 5 is the base. */
static void m16c_sym_triangle(int x, int y, u32 color)
{
    int r;

    for (r = 0; r < 5; r++) {
        fill_rect(m16c_surface, x + 2 - (r >> 1), y + r, 1, 1, color);
        fill_rect(m16c_surface, x + 3 + (r >> 1), y + r, 1, 1, color);
    }
    fill_rect(m16c_surface, x, y + 5, 6, 1, color);
}

/* ---- THE PORTRAIT ----------------------------------------------------------
 *
 * ***** THE FEATHER IS PRECOMPUTED; THIS DOES NO FEATHER MATH. ***** Alpha is
 * read out of the generated palette and used as a blend weight, nothing more.
 * tools/mkart.py bakes a FRAME-ANCHORED UPPER-RIGHT BLEND into those entries at
 * build time. There is NO radius, NO ellipse and NO luminance key anywhere in
 * that tool -- alpha is a pure function of (x, y) and never of colour, which is
 * why the dog's near-black eyes and nose cannot be hollowed out.
 *
 * ***** ALL FOUR EDGES DISSOLVE, AND THE 4:1 ASYMMETRY IS THE DESIGN. *****
 * Alpha is the product of four directional dissolves -- two broad, two tiny:
 *
 *     LEFT   12 px  ATMOSPHERIC -- irregular dissolve into the backdrop
 *     BOTTOM 12 px  ATMOSPHERIC -- irregular dissolve into the backdrop
 *     TOP     3 px  EDGE-BREAKING -- roughens the boundary under the y=6 rule
 *     RIGHT   3 px  EDGE-BREAKING -- roughens the boundary behind the x=473 rule
 *
 * Left and bottom carry the photograph out into open background. Top and right
 * do NOT fade the portrait -- 3 px cannot fade anything -- they exist solely so
 * the bright fur stops terminating in a dead-straight line, over rows the
 * frame's own rules then overpaint. This is NOT a symmetric four-sided feather;
 * tools/mkart.py refuses to build one.
 *
 * FIVE earlier masks were built and rejected. A radial vignette and a SYMMETRIC
 * four-edge feather both read as a circular avatar (the roundness came from the
 * symmetry, not the arithmetic). An asymmetric version with SMOOTH 3 px
 * top/right bands, floating clear of the frame, fixed the circle but read as a
 * rectangular photo card, because a smooth fade ending in open background only
 * softens the rectangle -- a mask cannot dissolve an edge into a background that
 * edge does not touch. Placing the portrait flush against the frame's violet
 * rules fixed THAT, and the result still failed twice more: once as a photo card
 * with a grey drop shadow, because the alpha field was SEPARABLE (its iso-alpha
 * contours were straight lines) and because cream fur composited onto navy
 * passes through NEUTRAL GREY; and once because the top and right edges were
 * still anchored at 0 px, leaving straight fur edges that the 1 px rules were
 * too thin to hide. The frozen mask answers all three -- a 2D value-noise wobble
 * makes the field non-separable, a violet haze gives the transition a colour of
 * its own, and the 3 px bands break the last two straight edges. The generated
 * parameters and their proofs live in m16c_splash_art.inc.
 *
 * The outermost row and column on ALL FOUR sides are exactly alpha 0 regardless
 * of image content, so they map to palette entry 0 and are SKIPPED here -- those
 * surface pixels are never written, which is why the gradient and the stars show
 * through the dissolve instead of being covered by a rectangle of
 * pre-composited background.
 *
 * The blend is (dst*(256-a) + src*a) >> 8 rather than dst + ((src-dst)*a >> 8)
 * so that every term stays non-negative and the shift never depends on an
 * arithmetic shift of a negative int. a is widened 0..255 -> 0..256 by
 * a + (a >> 7), which maps 0 to 0 and 255 to 256 exactly, so a fully opaque
 * palette entry reproduces the source colour bit for bit.
 *
 * The result is written back with alpha forced to 0xFF: this surface is
 * scanned out as ARGB8888 and m16c_fade_surface() assumes an opaque surface,
 * so the portrait must not leave translucency behind it. */
static void m16c_draw_dog(int x0, int y0)
{
    int x, y, px, py, ia, na;
    u32 src, d;

    for (y = 0; y < M16C_DOG_H; y++) {
        py = y0 + y;
        if (py < 0 || py >= UI_H) continue;

        for (x = 0; x < M16C_DOG_W; x++) {
            px = x0 + x;
            if (px < 0 || px >= UI_W) continue;

            src = m16c_dog_pal[m16c_dog_pix[y * M16C_DOG_W + x]];
            ia = (int)(src >> 24);

            if (ia == 0) continue;                 /* clear: leave background */

            if (ia >= 255) {
                m16c_surface[py * UI_W + px] = 0xFF000000u | (src & 0x00FFFFFFu);
                continue;
            }

            ia += (ia >> 7);                       /* 0..255 -> 0..256        */
            na = 256 - ia;
            d = m16c_surface[py * UI_W + px];

            m16c_surface[py * UI_W + px] = 0xFF000000u
                | ((u32)((((int)((d >> 16) & 0xFFu)) * na
                        + ((int)((src >> 16) & 0xFFu)) * ia) >> 8) << 16)
                | ((u32)((((int)((d >>  8) & 0xFFu)) * na
                        + ((int)((src >>  8) & 0xFFu)) * ia) >> 8) <<  8)
                |  (u32)((((int)( d        & 0xFFu)) * na
                        + ((int)( src       & 0xFFu)) * ia) >> 8);
        }
    }
}

/* ---- THE STARTUP IDENTITY SCREEN ------------------------------------------
 *
 * ***** DRAW-ONLY, AND THE ONLY SCREEN IN THIS FILE THAT DOES NOT USE
 * m16c_masthead(). ***** Every other screen is a WORKING screen, so its title
 * belongs on the title bar at y=4 with the content beneath it. This one has no
 * content: it is the payload saying what it is, once, so the identity is
 * centred in the panel instead of pinned to the top.
 *
 * DRAW ORDER IS BACK TO FRONT and each layer is allowed to cover the last:
 * backdrop, stars, grid, trails, handheld, text, wordmark, portrait, frame.
 * The trails sit in FRONT of the grid, as in the approved artwork. The frame is
 * last so the grid cannot run over it; the portrait is late so it composites
 * over a finished background rather than over a half-built one.
 *
 * ***** THE CENTRED BRANDING IS CENTRED ON THE WHOLE 480 px SURFACE AND THE
 * DOG CONTRIBUTES NOTHING TO IT. ***** The wordmark spans 11 glyphs * 8 px * 3
 * = 264 px, so its origin is (480-264)/2 = 108 -- a constant derived from UI_W
 * and the glyph count alone. M16C_SUBTITLE is 34 characters at a fixed 8x8
 * (runtime/gfx.c:44-50) = 272 px, so draw_centered() places it at 104 and the
 * rule is drawn to that exact width. The portrait is pinned to a fixed
 * right-hand origin and appears in NO centring expression, so it cannot shift
 * either line.
 *
 * LAYOUT, PROVEN DISJOINT (all logical px, x4 on screen):
 *
 *     author block     16..200   16..103     widest line is 23 ch = 184 px
 *     portrait        393..472    7..86      x gap 193 to the author block,
 *                                            y gap 26 above the wordmark
 *     wordmark        108..372  112..136     y gap 9 below the author block
 *     rule            104..376  142
 *     subtitle        104..376  148..156
 *     handheld        164..316  162..230    screen 206..274, D-pad 164..206,
 *                                           two face buttons 274..316; rows
 *                                           162..165 are the shoulder caps,
 *                                           the body proper is 166..230
 *     slogan dashes    80..104  249         and 376..400, mirrored about 240
 *     slogan          112..368  246..254
 *     v0.0.1          412..460  246..254     x gap 44 to the slogan, of which
 *                                            the right dash takes 376..400,
 *                                            leaving 12 px clear; ink runs
 *                                            413..458, clear of the x=473 rule
 *     inner frame       6..473    6..263     everything is inside it
 *
 * ***** THE PORTRAIT IS DELIBERATELY FLUSH AGAINST TWO FRAME RULES. ***** Rows
 * 7..86 sit directly beneath the top rule at y=6 and columns 393..472 directly
 * left of the right rule at x=473. That is not a coincidence of layout, it is
 * the mask's precondition: the top and right edges carry only a 3 px
 * edge-breaking dissolve -- enough to destroy the straight boundary, nowhere
 * near enough to fade the portrait out. Those two edges work ONLY because the
 * frame's rules are drawn AFTERWARDS directly over the rows they give up,
 * capping the fade and supplying the violet it fades into. Composite these
 * bytes anywhere else and the same two edges become the rectangular photo-card
 * edge that was already rejected on review. The one consequence of the
 * placement is that the frame's top-right corner block (469,6,5,5) repaints
 * 16 px of fur at x 469..472, y 7..10 -- extreme corner, ~45 px from the
 * nearest eye, and the approved reference shows a corner bracket there too.
 * tools/mkart.py reports that exact count on every regeneration so it stays
 * measured rather than discovered on hardware.
 *
 * ***** THE VERSION IS A BITMAP, NOT A STRING. ***** "v0.0.1" needs a lowercase
 * v, and runtime/gfx.c:28 folds lowercase before the font lookup, so draw_str()
 * would render "V0.0.1". It goes through m16c_draw_glyphs() for exactly the
 * reason the wordmark does. It replaced a neutral "DEV BUILD" label, which was
 * in no required-string list; M16C_TITLE and M16C_SUBTITLE still carry no
 * version, for the reason stated at their definitions.
 *
 * Indigo is used for rules and NOWHERE for text, which is the palette rule
 * stated at the colour table: M16C_C_INDIGO is the one colour in this file
 * that is not contrast-legal as text. */
static void m16c_draw_splash(void)
{
    ui_fill(m16c_surface, M16C_C_BG);

    m16c_art_backdrop();
    m16c_art_stars();

    m16c_art_grid();

    m16c_art_trail(163, 224, 2, M16C_A_TRAIL2);
    m16c_art_trail(170, 216, 1, M16C_A_TRAIL1);
    m16c_art_trail(178, 208, 1, M16C_A_TRAIL0);

    m16c_art_handheld();

    draw_str(m16c_surface, 16,  16, "BY NBT",                  M16C_C_SEL);
    draw_str(m16c_surface, 16,  30, "HOMEBREW",                M16C_C_MUTED);
    draw_str(m16c_surface, 16,  39, "EMULATION",               M16C_C_MUTED);
    draw_str(m16c_surface, 16,  48, "PS5 ENTHUSIAST",          M16C_C_MUTED);
    draw_str(m16c_surface, 16,  57, "BUILDING RETRO",          M16C_C_MUTED);
    draw_str(m16c_surface, 16,  66, "FOR A BRIGHTER TOMORROW", M16C_C_MUTED);
    draw_hline(m16c_surface, 78, 16, 64, M16C_C_INDIGO);
    draw_str(m16c_surface, 16,  86, "GAMES BRING",             M16C_C_MUTED);
    draw_str(m16c_surface, 16,  95, "PEOPLE TOGETHER",         M16C_C_MUTED);

    /* Drawn twice: a one-pixel offset shadow then the face. That is the whole
       "neon glow" -- a real bloom is a per-pixel blur and a second buffer. */
    m16c_draw_glyphs(m16c_wordmark, M16C_WM_N, 109, 113,
                     M16C_WM_SCALE, M16C_A_GLOW);
    m16c_draw_glyphs(m16c_wordmark, M16C_WM_N, 108, 112,
                     M16C_WM_SCALE, M16C_C_SEL);

    draw_hline(m16c_surface, 142, 104, UI_W - 104, M16C_C_INDIGO);
    draw_centered(m16c_surface, 148, M16C_SUBTITLE, M16C_C_TEXT);

    /* Flanking rules for the slogan. draw_hline takes x1,x2 -- NOT x,width
       (runtime/gfx.c:58-63) -- and x2 is exclusive, so these ink 80..103 and
       376..399: an 8 px gap to the 32-character slogan at 112..367, mirrored
       about x=240, and still 12 px clear of the version stamp at 412. Indigo,
       because these are rules and indigo is this file's rule colour. */
    draw_hline(m16c_surface, 249,  80, 104, M16C_C_INDIGO);
    draw_hline(m16c_surface, 249, 376, 400, M16C_C_INDIGO);
    draw_centered(m16c_surface, 246, "PLAY THE PAST. POWER THE FUTURE.",
                  M16C_C_SEL);
    m16c_draw_glyphs(m16c_version, M16C_VER_N, 412, 246, 1, M16C_C_MUTED);

    /* x: UI_W-7 is the inner frame's own right rule, so the portrait's right
       column lands at 472, flush against it. y: 7 is the row directly beneath
       the top rule at y=6. Both 3 px edge-breaking bands therefore dissolve
       ONTO a frame rule rather than into open background, and the frame below
       is drawn afterwards over the rows they give up -- which is the only
       reason those bands are legitimate here. See m16c_splash_art.inc. */
    m16c_draw_dog(UI_W - 7 - M16C_DOG_W, 7);

    /* Frame last, so nothing drawn above can run over it. */
    fill_rect(m16c_surface,   2,   2, UI_W - 4, 1,        M16C_A_BORDLO);
    fill_rect(m16c_surface,   2, UI_H - 3, UI_W - 4, 1,   M16C_A_BORDLO);
    fill_rect(m16c_surface,   2,   2, 1,        UI_H - 4, M16C_A_BORDLO);
    fill_rect(m16c_surface, UI_W - 3, 2, 1,     UI_H - 4, M16C_A_BORDLO);

    fill_rect(m16c_surface,   6,   6, UI_W - 12, 1,        M16C_A_BORDER);
    fill_rect(m16c_surface,   6, UI_H - 7, UI_W - 12, 1,   M16C_A_BORDER);
    fill_rect(m16c_surface,   6,   6, 1,         UI_H - 12, M16C_A_BORDER);
    fill_rect(m16c_surface, UI_W - 7, 6, 1,      UI_H - 12, M16C_A_BORDER);

    fill_rect(m16c_surface,   6,   6, 5, 5, M16C_A_BORDER);
    fill_rect(m16c_surface, UI_W - 11,   6, 5, 5, M16C_A_BORDER);
    fill_rect(m16c_surface,   6, UI_H - 11, 5, 5, M16C_A_BORDER);
    fill_rect(m16c_surface, UI_W - 11, UI_H - 11, 5, 5, M16C_A_BORDER);
}

/* ---- THE FUNCTIONAL MASTHEAD ----------------------------------------------
 *
 * ***** THE BRAND IS "LUAp0rt", AND THIS IS WHERE THE LAUNCHER SPELLS IT.
 * ***** This was draw_centered(..., M16C_TITLE, ...), and runtime/gfx.c:28
 * folds lowercase to uppercase before the font lookup, so that path could only
 * ever render "LUAPORT GBA" -- not through an oversight, but because no string
 * can survive the fold. Routing the title through m16c_draw_glyphs() -- BY
 * INDEX, no string, no draw_char() -- makes the fold UNREACHABLE, exactly as it
 * already is for the splash wordmark and the version stamp.
 *
 * ***** THIS ADDS NO ASSET. ***** It consumes the SAME m16c_wordmark the
 * splash consumes. No second table, no new font system, no bitmap duplicated,
 * nothing allocated and no new persistent state.
 *
 * GEOMETRY IS UNCHANGED, AND DELIBERATELY SO. M16C_TITLE is 11 characters at
 * 8 px = 88 px, so draw_centered() placed it at (UI_W - 88) / 2 = 196. The
 * wordmark is 11 glyphs at 8 px at scale 1 -- the SAME 88 px -- so the same
 * expression yields the SAME 196, derived from UI_W and the glyph count alone
 * exactly as the splash derives its own origin. The title bar occupies the
 * pixels it always has; four letterforms changed (P O R T -> p 0 r t) and
 * nothing else. L, U, A, G and B are transcribed from runtime/tables.h, so
 * they are byte-identical to what draw_str() was already drawing.
 *
 * THE ONE REAL DIFFERENCE IS THE DESCENDER. Uppercase glyphs ink rows 0..5, so
 * the old title ended at y = 4 + 5 = 9. The lowercase 'p' descends into rows
 * 6..7, so the new title ends at y = 4 + 7 = 11. Every screen that rules
 * beneath the masthead rules at y=16 -- the picker at 26 -- so the tightest
 * clearance goes from 6 px to 4 px, and no screen is tighter than that.
 *
 * ***** M16C_TITLE IS STILL LIVE, AND STILL EXACTLY "LUAPORT GBA". ***** It is
 * now an INTERNAL identity rather than a player-visible one: logged once at
 * startup (step 441) and never drawn. m16c-verify greps rodata.bin for that
 * literal (Makefile:16303); the startup log keeps it in the image HONESTLY --
 * a real, reachable diagnostic, not a blob parked there to satisfy a grep. */
static void m16c_masthead(void)
{
    ui_fill(m16c_surface, M16C_C_BG);
    m16c_draw_glyphs(m16c_wordmark, M16C_WM_N,
                     (UI_W - M16C_WM_N * 8) / 2, 4, 1, M16C_C_SEL);
}

/* ---- RUNTIME-WIDE TEARDOWN, ONCE, IN THE PROVEN ORDER ----------------------
 *
 * ***** THIS IS NOT CALLED WHEN A GAME ENDS. ***** That is the single most
 * important sentence in this file. A game ending calls gba_session_end(), which
 * releases the SESSION and deliberately leaves video, pad and the audio PORT
 * alive (gba_session.c:156-158) because the picker needs all three. This
 * function is reached from exactly two places: the payload's normal exit and
 * m16c_runtime_fatal(). m16c-verify asserts that mechanically.
 *
 * AUDIO GOES DOWN FIRST, AND UNLIKE THE OTHER TWO IT REALLY DOES CLOSE.
 * plat_pad_shutdown() deliberately does NOT call scePadClose and
 * plat_video_shutdown() deliberately does NOT release the direct memory
 * reservation (runtime/platform.c:328-340, :517-533). sceAudioOutClose is the
 * documented counterpart to sceAudioOutOpen and a port left open across a
 * relaunch is a leaked hardware resource.
 *
 * ***** IT IS IDEMPOTENT BY FLAG. ***** */
static void m16c_teardown(void)
{
    if (m16c_audio_up) {
        plat_audio_shutdown();
        m16c_audio_up = 0u;
        klog("LUAport M16C: audio port closed\n");
    }
    if (m16c_pad_up) {
        plat_pad_shutdown();
        m16c_pad_up = 0u;
    }
    if (m16c_video_up) {
        plat_video_shutdown();
        m16c_video_up = 0u;
        klog("LUAport M16C: pad and display released -- the host game should be "
             "visible again. RELAUNCH IT BEFORE THE NEXT SESSION\n");
    }
}

/* ---- THE FAILURE SCREEN ----------------------------------------------------
 *
 * ***** DETAILED, DELIBERATELY. ***** The polish this launcher asks for is that
 * a SUCCESSFUL session shows no diagnostics. A failure is the one moment the
 * operator needs them. `d1`/`d2`/`d3` may be NULL; a row is omitted rather than
 * drawn empty, because an empty labelled row reads as a MISSING measurement.
 *
 * `tail` is the one addition over M13C's version: it is the line that tells the
 * operator WHAT HAPPENS NEXT, and in a looping launcher that is no longer a
 * constant. "( O ) EXIT" is true of a runtime-fatal screen and false of a
 * session-fatal one, where CIRCLE merely dismisses the screen and the picker
 * comes back. Getting that wrong would teach the operator that the launcher had
 * crashed when it had in fact recovered. */
static void m16c_fail_screen(const char *head, int status, unsigned step,
                             const char *d1, const char *d2, const char *d3,
                             const char *tail)
{
    struct plat_pad_state pad;
    struct m16c_line l;
    unsigned f;
    u32 prev = 0u;

    for (f = 0; f < M16C_FAIL_FRAMES; f++) {
        u32 pressed;

        (void)plat_pad_read(&pad);
        pressed = pad.buttons & ~prev;
        prev    = pad.buttons;
        if (pressed & PAD_CIRCLE) break;

        m16c_masthead();
        draw_hline(m16c_surface, 16, 4, UI_W - 4, M16C_C_INDIGO);

        /* ***** THE HEADING IS THE ONLY RED THING ON THIS SCREEN. ***** */
        draw_str(m16c_surface, 4, 30, head ? head : M16C_H_NOTREADY,
                 M16C_C_ERR);

        draw_hline(m16c_surface, 44, 4, UI_W - 4, M16C_C_INDIGO);

        /* ***** POLISH-1: THE SENTENCES NOW HOLD THE POSITION THE CODES USED
           TO. ***** The intent was always the one the old comment stated -- the
           codes are for whoever reads the log, the sentences are for the person
           in the room -- but the LAYOUT said the opposite: "ERROR -617" and
           "STEP 452" occupied rows two and three, the most prominent space on
           the screen, and the English was pushed below a rule. An operator read
           a number first and a sentence second. That order is now inverted.
           Nothing about WHICH strings exist has changed; only where they sit.

           The rows are spaced 12 px rather than 10 for the same reason a
           paragraph is not set solid: three full-width sentences at 8 px glyph
           height read as a block at 10 and as lines at 12. */
        if (d1) draw_str(m16c_surface, 4,  56, d1, M16C_C_TEXT);
        if (d2) draw_str(m16c_surface, 4,  68, d2, M16C_C_TEXT);
        if (d3) draw_str(m16c_surface, 4,  80, d3, M16C_C_TEXT);

        /* ***** THE CODES ARE DEMOTED. THEY ARE NOT REMOVED, AND THEY MUST NOT
           BE. *****
           runtime/shim.c returns early when no UDP socket resolved, so on a
           console with no listener THIS SCREEN IS THE ONLY EVIDENCE CHANNEL
           THERE IS -- a photograph of it is how every failure in this project
           has ever been diagnosed. Deleting the codes to make the screen
           prettier would trade the entire diagnostic path for two rows of
           whitespace.

           m16c-verify asserts the literals "ERROR " and "STEP " are present in
           the image's rodata (the user-facing-string gate). They are emitted
           below as the SAME two ln_puts() literals, trailing space included, so
           that assertion still protects exactly what it protected before: not
           the layout, but the fact that a failed launcher can still name its
           status and its step. The gate was neither weakened nor rewritten to
           accommodate this change. */
        draw_hline(m16c_surface, UI_H - 34, 4, UI_W - 4, M16C_C_INDIGO);

        ln_reset(&l);
        ln_puts(&l, "ERROR ");
        ln_dec(&l, status);
        ln_puts(&l, "   ");
        ln_puts(&l, "STEP ");
        ln_udec(&l, step);
        draw_str(m16c_surface, 4, UI_H - 26, l.b, M16C_C_MUTED);

        draw_str(m16c_surface, 4, UI_H - 14,
                 tail ? tail : "( O ) EXIT", M16C_C_MUTED);

        m16c_flip();
    }
}

/* ---- RUNTIME-FATAL -------------------------------------------------------
 *
 * ***** THIS IS M13C's m13c_abort(), UNCHANGED IN BEHAVIOUR. ***** Show the
 * screen if there is a screen to show, release every subsystem that came up, set
 * the status. The caller returns from _start() immediately afterwards.
 *
 * ***** IT IS NOT THE DEFAULT. ***** M13C had nine abort sites and every one of
 * them ended the payload, because in M13C there was nothing else to end. Here
 * each of those nine has been classified individually (see the status block
 * above) and only the payload-scoped ones reach this function. A wholesale
 * conversion in either direction would be a correctness regression. */
static void m16c_runtime_fatal(struct ext_args *ext, int status, const char *head,
                               const char *d1, const char *d2, const char *d3)
{
    printf("M16C: ***** RUNTIME-FATAL: %s ***** status %d, step %u\n",
           head ? head : "FAILURE", status, (unsigned)ext->step);
    klog("LUAport M16C: ***** LUAPORT CANNOT SAFELY CONTINUE. ***** This is not "
         "a game ending -- it is the runtime ending. The picker will NOT come "
         "back\n");

    if (m16c_video_up && m16c_pad_up)
        m16c_fail_screen(head, status, (unsigned)ext->step, d1, d2, d3,
                         "( O ) EXIT");

    m16c_teardown();
    ext->status = status;
}

/* ---- THE SESSION BOUNDARY ------------------------------------------------
 *
 * ***** THE MOST IMPORTANT FUNCTION IN THIS FILE, AND IT ADDS NO MECHANISM.
 * *****
 *
 * It runs on EVERY path out of a game session -- clean, unclean, and every
 * session-fatal failure -- because a cartridge that failed to load is exactly as
 * capable of leaving a mapping behind as one that played for an hour. M16-1's
 * finding is the reason this is not negotiable: gpSP leaves the PREVIOUS
 * mapping intact when a load fails, so a session-fatal return that skipped
 * memory_term() would leave the last cartridge's bytes mapped under the next
 * cartridge's identity.
 *
 * ORDER, AND WHY EACH STEP IS WHERE IT IS:
 *
 *   1. gba_session_end()   the proven teardown, VERBATIM. Step 4 of it is
 *                          gba_save_unlatch(), which is why the commit must
 *                          already have happened. Safe when no cartridge was
 *                          ever loaded -- every step is individually idempotent
 *                          (gba_session.h:110-112), which is what lets a failure
 *                          path unwind through the same code as a clean ending.
 *   2. arena_rewind(mark)  AFTER memory_term(), because memory_term() is what
 *                          stops gpSP referencing the buffers the rewind
 *                          reclaims. Rewinding first would hand those bytes out
 *                          while they were still live.
 *   3. the watermark       measured, not assumed.
 *   4. the probe           nine bits, every one FATAL.
 *
 * ***** IT FAILS CLOSED AND IT FAILS RUNTIME-FATAL. ***** Returns 1 only when
 * the boundary is provably clean. On 0 the status is already set and the runtime
 * is already down; the caller returns from _start(). */
static int m16c_boundary(struct ext_args *ext, u64 mark, int mark_taken,
                         unsigned rfile_before)
{
    unsigned m;
    u64      after_reset;
    unsigned buffers_after, rfile_after, latched_after, armed_after;
    unsigned reg_p1_after, relaunch;

    ext->step = 458;
    klog("LUAport M16C: --- SESSION BOUNDARY ---\n");

    /* ---- 1: THE PROVEN TEARDOWN, VERBATIM -------------------------------- */
    gba_session_end();

    /* ---- 2: THE REWIND, STRICTLY AFTER IT -------------------------------- */
    if (mark_taken) {
        if (!arena_rewind(mark)) {
            printf("M16C: ***** arena_rewind(%u) REFUSED *****\n",
                   (unsigned)mark);
            ext->dbg[4] = mark;
            m16c_runtime_fatal(ext, M16C_ARENA_REWIND_BAD, M16C_H_BOUNDARY,
                               "THE MEMORY USED BY THE GAME COULD NOT BE",
                               "RECLAIMED. LUAPORT STOPPED RATHER THAN START",
                               "ANOTHER GAME ON TOP OF IT.");
            return 0;
        }
    } else {
        klog("LUAport M16C: no arena mark was taken this session, so there is "
             "nothing to rewind. The teardown still ran in full\n");
    }

    /* ---- 3: THE MEASUREMENTS --------------------------------------------- */
    after_reset   = arena_used();
    buffers_after = m4_rom_read(M4_RD_BUFFER_COUNT);
    rfile_after   = gba_rfile_inuse();
    latched_after = gba_save_latched();
    armed_after   = gba_restore_read(GBA_RESTORE_RD_ARMED);
    reg_p1_after  = m8_input_read_reg_p1();
    relaunch      = gba_session_probe_relaunch();

    printf("M16C:   arena   mark %u, after reset %u (baseline %u)\n",
           (unsigned)mark, (unsigned)after_reset,
           (unsigned)m16c_arena_baseline);
    printf("M16C:   buffers %u (expect 0)\n", buffers_after);
    printf("M16C:   rfile   %u -> %u (expect back to %u)\n",
           rfile_before, rfile_after, rfile_before);
    printf("M16C:   latched %u (expect 0), armed %u (expect 0)\n",
           latched_after, armed_after);
    printf("M16C:   REG_P1  0x%03X (expect 0x%03X)\n",
           reg_p1_after, (unsigned)GBA_SESS_P1_NEUTRAL);

    /* ***** CLAIM 1: THE ARENA RETURNS TO THE SAME WATERMARK. ***** */
    if (mark_taken && after_reset != mark) {
        printf("M16C: ***** THE ARENA DID NOT RETURN: %u != mark %u *****\n",
               (unsigned)after_reset, (unsigned)mark);
        ext->dbg[4] = after_reset;
        m16c_runtime_fatal(ext, M16C_ARENA_LEAK, M16C_H_BOUNDARY,
                           "MEMORY WAS NOT FULLY RECLAIMED WHEN THE GAME",
                           "ENDED. LUAPORT STOPPED RATHER THAN LEAK IT INTO",
                           "THE NEXT GAME.");
        return 0;
    }

    /* ***** CLAIM 2: THE RFILE POOL RETURNS TO ITS OWN BASELINE. *****
       Measured against THIS session's baseline rather than against zero, because
       a demand-paged cartridge holds a handle during play and a fully resident
       one does not. The pool has FOUR slots, so a leak of one per launch becomes
       fatal on the fifth game -- and would look like a corrupt ROM. */
    if (rfile_after != rfile_before) {
        printf("M16C: ***** RFILE LEAK: %u before, %u after *****\n",
               rfile_before, rfile_after);
        ext->dbg[4] = (u64)rfile_after;
        m16c_runtime_fatal(ext, M16C_BOUNDARY_DIRTY, M16C_H_BOUNDARY,
                           "THE GAME FILE WAS NOT CLOSED WHEN THE SESSION",
                           "ENDED. AFTER A FEW GAMES NOTHING WOULD LOAD, SO",
                           "LUAPORT STOPPED NOW INSTEAD.");
        return 0;
    }

    /* ***** CLAIM 3: EVERY SESSION-OWNED LATCH IS CLEAR. *****
       GBA_SESS_R_LATCHED is the data-loss bit and it gets its own status: a
       surviving identity means the NEXT cartridge would commit into THIS one's
       save file. gba_session.h:183-185. */
    if ((m = report("RELAUNCH", relaunch, relaunch_bit))) {
        klog("LUAport M16C: ***** THE SESSION TEARDOWN LEFT STATE BEHIND. ***** "
             "Starting another cartridge on top of it would carry this "
             "session's state into the next one. REFUSING TO CONTINUE\n");
        ext->dbg[4] = (u64)m;

        if (m & GBA_SESS_R_LATCHED) {
            klog("LUAport M16C: ***** THE SAVE IDENTITY IS STILL LATCHED. ***** "
                 "This is the DATA-LOSS condition: the next cartridge would "
                 "commit its memory into THIS cartridge's save file\n");
            m16c_runtime_fatal(ext, M16C_IDENTITY_STALE, M16C_H_BOUNDARY,
                               "THE SAVE IDENTITY DID NOT CLEAR WHEN THE GAME",
                               "ENDED. THE NEXT GAME COULD HAVE OVERWRITTEN",
                               "THIS GAME'S SAVE, SO LUAPORT STOPPED.");
        } else {
            m16c_runtime_fatal(ext, M16C_BOUNDARY_DIRTY, M16C_H_BOUNDARY,
                               "THE GAME SESSION DID NOT FULLY RELEASE.",
                               "LUAPORT STOPPED RATHER THAN START ANOTHER",
                               "GAME ON TOP OF IT.");
        }
        return 0;
    }

    klog("LUAport M16C: session boundary CLEAN -- arena at its watermark, "
         "buffers released, ROM file closed, identity unlatched, restore "
         "disarmed, input neutral, observers reset. The picker is coming "
         "back\n");
    return 1;
}

/* ---- SESSION-FATAL -------------------------------------------------------
 *
 * ***** ABANDON THIS CARTRIDGE, KEEP LUAPORT. ***** The screen is shown, the
 * complete boundary runs, and the caller returns to the picker.
 *
 * ***** IT DELIBERATELY DOES NOT SET ext->status. ***** ext->status describes
 * the PAYLOAD, and the payload has not failed -- it is about to show the picker
 * again. The failing session's code is recorded in m16c_last_session_status,
 * counted in m16c_sessions_failed, logged in full and drawn on screen. Writing
 * it into ext->status would make a later clean exit look like a failure, or
 * worse, make a failure look clean once the operator exited normally.
 *
 * Returns 1 when the boundary was clean and the caller may continue to the
 * picker; 0 when the boundary was contaminated, in which case the status IS set,
 * the runtime IS down, and the caller must return from _start(). */
static int m16c_session_fatal(struct ext_args *ext, int status, const char *head,
                              const char *d1, const char *d2, const char *d3,
                              u64 mark, int mark_taken, unsigned rfile_before)
{
    unsigned step = (unsigned)ext->step;

    printf("M16C: ***** SESSION-FATAL: %s ***** status %d, step %u\n",
           head ? head : "FAILURE", status, step);
    klog("LUAport M16C: this GAME could not continue. LUAport itself is fine -- "
         "the session is being torn down safely and the picker will come "
         "back\n");

    m16c_sessions_failed++;
    m16c_last_session_status = status;
    ext->dbg[4] = (u64)(u32)(s32)status;

    if (m16c_video_up && m16c_pad_up)
        m16c_fail_screen(head, status, step, d1, d2, d3,
                         "( O ) RETURNS TO PICKER");

    return m16c_boundary(ext, mark, mark_taken, rfile_before);
}

/* ============================================================== the picker == */

/* Row geometry. 8x8 glyphs, so a 480-pixel row holds 60 columns.
 *
 *   x=4     cursor gutter  (">" on the selected row)
 *   x=16    the filename label, up to M13B_LABEL_MAX = 44 glyphs
 *   x=372   the unavailability reason, right of the label
 *
 * ***** 44 IS m13b_picker.inc's NUMBER AND IS NOT RE-CHOSEN HERE. ***** The
 * label column runs from x=16 to the reason column at x=372, which is
 * (372-16)/8 = 44.5 -> 44 whole glyphs.
 *
 * draw_char() clips PER PIXEL against UI_W/UI_H (runtime/gfx.c:36-38). */
#define ROW_Y0       32
#define ROW_DY        9
#define COL_CURSOR    4
#define COL_NAME     16
#define COL_REASON  372

/* M17 TIER-1B. The second column of the AUDIO DIAG block on the SESSION
   COMPLETE screen. UI_W is 480 and the font is 8 px wide (runtime/gfx.h), so
   column 4 gives the left block 30 characters before this one starts and this
   one 29 before the right edge -- enough for an 11-character label and a full
   20-digit u64 without either column touching the other. */
#define COL_DIAG2   244

#define PANEL_X      96
#define PANEL_Y0    199
#define PANEL_DY      9

/* ***** THE DETAILS CONTROL LEGEND NEEDS ITS OWN VALUE COLUMN AND CANNOT
   REUSE PANEL_X. ***** The legend's left column now reads "( X ) / ( O )",
   which is 13 glyphs at the fixed 8 px advance (runtime/gfx.c:47) = 104 px
   from COL_CURSOR, ending at x=107. PANEL_X is 96, so the string would run
   twelve pixels INTO the value column and overdraw it. The value column moves
   to 152 for the three GAMEPLAY rows only; the ROM info panel above keeps
   PANEL_X unchanged, which is why this is a separate name and not an edit to
   PANEL_X. 152 leaves a 44 px gap after the legend and still clears the next
   column at 248 by 56 px with "D-PAD" in place. */
#define M16C_LEG_VALX 152

/* ***** SIXTEEN ROWS IS m13b_picker.inc's M13B_ROWS AND IS NOT RESTATED. ***** */

/* ---- THE DISPLAY NAME (POLISH-2) -------------------------------------------
 *
 * ***** THE RULE THIS FUNCTION IS WRITTEN TO OBEY IS m13b_picker.inc:44 *****
 *
 *         A LABEL MAY BE SHORTENED. A PATH MAY NOT.
 *
 * Every entry in the library is, by gba_lib_classify()'s definition, a file
 * whose name ends in ".gba" case-insensitively. So the extension is the one
 * part of every row that carries ZERO information: it is four identical glyphs
 * repeated down all sixteen rows, and in a 44-glyph column it is four glyphs of
 * the actual name that the operator does not get to see. Removing it is worth
 * doing precisely because it costs nothing -- there is no case in which its
 * absence is ambiguous, because a non-.gba file is not in the library at all.
 *
 * ***** THIS FUNCTION CANNOT REACH A FILENAME OR A PATH, BY CONSTRUCTION. *****
 * Three independent reasons, not one:
 *
 *   1. `src` is `const char *`. It is read and never written.
 *   2. The shortened form is built in `tmp`, an AUTOMATIC buffer owned by this
 *      call, which ceases to exist when the function returns.
 *   3. `dst` is the caller's DISPLAY buffer -- the same `lab[]` that
 *      m13b_label() has always written into, whose only consumer is draw_str().
 *
 * m16c_chosen_name, gba_lib_path(), gba_lib_name(), m16c_capture(),
 * m4_rom_load_named(), gba_save_name_sav() and every .sav/.hdr identity are
 * therefore untouched and unreachable from here. The bytes handed to gpSP and
 * the bytes used to name a save file still come straight out of the library
 * entry, exactly as before. NOTHING IS LOGGED FROM A DISPLAY BUFFER either: the
 * printf at "CHOSEN NAME" prints m16c_chosen_name itself, and it still does.
 *
 * ***** IT DELEGATES THE SHORTENING RATHER THAN REPEATING IT. ***** The "..."
 * cut, the cap arithmetic and the NUL guarantee are m13b_label()'s, called here
 * with the same dst/cap the caller would have passed anyway. That file is
 * FROZEN and proven by tools/m13b_picker_equiv.c's 283 offline checks; this
 * function is a pre-pass in front of it, not a second implementation of it, so
 * none of those checks are invalidated and m13b_picker.inc is not touched.
 *
 * ***** WHY THE STRIP HAPPENS BEFORE THE LABEL AND NOT AFTER. ***** Doing it
 * afterwards would be simpler but would waste the four columns on exactly the
 * names that need them most: a 46-character filename would be cut to 44 with a
 * "..." stamp and never reach its own extension. Stripping first gives those
 * four glyphs back to the name.
 *
 * ***** `tmp` IS SIZED SO THAT IT CAN NEVER BE THE THING THAT TRUNCATES. *****
 * It is M13STORE_PATH_MAX, and gba_library.h:52 defines GBA_LIB_PATH_MAX as
 * exactly M13STORE_PATH_MAX -- every name this function can be handed is a
 * substring of a path stored in a buffer of that size, so it is ALWAYS shorter
 * than `tmp`. The clamp below therefore cannot fire on any real input; it is
 * there so that the copy is bounded by the buffer rather than by an argument,
 * which is a property the reader can check locally.
 *
 * That sizing is deliberate and is the reason a smaller buffer was not used.
 * A `tmp` shorter than the longest possible name would silently drop the tail
 * of a long filename BEFORE m13b_label() ever saw it, and m13b_label() would
 * then find a string that already fit and copy it WITHOUT stamping "..." --
 * turning a visible abbreviation into an invisible one. That is precisely the
 * failure mode the label contract exists to prevent, so the 256 bytes of stack
 * are buying the one guarantee this function has to make. It is an automatic
 * buffer in a leaf display helper: it costs no .bss and nothing persists. */
static void m16c_display_name(char *dst, unsigned cap, const char *src)
{
    char     tmp[M13STORE_PATH_MAX];
    unsigned n = 0u, keep, i;

    if (!dst || cap == 0u)
        return;

    /* A NULL source is m13b_label()'s case to handle, and it already does:
       it writes an empty, NUL-terminated string. Do not duplicate that. */
    if (!src) {
        m13b_label(dst, cap, src);
        return;
    }

    while (src[n] != '\0')
        n++;

    keep = n;

    /* ***** STRICTLY GREATER THAN FOUR. ***** A file named exactly ".gba" is a
       legal name and stripping it would leave an EMPTY row -- an entry the
       operator can see in the count but not read. Such a name keeps its four
       characters and displays as ".GBA", which is honest and selectable. */
    if (n > 4u && src[n - 4u] == '.') {
        char a = src[n - 3u], b = src[n - 2u], c = src[n - 1u];

        /* Case folded locally, on COPIES of three characters. tolower() is not
           used: this file is -ffreestanding and does not link <ctype.h>. */
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));

        if (a == 'g' && b == 'b' && c == 'a')
            keep = n - 4u;
    }

    if (keep > (unsigned)sizeof(tmp) - 1u)
        keep = (unsigned)sizeof(tmp) - 1u;

    for (i = 0; i < keep; i++)
        tmp[i] = src[i];
    tmp[keep] = '\0';

    m13b_label(dst, cap, tmp);
}

static void m16c_draw_picker(unsigned sel, unsigned top,
                             const struct m13b_input *in, int gated)
{
    struct m16c_line l;
    char             lab[M13B_LABEL_CAP];
    unsigned         r;
    unsigned         idx = 0u;
    int              have = 0;

    m16c_masthead();

    /* ---- THE SOURCE LINE AND THE COUNT ---------------------------------
     * The location "ROMS /TEMP0" is identity, the count is a measurement, and
     * "N UNAVAILABLE" is the one clause on this row that wants the operator's
     * eye. The font is a fixed 8x8 and a glyph at x spans x..x+7, so advancing
     * by 8 * (glyphs + gap) lands each run in its own cell. str_len() is gfx.h's,
     * so the measurement and the renderer agree by construction. */
    {
        int x;

        ln_reset(&l);
        ln_puts(&l, "ROMS ");
        ln_puts(&l, M16C_ROM_LABEL);
        draw_str(m16c_surface, COL_CURSOR, 16, l.b, M16C_C_SEL);
        x = COL_CURSOR + 8 * (str_len(l.b) + 5);

        ln_reset(&l);
        ln_udec(&l, m16c_lib.count);
        ln_puts(&l, (m16c_lib.count == 1u) ? " ROM" : " ROMS");
        draw_str(m16c_surface, x, 16, l.b, M16C_C_INFO);
        x += 8 * (str_len(l.b) + 3);

        if (m16c_rejected) {
            ln_reset(&l);
            ln_udec(&l, m16c_rejected);
            ln_puts(&l, " UNAVAILABLE");
            draw_str(m16c_surface, x, 16, l.b, M16C_C_WARN);
        }
    }

    draw_hline(m16c_surface, 26, 4, UI_W - 4, M16C_C_INDIGO);

    /* ---- THE SIXTEEN ROWS ----------------------------------------------- */
    for (r = 0; r < M13B_ROWS; r++) {
        unsigned disp = top + r;
        unsigned i;
        u32      col;
        int      y = ROW_Y0 + (int)(r * ROW_DY);

        if (disp >= m16c_lib.count) break;

        /* ***** THE PERMUTATION, NOT A REORDERING. ***** m13b_order maps a
           DISPLAY ROW to a SCAN INDEX; gba_lib_path()/gba_lib_name() are always
           asked for the scan index, so the bytes on screen and the bytes handed
           to gpSP come from the same, unmoved library entry. */
        i = (unsigned)m13b_order[disp];

        if (disp == sel)            { col = M16C_C_SEL; idx = i; have = 1; }
        else if (m16c_items[i].sel)   col = M16C_C_TEXT;
        else                          col = M16C_C_MUTED;

        if (disp == sel)
            draw_str(m16c_surface, COL_CURSOR, y, ">", M16C_C_SEL);

        /* POLISH-2: the extension is dropped FOR THE ROW ONLY. `i` is still the
           scan index and gba_lib_path(&m16c_lib, i) is still what launches. */
        m16c_display_name(lab, (unsigned)sizeof(lab),
                          gba_lib_name(&m16c_lib, i));
        draw_str(m16c_surface, COL_NAME, y, lab, col);

        /* ***** AN UNAVAILABLE ENTRY STAYS VISIBLE AND SAYS WHY. ***** */
        if (!m16c_items[i].sel)
            draw_str(m16c_surface, COL_REASON, y,
                     m13b_reason_text(m16c_items[i].reason), M16C_C_WARN);
    }

    draw_hline(m16c_surface, 178, 4, UI_W - 4, M16C_C_INDIGO);

    /* The scroll indicator. Absent entirely when everything already fits. */
    if (m16c_lib.count > M13B_ROWS) {
        ln_reset(&l);
        ln_udec(&l, sel + 1u);
        ln_puts(&l, " OF ");
        ln_udec(&l, m16c_lib.count);
        if (top > 0u)                          ln_puts(&l, "    UP FOR MORE");
        if (top + M13B_ROWS < m16c_lib.count)  ln_puts(&l, "    DOWN FOR MORE");
        draw_str(m16c_surface, COL_CURSOR, 183, l.b, M16C_C_MUTED);
    }

    draw_hline(m16c_surface, 194, 4, UI_W - 4, M16C_C_INDIGO);

    /* ---- THE INFORMATION PANEL -------------------------------------------
     *
     * ***** WHAT IS KNOWN HERE IS SHOWN. WHAT IS NOT KNOWN HERE SAYS SO. *****
     * TITLE, CODE, SAVE CLASS and SAVE FILE all need a RESIDENT cartridge image,
     * and there is none until load_gamepak() has run. The two forbidden ways to
     * fill them are loading each highlighted cartridge on every D-pad press, and
     * writing a second picker-local header parser that could disagree with the
     * proven one. NEITHER IS DONE HERE, AND POLISH-6 DID NOT RELAX THAT: this
     * panel still performs NO ROM READ, parses NO HEADER, and invents NO
     * metadata. The honesty rule is unchanged; only its PRESENTATION is.
     *
     * ***** WHAT CHANGED AND WHY. ***** The rule was first expressed as four
     * separate rows each reading the identical string "READ AT LAUNCH". That is
     * honest, and it is also what an unfinished panel looks like: four labelled
     * fields and not one value, repeated on every D-pad press. Those four rows
     * became one line of prose, "FULL DETAILS SHOWN AT LAUNCH", and that line
     * has now become a STATUS row -- because prose describing a future screen
     * is still not a reading of this cartridge, and the panel had a real fact
     * about the highlighted entry available the whole time.
     *
     * ***** THE PROSE WAS REPLACED BY SOMETHING THE PANEL ALREADY KNEW. *****
     * No information was added and none was removed at any step: the promise
     * line stated where values would appear, and STATUS states whether this
     * entry can be launched at all -- which is the question the operator is
     * actually asking while standing on it.
     *
     * SIZE is the exception and IS shown, because m4_rom_query_named() opened,
     * measured and closed every entry before this panel was ever drawn. It is
     * therefore promoted to the first row: it is the only measurement here, so
     * it should not be third behind two placeholders.
     *
     * ***** A REJECTED ENTRY STILL SAYS WHY, AND SAYS IT MORE PLAINLY. *****
     * The rejection reason used to be printed against a label reading "CODE",
     * which named the wrong thing -- m13b_reason_text() returns why an entry
     * cannot be launched, not a game code. It now sits under "REASON", where it
     * belongs, with the entry's state on the row above it. That reason is the
     * single most useful string on this screen when something is wrong and it
     * is NOT abbreviated, moved off-screen or dropped. */
    {
        const struct m13b_item *it = have ? &m16c_items[idx] : 0;
        int y = PANEL_Y0;

        draw_str(m16c_surface, COL_CURSOR, y, "SIZE", M16C_C_MUTED);
        ln_reset(&l);
        ln_size(&l, it ? it->size : 0u);
        draw_str(m16c_surface, PANEL_X, y, l.b, M16C_C_INFO);
        y += PANEL_DY;

        if (it && !it->sel) {
            draw_str(m16c_surface, COL_CURSOR, y, "STATUS", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y, "UNAVAILABLE", M16C_C_WARN);
            y += PANEL_DY;

            draw_str(m16c_surface, COL_CURSOR, y, "REASON", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y,
                     m13b_reason_text(it->reason), M16C_C_WARN);
        } else if (it) {
            /* ***** THE ROW IS ONLY DRAWN WHEN AN ENTRY IS ACTUALLY
               HIGHLIGHTED. ***** With no selection there is no cartridge for
               the status to be about, and a launcher reporting READY for
               nothing would be making the panel say MORE than it knows --
               which is the exact failure this whole block is written to avoid.
               The row is simply absent instead.

               ***** READY IS A FACT THIS PANEL ALREADY OWNED. ***** `it->sel`
               IS launchability: the picker gates the launch press on exactly
               this flag, and the branch directly above prints UNAVAILABLE for
               its complement under the very same label. READY is therefore the
               other half of a decision this screen was already displaying, and
               it is produced with NO ROM READ, NO HEADER PARSE, NO CACHE and NO
               NEW STATE -- the honesty rule at the top of this block is intact.

               It takes the success colour because it is the one row here that
               reports a cartridge is good to go; UNAVAILABLE above it is amber
               for the same reason in the opposite direction. */
            draw_str(m16c_surface, COL_CURSOR, y, "STATUS", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y, "READY", M16C_C_OK);
        }
    }

    /* ***** THE SHARED FOOTER GEOMETRY, NOT A PICKER-LOCAL ONE. ***** This rule
       and the prompt below it now sit on the same two anchors the details, the
       failure and the empty-library screens use -- UI_H-34 and UI_H-26 -- so the
       footer does not JUMP VERTICALLY as the operator moves between screens. It
       was 246/252 here and 236/244 on details, a 10 px shift on every
       transition, which reads as the panel twitching rather than as two screens.

       CLEARANCE, RESTATED BECAUSE THE RULE MOVED UP:
         panel rows       SIZE PANEL_Y0 199, STATUS 208, REASON 217 (worst case,
                          a rejected entry -- the deepest the panel ever goes)
         last glyph bottom  217 + 8 = 225   (the font is 8x8)
         this rule          UI_H - 34 = 236 -- 11 px clear of the panel
       It was 21 px clear at 246. 11 px is the tightest gap on this screen and it
       is still wider than the 8 px glyph body, so the descender of a REASON row
       cannot touch the rule. */
    draw_hline(m16c_surface, UI_H - 34, 4, UI_W - 4, M16C_C_INDIGO);

    /* ***** THREE PROMPTS, AND THE THIRD IS NEW TO M16-2. *****
     *
     * `gated` is the release gate described at M16C_RELEASE_SETTLE. It is drawn
     * because a picker that silently ignores input for a fifth of a second reads
     * as a frozen picker, and an operator whose finger really is stuck needs to
     * be told which finger. It is DISPLAY ONLY and does not decide anything.
     *
     * The WAIT prompt has to be visible while the release window is open: the
     * launch fires on the CROSS-UP edge, so a HELD CROSS produces no other
     * on-screen change at all. */
    if (gated)
        draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                 "RELEASE THE BUTTONS", M16C_C_WARN);
    else if (in && in->state == M13B_IN_WAIT)
        draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                 "RELEASE ( X ) TO LAUNCH", M16C_C_SEL);
    else
        draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                 "UP/DOWN MOVE   L1/R1 PAGE   ( X ) LAUNCH   ( O ) EXIT",
                 M16C_C_MUTED);
}

/* ---- THE PICKER RE-ENTRY RESET -------------------------------------------
 *
 * ***** THE ONE GENUINELY NEW PIECE OF INPUT LOGIC IN M16-2, AND IT IS
 * APP-LOCAL. ***** apps/m13bgpsp/m13b_picker.inc IS NOT MODIFIED.
 *
 * THREE THINGS, EACH FIXING A SPECIFIC DEFECT:
 *
 *   1. m13b_input_reset(). M13B_IN_LAUNCH and M13B_IN_TIMEOUT are TERMINAL AND
 *      DELIBERATELY INERT (m13b_picker.inc:424-426): once the machine has
 *      answered, no further frame can make it answer again. That is exactly the
 *      right property for a fixture that launches once, and it means a machine
 *      carried over from the previous entry is PERMANENTLY DEAD -- the operator
 *      could press CROSS forever and nothing would happen. It MUST be reset.
 *
 *   2. THE RELEASE GATE. Input is not consumed until M16C_RELEASE_MASK has been
 *      clear for M16C_RELEASE_SETTLE consecutive frames. The picker is drawn
 *      throughout, so it REAPPEARS INSTANTLY -- which is the operator-visible
 *      pass criterion for the return -- it simply does not act yet.
 *
 *   3. prev_buttons IS PRIMED FROM A LIVE READ, NOT FROM ZERO. This is the fix
 *      that actually matters, and it works even if the gate times out on a stuck
 *      button: `pressed = buttons & ~prev` can only report an edge that really
 *      happened after this instant. M13C's `prev_buttons = 0u` is what would
 *      have turned a held L1+R1 into a -16 and a +16 page jump on the return
 *      frame, and a held CIRCLE into an immediate payload exit.
 *
 * ***** THE CURSOR IS PRESERVED, THEN RE-CLAMPED. ***** Returning the operator
 * to the row they just played is the correct behaviour; m13b_scroll() is TOTAL
 * and clamps every argument combination into [0, count), so a preserved cursor
 * cannot point off the end of a list. */
static void m16c_picker_reentry(struct m13b_input *in, u32 *prev_buttons,
                                unsigned sel, unsigned *top, int first_entry)
{
    struct plat_pad_state pad;
    unsigned clear_run = 0u;
    unsigned f;

    m13b_input_reset(in);
    *top = m13b_scroll(sel, m16c_lib.count, *top);

    /* ***** THE GATE IS SKIPPED ON THE VERY FIRST ENTRY, AND ONLY THERE. *****
       Nothing has been played yet, so nothing can be held from a previous
       session, and making the operator satisfy a release gate before the
       launcher has ever drawn a row would be a delay with no cause. The live
       priming below still happens, so even the first entry is protected against
       a button held from the host game's own menu. */
    if (!first_entry) {
        klog("LUAport M16C: the picker is back. Holding input until the exit "
             "chord is released -- the return chord IS the shoulder buttons, so "
             "accepting them now would page the list or exit the payload\n");

        for (f = 0; f < M16C_RELEASE_MAX_FRAMES; f++) {
            (void)plat_pad_read(&pad);

            if ((pad.buttons & M16C_RELEASE_MASK) == 0u) {
                clear_run++;
                if (clear_run >= M16C_RELEASE_SETTLE) break;
            } else {
                clear_run = 0u;
            }

            m16c_draw_picker(sel, *top, in, 1);
            m16c_flip();
        }

        if (f >= M16C_RELEASE_MAX_FRAMES)
            klog("LUAport M16C: ***** THE RELEASE GATE TIMED OUT. ***** A "
                 "button appears to be stuck. The picker is opening anyway -- "
                 "priming the edge detector from the LIVE pad state means a "
                 "held button still produces no press edge\n");
    }

    /* ***** THE PRIMING. ***** Read the pad ONE more time and adopt whatever is
       held as the "already seen" state. This is the line that makes a held
       button harmless, and it is why the gate above is an operator courtesy
       rather than the actual correctness mechanism. */
    (void)plat_pad_read(&pad);
    *prev_buttons = pad.buttons;

    printf("M16C: picker entry -- prev_buttons primed to 0x%08X from a live "
           "pad read (M13C primed 0 here, which would have reported every held "
           "button as a fresh press)\n", (unsigned)*prev_buttons);
}

/* ---- THE EMPTY LIBRARY ----------------------------------------------------
 * ***** NO CRASH, NO GAMEPLAY SETUP, AND NO BIOS REQUIREMENT. ***** Reached
 * before the ROM buffers are allocated, before init_sound(), before reset_gba()
 * and before a single BIOS candidate is opened. */
static void m16c_draw_empty(void)
{
    m16c_masthead();
    draw_hline(m16c_surface, 16, 4, UI_W - 4, M16C_C_INDIGO);

    /* AMBER, NOT RED. An empty library is not a fault. */
    draw_str(m16c_surface, COL_CURSOR, 40, M16C_H_NOROMS, M16C_C_WARN);

    draw_str(m16c_surface, COL_CURSOR, 64, "TO ADD GAMES:", M16C_C_MUTED);
    draw_str(m16c_surface, COL_CURSOR, 76, "COPY .GBA FILES TO /TEMP0",
             M16C_C_TEXT);

    draw_str(m16c_surface, COL_CURSOR, 100, "LOOKED IN", M16C_C_MUTED);
    draw_str(m16c_surface, PANEL_X,    100, M16C_ROM_LABEL, M16C_C_INFO);

    /* ***** THE ONE DRAW THIS SCREEN WAS MISSING. ***** Every other screen in
       the launcher separates its prompt from its content with a rule; this one
       left "( O ) EXIT" floating at UI_H-20, so the empty library was the only
       place the footer had no edge and sat 4 px lower than everywhere else. The
       rule is the SAME primitive, inset and colour used by the details, failure
       and picker footers -- no new style, no new string, no behaviour.
         content ends      LOOKED IN row 100 + 8 = 108
         this rule         UI_H - 34 = 236 -- 128 px clear, by a wide margin the
                           roomiest screen in the launcher, which is correct for
                           one whose entire message is "there is nothing here" */
    draw_hline(m16c_surface, UI_H - 34, 4, UI_W - 4, M16C_C_INDIGO);

    draw_str(m16c_surface, COL_CURSOR, UI_H - 26, "( O ) EXIT", M16C_C_MUTED);
}

/* ---- THE LOADING ACKNOWLEDGEMENT ------------------------------------------
 * Short and content-free on purpose: it exists so a CROSS press is never silent,
 * not to report anything. */
static void m16c_draw_loading(void)
{
    char lab[60];

    m16c_masthead();
    draw_hline(m16c_surface, 16, 4, UI_W - 4, M16C_C_INDIGO);

    draw_str(m16c_surface, COL_CURSOR, 40, "LOADING...", M16C_C_SEL);

    m16c_display_name(lab, (unsigned)sizeof(lab), m16c_chosen_name);
    draw_str(m16c_surface, COL_CURSOR, 60, lab, M16C_C_TEXT);
}

/* ============================================================== the launcher */

__attribute__((section(".text._start")))
void _start(u64 eboot_base, u64 dlsym_addr, struct ext_args *ext)
{
    void *G = (void *)(eboot_base + GADGET_OFFSET);
    void *D = (void *)dlsym_addr;

    /* =====================================================================
     * PAYLOAD-SCOPED LOCALS. EVERY ONE OF THESE OUTLIVES A GAME.
     * =====================================================================
     * ***** WHAT IS **NOT** HERE IS THE POINT. ***** end_reason, clean,
     * modified, commit_state, restore_state, the frame counters, the identity
     * strings, the arena mark and every probe mask are declared INSIDE the
     * for(;;) body below, so C re-initialises them on every iteration and no
     * value can survive from one game into the next. See the note on
     * struct m16c_sess. */
    unsigned i;
    int      scanned = 0;
    int      rc      = 0;

    /* PICKER-SCOPED: preserved across returns on purpose, so the operator comes
       back to the row they just played. Re-clamped by m16c_picker_reentry(). */
    unsigned sel = 0u, top = 0u;
    u32      prev_buttons = 0u;
    struct m13b_input in;
    struct plat_pad_state pad;

    /* PAYLOAD-SCOPED, AND DECLARED OUT HERE BECAUSE THE CALL AND THE GATE ARE IN
       DIFFERENT SCOPES. plat_savedata_init() is invoked inside the nested block at
       step 444 that owns the kmkdir/loadmod pointers, and its verdict is tested
       AFTER that block has closed. Initialised to PLAT_SD_ENOTREADY so the gate
       REFUSES rather than passes if the call is ever skipped. The layer itself is
       initialised exactly once per payload -- see step 444 -- so this value is
       never recomputed per session. */
    int      sd_init_rc = PLAT_SD_ENOTREADY;

    /* PAYLOAD-SCOPED: the BIOS is ONE 16 KB image for every cartridge and is
       loaded exactly once. That is why M3_PRE_BIOS_ZERO is an EXPECTED bit in
       GBA_SESS_PRE3_RELAUNCH rather than a failure. */
    unsigned bios_idx = 0u, bios_size = 0u;
    int      bios_found = 0;

    /* PAYLOAD-SCOPED: the clock. HOISTED OUT OF THE SESSION, deliberately --
       see step 448. */
    int      time_rc;

    int      first_entry   = 1;
    int      user_exit     = 0;

    /* ---- THE FOUR ONE-TIME SETUP FLAGS ------------------------------------
     *
     * ***** THESE EXIST BECAUSE `first_session ? 0 : MASK` IS WRONG, AND IT IS
     * WRONG IN A WAY THAT FAILS CLOSED ON A GOOD CONSOLE. *****
     *
     * The three relaunch masks in gba_session.h are not one fact, they are FOUR
     * independent survivors, and step 451 installs them at four DIFFERENT points
     * with session-fatal failures in between:
     *
     *   m2_set_screen()        (a), then -> M2_PRE_SCREEN_NULL survives
     *   m4_rom_buffer_init()   (c)  <-- SESSION-FATAL when buffers < 2
     *   m2_call_init_sound()   (d), then -> M2_PRE_NOISE15_ZERO|NOISE7_ZERO
     *   m3_bios_load()         (f), then -> M3_PRE_BIOS_ZERO
     *   m4_rom_load_named()    (i), then -> M4_PRE_SIZE_SET|ROM_MAPPED
     *                                   and  M3_MAP_ROM_PRESENT
     *
     * A single flag says "all four or none". So consider the real sequence in
     * which session 1 installs the screen at (a) and then dies at (c) because the
     * greedy allocator handed back one buffer. The screen IS installed; the noise
     * tables are still zero; no BIOS and no cartridge were ever loaded. On the
     * next iteration a single flag offers only two expectations, 0 or the full
     * PRE2 mask, and the TRUE state -- M2_PRE_SCREEN_NULL alone -- is neither of
     * them. The exact-equality comparison fails, and because first_session is
     * now 0 it fails RUNTIME-fatal: LUAport ends itself, blaming the boundary,
     * on a console where nothing whatsoever is wrong.
     *
     * ***** THE FIX IS TO TRACK WHAT WAS ACTUALLY DONE AND COMPOSE THE EXPECTED
     * MASK FROM IT. ***** Each flag is set ONLY once its own setup call has
     * provably succeeded, and each contributes EXACTLY the bits gba_session.h
     * attributes to it -- spelled with that header's own macro names, never as
     * numbers, so a renumbered bit cannot change the meaning here. When all four
     * are set the composition is bit-for-bit GBA_SESS_PRE{2,3,4}_RELAUNCH and
     * GBA_SESS_MAP3_RELAUNCH, which is the steady state from session 2 onward.
     *
     * ***** THEY ALSO CHOOSE THE SEVERITY OF A MISMATCH, WHICH IS WHY THERE IS
     * NO SEPARATE first_session FLAG. ***** A pre-probe disagreement means one of
     * two completely different things, and the flag for the subsystem being
     * probed is exactly what distinguishes them: if that subsystem has NEVER been
     * set up, the dirty state came from the HOST PROCESS and is SESSION-fatal --
     * LUAport stays up and the operator may try another game. If it HAS been set
     * up, the state was left by a previous session's teardown, which is
     * BOUNDARY evidence and RUNTIME-fatal: every future session would inherit it.
     *
     * A single "is this the first time round the loop" flag could not express
     * that, and worse, it could not be cleared safely -- every session-fatal path
     * in step 451 uses `continue`, which would jump straight past any assignment
     * placed at the bottom of the loop body. Each flag here is set at the exact
     * point its own setup call succeeds, so there is no ordering hazard at all.
     * Because each is READ (while composing the expected mask) strictly before it
     * is WRITTEN within any one iteration, each one always answers about PREVIOUS
     * iterations, which is precisely the question being asked.
     *
     * ***** NOTHING IS WEAKENED. THE COMPARISON STAYS EXACT EQUALITY. ***** Any
     * bit that is not accounted for by a flag is STILL A FAILURE, on every
     * session. In particular M4_PRE_NO_BUFFERS and M4_PRE_BUF0_NULL are
     * contributed by no flag, so an arena that failed to rewind is caught exactly
     * as before; and m3_bios_probe_content() is gated at 0 unconditionally. This
     * makes the gate MORE precise on the early-failure paths and identical to
     * `first_session ? 0 : MASK` on the path where every setup step succeeded --
     * which is the only path M16-0 and M16-1 ever exercised, so no
     * hardware-proven behaviour changes. */
    int      screen_on = 0;   /* m2_set_screen() has run                      */
    int      sound_on  = 0;   /* m2_call_init_sound() has run                 */
    int      bios_on   = 0;   /* a BIOS image is loaded and validated         */
    int      rom_ever  = 0;   /* at least one cartridge has reached gpSP      */

    struct m16c_line l;
    char     lab[60];

    /* ***** FUNCTION SCOPE ON PURPOSE, AND IT IS LOAD-BEARING. *****
       install_fault_trap() stores the sockaddr POINTER in a static
       (runtime/fault.inc:15-17) and the handler dereferences it later, possibly
       much later. Declaring log_sa inside the bring-up block would leave that
       static pointing at a dead stack slot the instant the block exited. */
    s32      log_fd = -1;
    u8       log_sa[16];

    /* ---- step 440: native entry -----------------------------------------
       Nothing writable exists yet, so this uses only arguments and stack. The
       bootstrap is copied VERBATIM from the hardware-proven
       M5/M8/M10/M11/M12C/M13B/M13C path and is not varied by one line. */
    ext->step = 440;

    {
        void *early_sendto = SYM(G, D, LIBKERNEL_HANDLE, "sendto");
        s32   early_fd     = ext->log_fd;
        u8   *early_sa     = ext->log_addr;

        bmsg(G, early_sendto, early_fd, early_sa,
             "LUAport M16C: native entry, step", 440);
        bmsg(G, early_sendto, early_fd, early_sa,
             "LUAport M16C: image base", (u64)&_start);

        void *mmap   = SYM(G, D, LIBKERNEL_HANDLE, "mmap");
        void *munmap = SYM(G, D, LIBKERNEL_HANDLE, "munmap");

        /* ***** THESE THREE CANNOT DRAW A SCREEN AND DO NOT PRETEND TO. *****
           There is no video, no pad and -- for the first two -- no writable
           .data yet, so bmsg() (stack and arguments only) is the entire
           channel. The heading is still named, so the log and the notification
           use the same words the on-screen failures would have. */
        if (!mmap) {
            bmsg(G, early_sendto, early_fd, early_sa,
                 "LUAport M16C: " M16C_H_MEMORY " -- no mmap, status",
                 (u64)(s64)M16C_NO_MMAP);
            ext->status = M16C_NO_MMAP;
            return;
        }

        if (!boot_data_region(G, mmap, munmap, early_sendto, early_fd,
                              early_sa)) {
            bmsg(G, early_sendto, early_fd, early_sa,
                 "LUAport M16C: " M16C_H_MEMORY " -- relocations, status",
                 (u64)(s64)M16C_BOOT_FAILED);
            ext->status = M16C_BOOT_FAILED;
            return;
        }
        bmsg(G, early_sendto, early_fd, early_sa,
             "LUAport M16C: relocations OK, step", 441);

        /* ---- step 441: runtime bring-up -------------------------------- */
        ext->step = 441;

        {
            void *gettod = SYM(G, D, LIBKERNEL_HANDLE, "gettimeofday");
            void *sendto = SYM(G, D, LIBKERNEL_HANDLE, "sendto");
            void *kopen  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelOpen");
            void *kread  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelRead");
            void *kwrite = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelWrite");
            void *kclose = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelClose");
            void *klseek = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLseek");
            void *kmkdir = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMkdir");

            void *arena;
            struct shim_ps5 ps5;

            log_fd = ext->log_fd;
            for (i = 0; i < 16u; i++) log_sa[i] = ext->log_addr[i];

            arena = (void *)NC(G, mmap, 0, ARENA_SIZE, 3, 0x1002, (u64)-1, 0);
            if ((s64)arena == -1) {
                bmsg(G, sendto, log_fd, log_sa,
                     "LUAport M16C: " M16C_H_MEMORY " -- arena, status",
                     (u64)(s64)M16C_NO_ARENA);
                ext->status = M16C_NO_ARENA;
                return;
            }

            ps5.gadget = G;      ps5.open   = kopen;   ps5.read = kread;
            ps5.write  = kwrite; ps5.close  = kclose;  ps5.lseek = klseek;
            ps5.mkdir  = kmkdir; ps5.sendto = sendto;  ps5.gettimeofday = gettod;
            ps5.log_fd = log_fd; ps5.log_sa = log_sa;
            shim_init(&ps5, arena, ARENA_SIZE);

            /* ***** THIS IS WHAT KEEPS M16C_TITLE LIVE. ***** The masthead no
               longer draws it -- the player sees the exact "LUAp0rt GBA"
               wordmark instead -- so this startup line is now the only
               reference to the literal, and m16c-verify greps rodata.bin for
               it (Makefile:16303). It is a genuine one-shot developer
               diagnostic on the bring-up path, printed once per payload start:
               not per-frame, not runtime spam, and never shown to the player.
               klog() takes no varargs (runtime/shim.h:31), so the identity is
               concatenated at compile time rather than formatted. */
            klog("LUAport M16C: runtime init OK -- internal identity "
                 M16C_TITLE "\n");
            klog("=== LUAport GBA launcher -- THE PICKER COMES BACK\n");

            install_fault_trap(G, D, sendto, log_fd, log_sa);
        }
    }

    /* ---- step 442: image measurement ------------------------------------
     * Every number below goes to the log; NONE of it reaches the screen. */
    ext->step = 442;

    printf("M16C: text+rodata+rela ends at %u\n", (unsigned)(u64)__data_load);
    printf("M16C: data bytes %u\n",
           (unsigned)((u64)__data_end - (u64)__data_start));
    printf("M16C: bss bytes %u\n",
           (unsigned)((u64)__bss_end - (u64)__bss_start));
    printf("M16C: arena %u bytes\n", (unsigned)ARENA_SIZE);
    printf("M16C: UI surface %u bytes, GBA screen %u bytes\n",
           (unsigned)sizeof(m16c_surface), (unsigned)sizeof(m16c_gba_screen));
    printf("M16C: library ceiling %u entries, NO session duration bound, "
           "emergency watchdog %u iterations (~%u min at %u Hz)\n",
           (unsigned)GBA_LIB_MAX_ENTRIES, (unsigned)M16C_PLAY_MAX_ITERS,
           (unsigned)M16C_WD_MINUTES, (unsigned)M16C_WD_FPS_CEIL);
    printf("M16C: RFILE pool %u slots\n", gba_rfile_slots());

    klog("LUAport M16C: ***** THIS IS THE LAUNCHER, AND IT RETURNS TO ITS OWN "
         "PICKER. ***** It executes the cartridge the operator picks and it "
         "RESTORES and COMMITS that cartridge's save. It may create or "
         "overwrite EXACTLY TWO FILES PER GAME -- /savedata0/gba/<ID>.sav and "
         "<ID>.hdr, named from the CARTRIDGE'S OWN CONTENT -- and it opens a "
         "read-write window ONLY after a clean exit and ONLY when the save "
         "actually changed. It deletes nothing, renames nothing, and touches no "
         "other cartridge's save\n");
    klog("LUAport M16C: HOLD L1+R1+L2+R2 DURING PLAY to return to the picker. "
         "PRESS CIRCLE AT THE PICKER to exit LUAport\n");

    /* ---- step 443: symbol resolution ------------------------------------ */
    ext->step = 443;
    if (!m13store_init(&m16c_store, G, D)) {
        printf("M16C: open=%s close=%s getdents=%s\n",
               m16c_store.open     ? "OK" : "MISSING",
               m16c_store.close    ? "OK" : "MISSING",
               m16c_store.getdents ? "OK" : "MISSING");
        printf("M16C: ***** %s ***** the directory primitives could not be "
               "resolved -- nothing below this point can be attempted\n",
               M16C_H_LIBRARY);
        ext->status = M16C_NO_SYMS;
        return;
    }
    printf("M16C: getdents resolved as '%s'\n", m16c_store.getdents_name);

    /* ---- step 444: THE SAVEDATA LAYER -----------------------------------
     *
     * ***** THE POSITION IS M12B's, M13B-5's AND M13C's, AND IT IS COPIED
     * RATHER THAN CHOSEN. ***** plat_savedata_init() loads a MODULE
     * (libSceSaveData.sprx) through sceKernelLoadStartModule; doing that with a
     * VideoOut open, an audio port open, an 8 MB cartridge resident and the
     * arena fully committed is a configuration NO milestone has ever tested.
     * Doing it here is a configuration seven have.
     *
     * ***** IT IS PAYLOAD-SCOPED AND IS DONE EXACTLY ONCE. ***** It resolves the
     * mount primitive and loads the module; it MOUNTS NOTHING and writes no byte
     * (runtime/savedata.h:95-105). Re-initialising it per game would be a new
     * failure mode for no benefit, and the mount window that DOES open is opened
     * and closed inside gba_savefile_commit() on each commit.
     *
     * ***** AND IT FAILS BEFORE THE PICKER. ***** A launcher that cannot commit
     * a save would let the operator play for an hour and then lose it. */
    ext->step = 444;
    {
        void *kmkdir  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMkdir");
        void *loadmod = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLoadStartModule");

        printf("M16C: sceKernelMkdir %s, sceKernelLoadStartModule %s\n",
               kmkdir  ? "resolved" : "NULL",
               loadmod ? "resolved" : "NULL");

        sd_init_rc = plat_savedata_init(G, D, loadmod, eboot_base,
                                        (s32)ext->dbg[3], kmkdir);
        printf("M16C: plat_savedata_init() returned %d\n", sd_init_rc);
    }

    if (sd_init_rc != PLAT_SD_OK || !plat_savedata_is_ready()) {
        printf("M16C: ***** %s ***** plat_savedata_init returned %d\n",
               M16C_H_STORAGE, sd_init_rc);
        klog("LUAport M16C: ***** THE SAVEDATA LAYER WOULD NOT COME UP. ***** "
             "No save could ever be committed, so the launcher is refused NOW, "
             "before anything is played and lost. NOTHING has been mounted, "
             "opened or written\n");
        ext->dbg[4] = (u64)(u32)(s32)sd_init_rc;
        ext->status = M16C_SD_INIT_BAD;
        return;
    }
    klog("LUAport M16C: the savedata layer is READY. ***** NOTHING IS MOUNTED "
         "READ-WRITE YET. ***** The window opens at a commit, only on a clean "
         "exit, and only if the save actually changed\n");

    /* ---- step 445: SCAN THE ROM LIBRARY ----------------------------------
     *
     * ***** SCANNED ONCE, AT LAUNCH, AND REUSED ON EVERY PICKER RETURN. *****
     * That is M16-1's policy, preserved deliberately rather than by omission.
     * Rescanning when a game ends would re-open every file on a mount whose
     * state the payload no longer controls, on a path that has just torn down a
     * cartridge -- for the sole benefit of noticing a file that appeared while
     * the console was mid-game. The chosen entry IS re-measured immediately
     * before every load (step 451 (g)), which is where a changed file actually
     * matters, and a size that moved is REFUSED. */
    ext->step = 445;
    printf("M16C: scanning %s\n", M16C_ROM_DIR);

    scanned = gba_lib_scan(&m16c_store, M16C_ROM_DIR, &m16c_lib,
                           m16c_dirbuf, (unsigned)sizeof(m16c_dirbuf));

    if (scanned < 0) {
        printf("M16C: SCAN FAILED rc=%d (0x%X) %s\n",
               scanned, (unsigned)scanned, m13store_errname((s32)scanned));
        klog("LUAport M16C: the ROM directory would not enumerate\n");
        ext->dbg[0] = (u64)(unsigned)scanned;
        ext->status = M16C_SCAN_FAILED;
        return;
    }

    printf("M16C: entries seen %u, accepted %u\n",
           m16c_lib.seen, m16c_lib.count);
    printf("M16C: rejected -- hidden %u, dir %u, ext %u, empty %u, "
           "PATH TOO LONG %u, table full %u\n",
           m16c_lib.rej_hidden, m16c_lib.rej_dir, m16c_lib.rej_ext,
           m16c_lib.rej_empty, m16c_lib.rej_toolong, m16c_lib.overflow);
    printf("M16C: getdents calls %u, last rc %d\n",
           m16c_lib.calls, (int)m16c_lib.last_rc);

    if (m16c_lib.capped)
        klog("LUAport M16C: ***** THE getdents CALL CAP WAS REACHED. ***** The "
             "directory never reported an end, so the walk was STOPPED to keep "
             "the console from hanging. The listing is INCOMPLETE\n");

    if (m16c_lib.overflow)
        printf("M16C: ***** %u ENTRIES OVERFLOWED THE %u-ENTRY TABLE AND ARE "
               "NOT LISTED *****\n",
               m16c_lib.overflow, (unsigned)GBA_LIB_MAX_ENTRIES);

    for (i = 0; i < m16c_lib.count; i++) {
        printf("M16C: ROM %u\n", i);
        printf("M16C:   NAME %s\n", gba_lib_name(&m16c_lib, i));
        printf("M16C:   PATH %s\n", gba_lib_path(&m16c_lib, i));
    }

    ext->dbg[0] = (u64)m16c_lib.count;
    ext->dbg[1] = (u64)m16c_lib.seen;
    ext->dbg[2] = (u64)(m16c_lib.rej_hidden + m16c_lib.rej_dir +
                        m16c_lib.rej_ext + m16c_lib.rej_empty +
                        m16c_lib.rej_toolong);
    ext->dbg[4] = (u64)m16c_lib.overflow;

    /* ***** THE DETERMINISTIC ORDER. ***** Built once, here, so the same drive
       always produces the same on-screen order on every picker entry. The sort
       is an index permutation and never moves a library entry. */
    m13b_sort(&m16c_lib, m13b_order, m16c_lib.count);

    /* ---- step 446: video, pad and audio ---------------------------------
     *
     * ***** ALL THREE ARE PAYLOAD-SCOPED AND NONE IS EVER RE-INITIALISED. *****
     * gba_session_end() deliberately does not touch them (gba_session.c:156-158)
     * because a menu cannot be drawn on a framebuffer that was released or read
     * from a pad that was closed, and gba_session.h:98-101 states the audio rule
     * explicitly: "Video, pad and the audio PORT stay up across the boundary ...
     * reopening an audio port per game would be a new failure mode for no
     * benefit. Only the audio RING is reset." m16c_teardown() is the only caller
     * of their shutdowns and it runs exactly once.
     *
     * ***** M13C BRINGS AUDIO UP INSIDE THE SESSION (main.c:2509-2520). M16-2
     * HOISTS IT, FOR THE SAME REASON IT HOISTS THE CLOCK. ***** An audio port is
     * a property of the PROCESS: sceAudioOutOpen does not become possible between
     * games. Leaving the open inside the loop would re-run a module load and a
     * port open before every cartridge -- eight opens for eight games, each one a
     * fresh chance to fail after the operator had already chosen -- and
     * plat_audio_shutdown() is NOT part of the session boundary, so the second
     * open would be issued while the first port was still held. M16-1 proved the
     * once-per-payload shape on hardware (apps/m16bdiag/main.c:924, outside its
     * session loop at :948). */
    ext->step = 446;

    rc = plat_video_init(G, D, eboot_base);
    printf("M16C: plat_video_init() returned %d\n", rc);
    if (rc != PLAT_VID_OK) {
        printf("M16C: ***** %s ***** plat_video_init() returned %d -- there is "
               "no framebuffer to draw on, so the launcher stops here\n",
               M16C_H_VIDEO, rc);
        ext->dbg[4] = (u64)(s64)rc;
        ext->status = M16C_NO_VIDEO;
        return;
    }
    m16c_video_up = 1u;

    m16c_fb0 = plat_video_get_framebuffer(0);
    m16c_fb1 = plat_video_get_framebuffer(1);
    printf("M16C: framebuffers fb0=%p fb1=%p\n",
           (void *)m16c_fb0, (void *)m16c_fb1);

    /* ***** THE FILLS STAY. ***** They clear BOTH hardware framebuffers before
       anything is presented from them, so neither buffer can flash whatever the
       previous process left in memory. m16c_flip() below blits the full surface
       over the top of one of them each frame, which makes the fills redundant
       for the frames that are actually shown -- but they are the guarantee that
       an unshown buffer is never garbage, and that is not this change's to
       remove.
       ***** THE BLIND BLACK PRESENTS BECOME THE IDENTITY SCREEN. ***** Same
       place, same shape, same absence of input: the only difference is that the
       frames now carry the payload's name instead of nothing. m16c_flip() is
       used rather than a raw plat_video_present() so this screen goes through
       the identical blit-and-present path as every other screen in the file and
       keeps m16c_frame_id monotonic, which is the payload-did-not-restart
       evidence read at step 459.

       ***** THE SCREEN NOW FADES UP AND BACK DOWN, AND THE FADE SITS BETWEEN
       THE DRAW AND THE FLIP. ***** m16c_draw_splash() composes the identical
       surface on every one of these 186 frames -- it is not aware of the fade
       and was not changed -- and m16c_fade_surface() then dims the finished
       result before m16c_flip() blits it. Redrawing the whole screen each frame
       rather than dimming the previous frame's output is what keeps the ramp
       exact: the error cannot accumulate, because every frame is scaled once
       from the pristine drawing instead of from the frame before it.

       The first presented frame is k = 0 and the last is k = 0, both of which
       m16c_fade_surface() turns into exact 0xFF000000 -- so this screen opens
       from true black and lands on true black, with the two fills above still
       guaranteeing that the buffer nobody is looking at is never garbage. */
    plat_video_fill(0, M16C_C_BG);
    plat_video_fill(1, M16C_C_BG);
    for (i = 0; i < M16C_SPLASH_FADE_IN; i++) {
        m16c_draw_splash();
        m16c_fade_surface((i * 256u) / (M16C_SPLASH_FADE_IN - 1u));
        m16c_flip();
    }
    for (i = 0; i < M16C_SPLASH_HOLD; i++) {
        m16c_draw_splash();
        m16c_flip();
    }
    for (i = 0; i < M16C_SPLASH_FADE_OUT; i++) {
        m16c_draw_splash();
        m16c_fade_surface(256u - (i * 256u) / (M16C_SPLASH_FADE_OUT - 1u));
        m16c_flip();
    }

    /* ***** THE userId CONTRACT. ***** scePadGetHandle needs the REAL user id
       from sceUserServiceGetInitialUser, handed across in ext->dbg[3]. 0xFF is
       the sceVideoOutOpen/sceAudioOutOpen convention and is NOT a user id. */
    rc = plat_pad_init(G, D, (s32)ext->dbg[3]);
    printf("M16C: plat_pad_init(user 0x%08X) returned %d\n",
           (unsigned)ext->dbg[3], rc);
    if (rc != PLAT_PAD_OK) {
        klog("LUAport M16C: no pad -- a launcher with no input cannot be "
             "operated\n");
        ext->dbg[4] = (u64)(s64)rc;
        m16c_runtime_fatal(ext, M16C_NO_PAD, M16C_H_PAD,
                           "NO CONTROLLER COULD BE OPENED.",
                           "SIGN A USER IN ON THE CONSOLE AND TRY AGAIN.", 0);
        return;
    }
    m16c_pad_up = 1u;

    /* ***** AUDIO LAST, AFTER VIDEO AND THE PAD -- M10's AND M13C's ORDER,
       COPIED RATHER THAN CHOSEN. ***** Audio is the newest of the three
       subsystems, so bringing it up after the two that are already proven means a
       failure here cannot be mistaken for a video or pad regression.
       plat_audio_init() TAKES NO USER ID: sceAudioOutOpen needs 0xFF and
       scePadGetHandle needs the REAL id, the two calls look alike and want
       opposite values, and the backend supplies 0xFF internally so it is not a
       value this launcher can get wrong (runtime/audio.h:45-58). */
    rc = plat_audio_init(G, D);
    printf("M16C: plat_audio_init() returned %d, handle %d\n",
           rc, (int)plat_audio_handle());
    if (rc != PLAT_AUD_OK) {
        ext->dbg[4] = (u64)(s64)rc;
        m16c_runtime_fatal(ext, M16C_NO_AUDIO, M16C_H_AUDIO,
                           "THE AUDIO OUTPUT COULD NOT BE OPENED.",
                           "NO SESSION WAS STARTED RATHER THAN RUN SILENT.", 0);
        return;
    }
    m16c_audio_up = 1u;

    /* ***** THE RING IS CLEARED HERE FOR SESSION 1 ONLY, AND EVERY LATER SESSION
       GETS ITS RESET FROM THE BOUNDARY. ***** gba_audio_reset() clears the ring,
       the resampler and gpSP's own mix bus -- render_gbc_sound() accumulates with
       `+=` and only sound_read_samples() zeroes a slot, so without a reset the
       first samples of a session would be the PREVIOUS cartridge's residue. That
       is exactly why it is step 2 of gba_session_end() (gba_session.h:118), and
       m16c_boundary() runs gba_session_end() on EVERY path out of a session --
       clean, unclean and every session-fatal failure alike. So sessions 2..N enter
       with a cleared ring by construction and session 1 is cleared here. This is a
       CALL INTO gpSP, not a modification of it. */
    gba_audio_reset();
    printf("M16C: audio %u Hz -> %u Hz, ratio %u:%u, ring %u, grain %u\n",
           (unsigned)GBA_AUDIO_SRC_HZ, (unsigned)GBA_AUDIO_DST_HZ,
           (unsigned)GBA_AUDIO_DEC_IN, (unsigned)GBA_AUDIO_DEC_OUT,
           (unsigned)GBA_AUDIO_RING_FRAMES,
           (unsigned)GBA_AUDIO_GRAIN_FRAMES);

    /* ***** THE PAYLOAD WATERMARK. ***** Video, pad and audio all come up BEFORE
       the ROM buffers, and m4_rom_buffer_init() is GREEDY (gba_rom.h:68-73) -- it
       mallocs 1 MB blocks until one FAILS -- so anything allocated ahead of it
       REDUCES the block count and the binding gate is `buffers >= 2`. In a
       LOOPING launcher this figure acquires a second job: it is the floor every
       session's arena_rewind() must return to, and m16c_boundary() prints it
       beside the measured value on every boundary.

       ***** THE AUDIO BRING-UP DOES NOT MOVE THIS NUMBER, WHICH IS WHY IT WAS
       SAFE TO HOIST AHEAD OF IT. ***** adapters/gba/gba_audio.c:27 is explicit --
       "no malloc/free. Every buffer below is static." The ring, the pull buffer
       and the grain buffer are all .bss, so the port opening earlier costs the
       cartridge window nothing. */
    m16c_arena_baseline = arena_used();
    printf("M16C: arena after video + pad + audio init: %u of %u -- THE PAYLOAD "
           "BASELINE\n",
           (unsigned)m16c_arena_baseline, (unsigned)ARENA_SIZE);
    ext->dbg[5] = m16c_arena_baseline;

    /* ---- THE EMPTY LIBRARY ----------------------------------------------- */
    if (m16c_lib.count == 0u) {
        unsigned frame;
        klog("LUAport M16C: the ROM directory contains no .gba file. Upload a "
             "cartridge with `make m13b-upload ROM=<file>` and re-run\n");

        prev_buttons = 0u;
        for (frame = 0; frame < M16C_FAIL_FRAMES; frame++) {
            u32 pressed;
            (void)plat_pad_read(&pad);
            pressed      = pad.buttons & ~prev_buttons;
            prev_buttons = pad.buttons;
            if (pressed & PAD_CIRCLE) break;
            m16c_draw_empty();
            m16c_flip();
        }
        m16c_teardown();
        ext->status = M16C_NO_ROMS;
        return;
    }

    /* ---- step 447: VALIDATE EVERY ENTRY ---------------------------------
     *
     * ***** THE EXISTING M4 GATE AND NO SECOND ONE. ***** gba_library.h:15-23
     * makes the argument in full: adapters/gba/gba_rom.h has owned ROM
     * validation since M4 and it is hardware-proven. A launcher that
     * re-implemented any of it would become a SECOND, DIVERGING validator.
     *
     * ***** IT RUNS ONCE, BEFORE THE FIRST ROW IS EVER DRAWN, AND IS REUSED ON
     * EVERY PICKER RETURN. ***** Every open is READ-ONLY and every one is closed
     * by m4_rom_query_named() before it returns. */
    ext->step = 447;
    m16c_valid    = 0u;
    m16c_rejected = 0u;

    for (i = 0; i < m16c_lib.count && i < (unsigned)GBA_LIB_MAX_ENTRIES; i++) {
        const char *p       = gba_lib_path(&m16c_lib, i);
        unsigned    len     = m13store_strlen(p);
        int         fits    = (len + 1u) <= M4_ROM_NAME_MAX;
        unsigned    size    = 0u;
        unsigned    srcmask = 0u;
        int         opened  = 0;

        /* THE LENGTH TEST COMES FIRST AND SHORT-CIRCUITS THE OPEN. An over-long
           path must be reported as NAME TOO LONG, not as CANNOT OPEN: the two
           send the operator to completely different remedies. */
        if (fits) {
            opened = m4_rom_query_named(p, &size);
            if (opened)
                srcmask = m4_rom_probe_source(size);
        }

        m13b_item_set(&m16c_items[i], fits, opened, size, srcmask);

        if (m16c_items[i].sel) m16c_valid++;
        else                   m16c_rejected++;

        printf("M16C: ENTRY %u %s -- %u bytes, %s\n", i,
               gba_lib_name(&m16c_lib, i), size,
               m13b_reason_text(m16c_items[i].reason));
    }

    printf("M16C: %u available, %u unavailable of %u listed\n",
           m16c_valid, m16c_rejected, m16c_lib.count);
    ext->dbg[6] = (u64)m16c_valid;

    if (m16c_valid == 0u) {
        klog("LUAport M16C: every entry was rejected by the ROM gate. The "
             "directory is not empty, so this is a cartridge problem and not a "
             "storage one -- the per-row reason is on screen\n");
        m16c_runtime_fatal(ext, M16C_NO_SELECTABLE, M16C_H_ROM_BAD,
                           "EVERY FILE IN THE LIBRARY WAS REJECTED.",
                           "THE FILES ARE PRESENT BUT NONE IS A LOADABLE",
                           "CARTRIDGE. CHECK THE PER-ROW REASON IN THE LIST.");
        return;
    }

    /* Start the cursor on the first AVAILABLE row rather than on row 0. Opening
       on a dimmed, unlaunchable entry invites the operator to press CROSS and
       conclude the launcher is broken when it correctly refuses. */
    for (i = 0; i < m16c_lib.count; i++) {
        if (m16c_items[m13b_order[i]].sel) { sel = i; break; }
    }

    /* ---- step 448: THE CLOCK. HOISTED OUT OF THE SESSION. ----------------
     *
     * ***** M13C INITIALISES THE CLOCK INSIDE THE SESSION (main.c:2399-2419).
     * M16-2 MOVES IT TO PAYLOAD SCOPE, AND THAT IS A DELIBERATE, ARGUED CHANGE
     * RATHER THAN A TIDY-UP. *****
     *
     * runtime/platform.c:89-92 is unambiguous: plat_time_us() returns 0 -- not a
     * stale value, not garbage -- unless plat_time_init() has resolved
     * gettimeofday first. `elapsed_us > 0` is a TERM IN THE CLEAN VERDICT, so a
     * dead clock fails EVERY session at the verdict no matter how perfect the
     * play was.
     *
     * In M13C that cost the operator ONE session. In a looping launcher it would
     * cost them EVERY session, one after another, each one playable and each one
     * refused at the end with its save discarded. The clock is a property of the
     * PROCESS -- gettimeofday does not become resolvable between games -- so it
     * is resolved once, checked twice exactly as M9/M10/M13C check it, and a
     * failure ends the payload here instead of ambushing the operator after the
     * first game they finish.
     *
     * ***** THERE IS DELIBERATELY NO FALLBACK TO A FRAME-COUNTED PSEUDO-CLOCK.
     * ***** Elapsed time must come from the real initialised wall clock. */
    ext->step = 448;
    time_rc = plat_time_init(G, D);
    printf("M16C: plat_time_init() returned %d\n", time_rc);
    if (time_rc != PLAT_TIME_OK) {
        ext->dbg[4] = (u64)(u32)(s32)time_rc;
        m16c_runtime_fatal(ext, M16C_TIME_INIT_BAD, M16C_H_TIMING,
                           "THE SYSTEM CLOCK COULD NOT BE READ.",
                           "NO SESSION WAS STARTED RATHER THAN RUN UNTIMED.",
                           0);
        return;
    }
    if (plat_time_us() == 0u) {
        ext->dbg[4] = 0;
        m16c_runtime_fatal(ext, M16C_CLOCK_DEAD, M16C_H_TIMING,
                           "THE SYSTEM CLOCK OPENED BUT READ ZERO.",
                           "NO SESSION WAS STARTED RATHER THAN RUN UNTIMED.",
                           0);
        return;
    }
    printf("M16C: clock live -- plat_time_us() = %llu us\n",
           (unsigned long long)plat_time_us());

    klog("LUAport M16C: ================================================\n");
    klog("LUAport M16C: BRING-UP COMPLETE. Everything above this line "
         "happens ONCE PER PAYLOAD and is never repeated.\n");
    klog("LUAport M16C: ================================================\n");

    /* =====================================================================
     * ***** THE OUTER LOOP. THE ONE BACKWARD BRANCH IN THE WHOLE FILE. *****
     * =====================================================================
     *
     * M13C's _start() has NO backward branch anywhere in it, and returning from
     * it IS the payload exiting (gba_session.h:48-52). This `for (;;)` is the
     * entire structural difference between the two launchers.
     *
     * ***** EVERY SESSION-SCOPED VARIABLE IS DECLARED INSIDE THIS BODY. *****
     * That is protection #1 against stale state -- C re-initialises a block
     * scope on every iteration, so there is no function-scope end_reason,
     * `clean`, `modified` or commit_state to survive from one game to the next.
     * Protection #2 is m16c_session_reset() plus m16c_session_is_fresh(), below.
     *
     * THE ONLY WAYS OUT OF THIS LOOP:
     *   CIRCLE at the picker        -> break, then the payload exit at step 459
     *   a RUNTIME-FATAL failure     -> return from _start() directly
     *   a contaminated boundary     -> return from _start() directly
     * A game ending -- cleanly, by watchdog, or by any session-fatal failure --
     * does NOT leave this loop. */
    for (;;) {
        /* ---- SESSION-SCOPED DECLARATIONS ----------------------------- */
        unsigned frame        = 0u;
        unsigned chosen_idx   = 0u;
        int      have_choice  = 0;
        unsigned m            = 0u;

        u64      mark         = 0;
        int      mark_taken   = 0;
        unsigned rfile_before = 0u;

        unsigned buffers  = 0u;
        unsigned rom_size = 0u;
        unsigned expect2 = 0u, expect3 = 0u, expect4 = 0u, expect_map = 0u;
        unsigned mapmask_before = 0u, mapmask_after = 0u;
        unsigned biosmap_raw = 0u, biosmap_after = 0u;
        unsigned buffers_after = 0u;
        unsigned metamask_after = 0u, contentmask_after = 0u;
        unsigned gamepak_size_before = 0u, gamepak_size_after = 0u;
        unsigned romhash_before = 0u, romhash_after = 0u;
        int      id_survived = 0;

        u64      arena_before_buf = 0;
        u64      arena_after_buf  = 0;

        char     save_id[GBA_SAVE_ID_MAX];
        char     save_id_after[GBA_SAVE_ID_MAX];
        char     title[16];
        char     code[8];
        char     sav_name[GBA_SAVE_NAME_MAX];

        int         probe_rc  = GBA_RESTOREFILE_ENOSAVE;
        const char *save_word = "NONE";

        /* ---- R4: THE DETAILS-SCREEN CONFIRMATION STATE ------------------
         *
         * ***** ALL FOUR ARE SESSION-SCOPED LOCALS, AND THAT IS A BUDGET
         * DECISION AS WELL AS A CORRECTNESS ONE. ***** They live on the stack
         * inside this for(;;) body, alongside every other per-session figure
         * declared above, so they are re-initialised from scratch on EVERY
         * entry to the details screen and cannot carry a decision from one
         * cartridge into the next. A file-scope `static` would have done the
         * same job and moved .bss off its frozen 2,011,424 -- which R4 is not
         * permitted to do -- so the stack is both the correct scope and the
         * only affordable one.
         *
         * det_in     the PROVEN CROSS machine from m13b_picker.inc, a SECOND
         *            instance. The picker's `in` is left completely alone: it
         *            is sitting in terminal M13B_IN_LAUNCH at this point and is
         *            reset by m16c_picker_reentry() on the way back, so
         *            borrowing it here would entangle two screens' input.
         * det_prev   the edge detector, primed from a LIVE pad read on entry.
         * det_clear  consecutive frames with M16C_DETAILS_ARM_MASK clear.
         * det_armed  0 until that run reaches M16C_RELEASE_SETTLE. While 0 the
         *            CROSS machine is NOT STEPPED AT ALL -- see the loop. */
        struct m13b_input det_in;
        u32      det_prev   = 0u;
        unsigned det_clear  = 0u;
        int      det_armed  = 0;
        int      det_action = M16C_DET_NONE;

        save_id[0]       = '\0';
        save_id_after[0] = '\0';
        title[0]         = '\0';
        code[0]          = '\0';
        sav_name[0]      = '\0';

        /* ---- THE RESET PROTECTION, RUN AND THEN VERIFIED --------------
         *
         * ***** PROTECTION #2. ***** Block scope above already guarantees fresh
         * locals; this proves the shared record is fresh too, and the gameplay
         * block below SEEDS its local end_reason FROM this verified record
         * rather than from a literal, so the check and the value cannot drift
         * apart. A failure here means the reset itself is broken, which is
         * RUNTIME-fatal: every future session would inherit the same corruption.
         */
        m16c_session_reset();
        if (!m16c_session_is_fresh()) {
            printf("M16C: ***** THE PER-SESSION RESET DID NOT TAKE *****\n");
            m16c_runtime_fatal(ext, M16C_SESSION_DIRTY, M16C_H_NOTREADY,
                               "INTERNAL STATE COULD NOT BE CLEARED BETWEEN",
                               "GAMES. LUAPORT STOPPED RATHER THAN RUN A GAME",
                               "WITH ANOTHER GAME'S RESULT ATTACHED TO IT.");
            return;
        }

        /* =================================================================
         * step 449 -- THE PICKER
         * ================================================================= */
        ext->step = 449;

        if (first_entry)
            klog("LUAport M16C: picker up\n");
        else
            printf("M16C: ======== RETURNING TO THE PICKER (frame id %u, "
                   "%u session(s) played, %u clean, %u failed) ========\n",
                   (unsigned)m16c_frame_id, m16c_sessions_started,
                   m16c_sessions_clean, m16c_sessions_failed);

        /* ***** THE RE-ENTRY FIX. ***** Resets the terminal input machine,
           holds input until the return chord is released, primes prev_buttons
           from a LIVE pad read and re-clamps the scroll window. See
           m16c_picker_reentry(). */
        m16c_picker_reentry(&in, &prev_buttons, sel, &top, first_entry);
        first_entry = 0;

        for (frame = 0; frame < M16C_PICKER_MAX_FRAMES; frame++) {
            u32 pressed;

            (void)plat_pad_read(&pad);
            pressed      = pad.buttons & ~prev_buttons;
            prev_buttons = pad.buttons;

            /* ***** CIRCLE -- THE PAYLOAD EXIT, AND IT STAYS THE PAYLOAD
               EXIT. ***** The source audit found no architectural reason to
               move it: it is the only way out that is not the power button,
               m16c_teardown() is already correct here, and it is the sole
               producer of M16C_EXIT. In M13C this ended a payload that had not
               run a game; here it may end one that has run several, which is
               why the ledger is printed at step 459. */
            if (pressed & PAD_CIRCLE) {
                user_exit = 1;
                break;
            }

            if (pressed & PAD_UP)    sel = m13b_move(sel, m16c_lib.count, -1);
            if (pressed & PAD_DOWN)  sel = m13b_move(sel, m16c_lib.count, +1);
            if (pressed & PAD_L1)    sel = m13b_move(sel, m16c_lib.count,
                                                     -(int)M13B_ROWS);
            if (pressed & PAD_R1)    sel = m13b_move(sel, m16c_lib.count,
                                                     +(int)M13B_ROWS);

            top = m13b_scroll(sel, m16c_lib.count, top);

            {
                unsigned st;
                unsigned idx = (unsigned)m13b_order[sel];

                /* AN UNAVAILABLE ROW CANNOT LATCH. The press is simply not fed
                   to the machine, so a rejected entry cannot enter WAIT and
                   cannot time out either. */
                if (m16c_items[idx].sel || in.state != M13B_IN_IDLE) {
                    st = m13b_input_step(&in, (pad.buttons & PAD_CROSS) ? 1 : 0);

                    /* ***** THE LAUNCH IS ON THE RELEASE EDGE. ***** It is
                       structural rather than a convention: M13B_IN_LAUNCH is
                       only reachable from the CROSS-up transition, so the
                       starting cartridge never sees a phantom A press. */
                    if (st == M13B_IN_LAUNCH) {
                        chosen_idx  = idx;
                        have_choice = 1;
                        break;
                    }
                    if (st == M13B_IN_TIMEOUT) {
                        klog("LUAport M16C: CROSS was held for the whole "
                             "release window and never came up. Suspect a stuck "
                             "button or a controller that dropped mid-press. "
                             "Nothing was launched\n");
                        m13b_input_reset(&in);
                    }
                }
            }

            m16c_draw_picker(sel, top, &in, 0);
            m16c_flip();
        }

        if (user_exit)
            break;                       /* -> the payload exit at step 459 */

        if (!have_choice) {
            /* ***** THE PICKER TIMEOUT ENDS THE PAYLOAD, AND THAT IS NOT AN
               OVERSIGHT. ***** It is the ONE failure on the picker's own path
               rather than a game's, and its meaning is "nobody is here". There
               is nothing to return to: looping back would redraw the same
               picker for the same absent operator forever, which is precisely
               the hung console the bound exists to prevent. No cartridge was
               opened, so no session boundary is owed. */
            klog("LUAport M16C: the picker reached its frame bound with no "
                 "selection. The bound exists so that an unattended console "
                 "reports a diagnosable timeout instead of hanging forever\n");
            printf("M16C: %u frames elapsed with no selection and no exit\n",
                   frame);
            m16c_runtime_fatal(ext, M16C_PICKER_TIMEOUT, M16C_H_NOTREADY,
                               "NO CARTRIDGE WAS CHOSEN.",
                               "LUAPORT CLOSED ITSELF RATHER THAN WAIT FOREVER.",
                               0);
            return;
        }

        /* =================================================================
         * step 450 -- THE LATCH
         * =================================================================
         * ***** LATCH THE EXACT SELECTED FULL PATH. ***** Copied ONCE, out of
         * the library entry, and REFUSED rather than truncated. The on-screen
         * label may have been shortened by m13b_label() AND had its ".gba"
         * dropped by m16c_display_name(); this never was, and no display buffer
         * can reach it. Both of those operate on caller-owned display storage
         * and take their source as `const char *`, so the bytes handed to
         * m16c_capture() below are the library entry's own, extension and all.
         * THE PATH LATCHED HERE IS WHAT NAMES THE SAVE FILE -- if a display
         * name could ever reach it, cartridges would change identity the moment
         * the picker changed its typography. */
        ext->step = 450;
        m16c_sessions_started++;

        if (!m16c_capture(m16c_chosen, (unsigned)sizeof(m16c_chosen),
                          gba_lib_path(&m16c_lib, chosen_idx)) ||
            !m16c_capture(m16c_chosen_name, (unsigned)sizeof(m16c_chosen_name),
                          gba_lib_name(&m16c_lib, chosen_idx))) {
            klog("LUAport M16C: the chosen path would not fit and was REFUSED, "
                 "not truncated. Nothing was opened\n");
            /* SESSION-FATAL: nothing was opened, no mark was taken, and every
               other row in the library is still perfectly launchable. */
            if (!m16c_session_fatal(ext, M16C_NAME_TOO_LONG, M16C_H_ROM_OPEN,
                                    "THE FILE NAME IS TOO LONG TO OPEN SAFELY.",
                                    "RENAME THE FILE TO SOMETHING SHORTER AND",
                                    "TRY AGAIN. OTHER GAMES ARE UNAFFECTED.",
                                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        printf("M16C: ======== SESSION %u -- CHOSEN ========\n",
               m16c_sessions_started);
        printf("M16C: CHOSEN NAME %s\n", m16c_chosen_name);
        printf("M16C: CHOSEN PATH %s\n", m16c_chosen);
        printf("M16C: MEASURED SIZE %u bytes\n", m16c_items[chosen_idx].size);
        ext->dbg[7] = (u64)m16c_items[chosen_idx].size;

        /* THE SHORT ACKNOWLEDGEMENT. Drawn AFTER the release edge and AFTER the
           latch, so what it names is the cartridge that is actually about to
           load. */
        for (i = 0; i < M16C_LOADING_FRAMES; i++) {
            m16c_draw_loading();
            m16c_flip();
        }

        /* =================================================================
         * step 451 -- ***** THE M13C SETUP, IN THE M13C ORDER. *****
         * =================================================================
         *
         * Every step below is M13C's, in M13C's position, with exactly ONE
         * class of change: the three fresh-process probes are compared against
         * `first ? 0 : <the relaunch mask>` instead of against 0.
         *
         * ***** THE PROBES ARE NOT WEAKENED. THEY ARE ASKED A SECOND,
         * DIFFERENT QUESTION. ***** gba_session.h:132-155 derives this in full.
         * m2_probe_pre(), m3_bios_probe_pre() and m4_rom_probe_pre() assert
         * VIRGIN .bss, which is the right assertion for the first launch in a
         * process and is KEPT EXACTLY AS IT IS on session 1. They are
         * necessarily FALSE on session 2 -- the BIOS is still loaded, the screen
         * is still installed, the noise tables are still full -- so on a
         * relaunch the controller requires EXACT EQUALITY against a named mask.
         * Any OTHER bit still fails. In particular M4_PRE_NO_BUFFERS and
         * M4_PRE_BUF0_NULL are absent from GBA_SESS_PRE4_RELAUNCH, so an arena
         * that failed to rewind is still caught here.
         * ================================================================= */
        ext->step = 451;
        klog("LUAport M16C: loading the chosen cartridge\n");

        /* (a) THE M2 PRE-CHECK MUST RUN BEFORE m2_set_screen(). */
        expect2 = 0u;
        if (screen_on) expect2 |= (unsigned)M2_PRE_SCREEN_NULL;
        if (sound_on)  expect2 |= (unsigned)(M2_PRE_NOISE15_ZERO |
                                             M2_PRE_NOISE7_ZERO);
        m = m2_probe_pre();
        if (m != expect2) {
            printf("M16C: ***** M2 PRE-CHECK 0x%08X, EXPECTED 0x%08X *****\n",
                   m, expect2);
            (void)report("M2 PRE-CHECK", m, m2pre_bit);
            ext->dbg[4] = (u64)m;
            /* NOTHING HAS EVER INSTALLED THE SCREEN OR THE NOISE TABLES, so a
               dirty M2 probe describes the HOST PROCESS, not our boundary. */
            if (!screen_on && !sound_on) {
                if (!m16c_session_fatal(ext, M16C_ASSERT_FAILED,
                        M16C_H_NOTREADY,
                        "THE EMULATOR STATE WAS NOT CLEAN AT STARTUP.",
                        "RELAUNCH THE HOST GAME AND TRY AGAIN.", 0,
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }
            /* ***** ON A RELAUNCH THIS IS BOUNDARY EVIDENCE, NOT CARTRIDGE
               EVIDENCE. ***** An unaccounted bit here means the PREVIOUS
               session's teardown left something the probe can see, and the next
               game would inherit it. FAIL CLOSED: end the payload. */
            m16c_runtime_fatal(ext, M16C_PRE_UNEXPECTED, M16C_H_BOUNDARY,
                               "THE EMULATOR DID NOT RETURN TO A KNOWN STATE",
                               "AFTER THE LAST GAME. LUAPORT STOPPED RATHER",
                               "THAN START ANOTHER ONE ON TOP OF IT.");
            return;
        }
        printf("M16C: M2 PRE-CHECK 0x%08X == expected 0x%08X (%s)\n",
               m, expect2, expect2 ? "RELAUNCH" : "FRESH PROCESS");

        /* ***** THE SCREEN BUFFER IS INSTALLED ONCE. ***** It is
           cartridge-independent and belongs to the payload, which is exactly
           what M2_PRE_SCREEN_NULL accounts for in GBA_SESS_PRE2_RELAUNCH.
           Re-installing it per game would be pointless churn; releasing it would
           leave the picker with nothing to draw into. */
        if (!screen_on) {
            m2_set_screen(m16c_gba_screen);
            screen_on = 1;
            printf("M16C: screen buffer %u bytes installed\n",
                   (unsigned)sizeof(m16c_gba_screen));
        }

        /* (b) ***** THE ARENA MARK. THE BINDING CONSTRAINT OF THE WHOLE
               MILESTONE. ***** Taken IMMEDIATELY BEFORE the greedy allocator
               and rewound to at the boundary. malloc() is a bump allocator whose
               free() is a no-op, so this mark/rewind pair is the only honest
               reclamation available -- and it is sufficient, because gpSP's ROM
               buffers are allocated together, used together and released
               together (runtime/shim.h:54-68). */
        mark       = arena_mark();
        mark_taken = 1;
        arena_before_buf = arena_used();
        printf("M16C: arena mark %u (used %u, payload baseline %u)\n",
               (unsigned)mark, (unsigned)arena_before_buf,
               (unsigned)m16c_arena_baseline);

        /* ***** EVERY SESSION MUST MARK AT THE SAME PLACE. ***** The mark sits
           before the ROM buffers and the buffer allocator is
           cartridge-independent, so a bigger cartridge cannot legitimately move
           it. A drift means the previous boundary did not fully rewind, and the
           arena would creep until a later session could not fund two buffers. */
        if (!m16c_mark_first_taken) {
            m16c_mark_first       = mark;
            m16c_mark_first_taken = 1;
        } else if (mark != m16c_mark_first) {
            printf("M16C: ***** THE ARENA MARK DRIFTED: SESSION 1 %u, NOW %u "
                   "*****\n", (unsigned)m16c_mark_first, (unsigned)mark);
            ext->dbg[4] = mark;
            m16c_runtime_fatal(ext, M16C_ARENA_LEAK, M16C_H_BOUNDARY,
                               "MEMORY IS CREEPING BETWEEN GAMES. A LATER GAME",
                               "WOULD FAIL TO LOAD, SO LUAPORT STOPPED NOW",
                               "WHILE THE LAST SAVE IS STILL SAFE.");
            return;
        }

        /* THE RFILE BASELINE FOR THIS SESSION. Measured rather than assumed to
           be zero: the boundary compares against THIS number, because a
           demand-paged cartridge holds a handle during play and a fully
           resident one does not. */
        rfile_before = gba_rfile_inuse();
        printf("M16C: RFILE in use before the load: %u of %u\n",
               rfile_before, gba_rfile_slots());

        /* (c) ***** THE ROM BUFFERS. THE BINDING GATE IS >= 2. ***** The build
               uses -DROM_BUFFER_SIZE=2, and load_gamepak_raw computes ldblks
               from the count and returns -1 when it is zero
               (gba_memory.c:2763-2768). A count of 1 would silently halve the
               demand-paging window. */
        buffers = m4_rom_buffer_init();
        arena_after_buf = arena_used();
        printf("M16C: gamepak buffers %u x %u bytes, arena used %u of %u\n",
               buffers, m4_rom_read(M4_RD_BLOCKSIZE),
               (unsigned)arena_after_buf, (unsigned)ARENA_SIZE);

        if (buffers < 2u) {
            printf("M16C: ***** ONLY %u GAMEPAK BUFFER(S) -- REQUIRE 2 *****\n",
                   buffers);
            ext->dbg[7] = (u64)buffers;
            if (!m16c_session_fatal(ext, M16C_NO_BUFFERS, M16C_H_NOTREADY,
                    "NOT ENOUGH MEMORY FOR THE CARTRIDGE WINDOW.",
                    "THE PREVIOUS GAME MAY NOT HAVE RELEASED IT.",
                    "RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        /* ***** THE BUFFERS ARE THE SAME BYTES EVERY SESSION, AND THAT IS WHY
           NO ASSERTION IN THIS FILE USES A POINTER. ***** At this instant the
           arena has just handed back the identical addresses the previous
           session used, and those addresses still hold the PREVIOUS cartridge's
           bytes. Isolation is proven by what load_gamepak_raw() writes over
           them -- and by the CONTENT-derived identity below -- never by where
           they are. This is M16-1's finding and it is why the ledger records
           hashes rather than addresses. */

        /* (d) init_sound() ONCE, BEFORE reset_gba(). init_sound() ENDS by
               calling reset_sound() (sound.c:609), so calling it first and
               letting reset_gba() run its own reset_sound() afterwards is
               idempotent. Its output -- the two noise tables and the GBC tick
               step -- is CARTRIDGE-INDEPENDENT, which is why
               M2_PRE_NOISE15_ZERO and M2_PRE_NOISE7_ZERO are expected survivors
               on a relaunch and why it is not called again. */
        if (!sound_on) {
            m2_call_init_sound();
            sound_on = 1;
        }

        /* (e) RESET #1. Per session: init_memory() re-defaults backup_type,
               eeprom_size, flash_bank_num, flash_mode, eeprom_mode,
               eeprom_address and eeprom_counter, which is exactly the
               per-cartridge configuration a NEW cartridge must not inherit. */
        m2_call_reset_gba();

        /* (f) THE BIOS, AFTER RESET #1. M3's gates, unchanged in content and
               loaded ONCE PER PAYLOAD -- it is the same 16 KB image for every
               cartridge and is not part of any cartridge's identity. */
        expect3 = bios_on ? (unsigned)M3_PRE_BIOS_ZERO : 0u;
        m = m3_bios_probe_pre();
        if (m != expect3) {
            printf("M16C: ***** BIOS PRE-CHECK 0x%08X, EXPECTED 0x%08X *****\n",
                   m, expect3);
            (void)report("BIOS PRE-CHECK", m, biospre_bit);
            ext->dbg[4] = (u64)m;
            /* NO BIOS HAS EVER BEEN LOADED, so a non-zero bios_rom is the host
               process's, not a survivor of our own teardown. */
            if (!bios_on) {
                if (!m16c_session_fatal(ext, M16C_ASSERT_FAILED,
                        M16C_H_NOTREADY,
                        "THE BIOS REGION WAS NOT CLEAN BEFORE LOADING.",
                        "RELAUNCH THE HOST GAME AND TRY AGAIN.", 0,
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }
            m16c_runtime_fatal(ext, M16C_PRE_UNEXPECTED, M16C_H_BOUNDARY,
                               "THE BIOS DID NOT RETURN TO A KNOWN STATE AFTER",
                               "THE LAST GAME. LUAPORT STOPPED RATHER THAN",
                               "START ANOTHER ONE ON TOP OF IT.");
            return;
        }

        if (!bios_on) {
            for (bios_idx = 0; bios_idx < m3_bios_candidate_count();
                 bios_idx++) {
                unsigned sz = 0;
                const char *nm = m3_bios_candidate_name(bios_idx);
                if (m3_bios_query(bios_idx, &sz)) {
                    printf("M16C: BIOS candidate %u '%s' OPENED, %u bytes\n",
                           bios_idx, nm ? nm : "(null)", sz);
                    bios_found = 1;
                    bios_size  = sz;
                    break;
                }
                printf("M16C: BIOS candidate %u '%s' not present\n",
                       bios_idx, nm ? nm : "(null)");
            }
            /* ***** THE BIOS IS RUNTIME-FATAL, NOT SESSION-FATAL. ***** It is
               payload-scoped: no cartridge can run without it and no later
               picker choice could succeed where this one failed. Returning to
               the picker would offer the operator a list of games none of which
               can start. */
            if (!bios_found) {
                m16c_runtime_fatal(ext, M16C_BIOS_FAILED, M16C_H_BIOS,
                                   "NO GBA BIOS IMAGE IS INSTALLED.",
                                   "UPLOAD ONE USING:  MAKE M3-UPLOAD-BIOS", 0);
                return;
            }
            /* load_bios() VALIDATES NOTHING -- it returns 0 for a 1-byte file
               (gba_bios.h:46-64). Measuring first is the only way to know the
               0x4000 bytes it blindly reads were all really there. */
            if (bios_size != M3_BIOS_SIZE) {
                printf("M16C: BIOS SIZE WRONG -- got %u, require exactly %u\n",
                       bios_size, (unsigned)M3_BIOS_SIZE);
                ext->dbg[4] = (u64)bios_size;
                m16c_runtime_fatal(ext, M16C_BIOS_FAILED, M16C_H_BIOS,
                                   "THE INSTALLED BIOS IS THE WRONG SIZE.",
                                   "IT MUST BE EXACTLY 16384 BYTES.",
                                   "UPLOAD A GOOD ONE:  MAKE M3-UPLOAD-BIOS");
                return;
            }
            rc = m3_bios_load(bios_idx);
            printf("M16C: load_bios() returned %d\n", rc);
            if (rc != 0) {
                m16c_runtime_fatal(ext, M16C_BIOS_FAILED, M16C_H_BIOS,
                                   "THE INSTALLED BIOS DID NOT LOAD.",
                                   "UPLOAD A GOOD ONE:  MAKE M3-UPLOAD-BIOS", 0);
                return;
            }
            /* ***** SET ONLY NOW. ***** Every failure above is runtime-fatal and
               returns, so reaching this line means bios_rom really does hold a
               validated 16 KB image -- which is precisely what M3_PRE_BIOS_ZERO
               is expected to report from here on. */
            bios_on = 1;
        }

        /* ***** THE BIOS GETS A STRONGER CHECK ON A RELAUNCH, NOT A WEAKER
           ONE. ***** m3_bios_probe_content() must be 0 on EVERY session, which
           proves the image is still STRUCTURALLY VALID rather than merely
           non-zero. Session 1 proves "it was clean, then we loaded it"; every
           later session proves "it is still the thing we loaded".

           The MAP gate carries GBA_SESS_MAP3_RELAUNCH for the reason M16-0
           established ON HARDWARE: m3_bios_probe_map() and m4_rom_probe_pre()
           read the IDENTICAL slot, memory_map_read[MAP_IDX(0x8000000)], and once
           session 1 maps a cartridge that window stays mapped -- nothing in
           init_memory() or memory_term() ever unmaps it. The comparison stays
           EXACT: M3_MAP_BIOS0 and M3_MAP_WINDOW are NOT in the mask and either
           one still fails, on every session. */
        expect_map    = rom_ever ? (unsigned)M3_MAP_ROM_PRESENT : 0u;
        biosmap_raw   = m3_bios_probe_map();
        m             = m3_bios_probe_content();
        if (m || biosmap_raw != expect_map) {
            printf("M16C: ***** BIOS CONTENT 0x%08X, MAP 0x%08X, EXPECTED MAP "
                   "0x%08X *****\n", m, biosmap_raw, expect_map);
            (void)report("BIOS CONTENT", m, bioscontent_bit);
            (void)report("BIOS MAPPING", biosmap_raw, biosmap_bit);
            ext->dbg[4] = (u64)(m ? m : biosmap_raw);
            m16c_runtime_fatal(ext, M16C_BIOS_FAILED, M16C_H_BIOS,
                               "THE INSTALLED BIOS DID NOT VALIDATE.",
                               "UPLOAD A GOOD ONE:  MAKE M3-UPLOAD-BIOS", 0);
            return;
        }
        printf("M16C: BIOS content OK, map 0x%08X == expected 0x%08X (%s)\n",
               biosmap_raw, expect_map,
               rom_ever ? "CARTRIDGE WINDOW PRESERVED FROM AN EARLIER SESSION"
                        : "NO CARTRIDGE HAS BEEN LOADED YET");

        /* (g) ***** THE CHOSEN CARTRIDGE -- MEASURED AGAIN, BY NAME, RIGHT
               BEFORE USE. ***** The picker measured it, but that was many frames
               and one operator decision ago -- and in a looping launcher it may
               have been a whole game ago. Re-measuring costs one open and
               removes any window in which the file changed underneath the
               launcher. */
        if (!m4_rom_query_named(m16c_chosen, &rom_size)) {
            printf("M16C: THE CHOSEN FILE WOULD NOT OPEN: %s\n", m16c_chosen);
            if (!m16c_session_fatal(ext, M16C_ROM_BAD_SOURCE, M16C_H_ROM_OPEN,
                    "THE FILE COULD NOT BE OPENED.",
                    "IT MAY HAVE BEEN MOVED OR DELETED SINCE THE LIST WAS",
                    "BUILT. RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }
        printf("M16C: re-measured %u bytes (the picker saw %u)\n",
               rom_size, m16c_items[chosen_idx].size);

        /* ***** AND THE TWO MEASUREMENTS MUST AGREE. ***** Re-measuring and then
           merely PRINTING the comparison would be a check in appearance only.
           If the size moved, the validation the operator saw no longer describes
           the bytes about to be loaded, and the save identity would be latched
           from content that was never gated. So this REFUSES. */
        if (rom_size != m16c_items[chosen_idx].size) {
            printf("M16C: ***** THE CHOSEN FILE CHANGED SIZE: %u -> %u *****\n",
                   m16c_items[chosen_idx].size, rom_size);
            if (!m16c_session_fatal(ext, M16C_ROM_BAD_SOURCE, M16C_H_ROM_BAD,
                    "THE FILE CHANGED SINCE THE LIST WAS BUILT.",
                    "IT IS NOT THE CARTRIDGE THAT WAS CHECKED, SO IT WAS",
                    "NOT LOADED. RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        if ((m = report("ROM SOURCE", m4_rom_probe_source(rom_size), srcbit))) {
            ext->dbg[4] = (u64)m;
            if (!m16c_session_fatal(ext, M16C_ROM_BAD_SOURCE, M16C_H_ROM_BAD,
                    "THE FILE IS NOT AN ACCEPTABLE CARTRIDGE IMAGE.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        expect4 = rom_ever ? (unsigned)(M4_PRE_SIZE_SET | M4_PRE_ROM_MAPPED) : 0u;
        m = m4_rom_probe_pre();
        if (m != expect4) {
            printf("M16C: ***** ROM PRE-CHECK 0x%08X, EXPECTED 0x%08X *****\n",
                   m, expect4);
            (void)report("ROM PRE-CHECK", m, prebit);
            ext->dbg[4] = (u64)m;
            /* NO CARTRIDGE HAS EVER REACHED gpSP, so a set gamepak_size or a
               mapped window cannot be something WE left behind. */
            if (!rom_ever) {
                if (!m16c_session_fatal(ext, M16C_ASSERT_FAILED,
                        M16C_H_NOTREADY,
                        "THE EMULATOR STATE WAS NOT CLEAN BEFORE LOADING.",
                        "RELAUNCH THE HOST GAME AND TRY AGAIN.", 0,
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }
            m16c_runtime_fatal(ext, M16C_PRE_UNEXPECTED, M16C_H_BOUNDARY,
                               "THE CARTRIDGE WINDOW DID NOT RETURN TO A KNOWN",
                               "STATE AFTER THE LAST GAME. LUAPORT STOPPED",
                               "RATHER THAN START ANOTHER ONE ON TOP OF IT.");
            return;
        }
        printf("M16C: ROM PRE-CHECK 0x%08X == expected 0x%08X (%s)\n",
               m, expect4, rom_ever ? "RELAUNCH" : "NO CARTRIDGE LOADED YET");

        /* (h) ***** THE BACKUP ARRAY, 0xFF, IMMEDIATELY BEFORE THE LOAD. *****
         *
         * ***** IN A LOOPING LAUNCHER THIS STOPS BEING A FORMALITY AND BECOMES
         * THE SAVE-ISOLATION GUARANTEE. ***** init_memory() PROVABLY NEVER
         * WRITES gamepak_backup[] (the only writers in gpsp/ are gba_memory.c:
         * 581, :587, :1195, :1206, :1220, :1239, :1247 -- all reachable only
         * from EXECUTING code -- plus the blank-normalise at :2906 which
         * load_gamepak() runs). Without this fill, game B would compute its
         * restore baseline over game A's save bytes and dirty detection would
         * become meaningless; worse, an A -> B -> A sequence could restore A's
         * own save over bytes B had left behind.
         *
         * ***** IT BELONGS TO SESSION START, NOT SESSION END. ***** That is why
         * gba_session_end() deliberately excludes it (gba_session.c:152-155):
         * putting it at the end would open a window in which a stale cartridge's
         * save bytes were live under a new cartridge. Here there is no such
         * window -- the fill sits immediately before the load.
         *
         * IT IS UPSTREAM PARITY, not an invention: gpsp/libretro/libretro.c:1278
         * memsets gamepak_backup to 0xFF immediately before its own load_gamepak
         * call. It is a BUFFER, not a file -- no save is opened or written. */
        m4_rom_backup_init();

        /* (h2) ***** THE PREVIOUS CARTRIDGE'S MAP TAIL, CLEARED IMMEDIATELY
         * BEFORE THE LOAD. ***** This is the M16-2 correction. The band and its
         * derivation are documented at M16C_MAPTAIL_LO above.
         *
         * ***** IT CANNOT ERASE THIS SESSION'S OWNERSHIP. ***** Three
         * independent facts each suffice, at this exact instruction:
         *
         *   1. load_gamepak() has not been called this session.
         *      m4_rom_load_named() is its only entry point and is the very next
         *      statement, so neither map_null() nor the load-path
         *      map_rom_entry() has run yet.
         *   2. No emulated instruction has executed this session. The fault-in
         *      (gba_memory.c:2350) and evict (gba_memory.c:2313) writers are
         *      reachable ONLY from the cpu.cc page-fault path, and execution
         *      does not begin until step 455+.
         *   3. No page claims residency. m4_rom_buffer_init() ->
         *      init_gamepak_buffer() set every gamepak_blk_queue[].phy_rom to
         *      -1 (gba_memory.c:2373-2377), so the resident set is EMPTY by
         *      construction.
         *
         * Therefore every non-NULL slot in this band right now NECESSARILY
         * belongs to a previous session whose buffers memory_term()
         * (gba_session.c:113) already freed. There is nothing current to lose.
         *
         * ***** IT MUST NOT RUN AFTER THE LOAD. ***** Two concrete failures,
         * both real: a 32 MB cartridge (B = 1024) LEGITIMATELY owns 6656..7167,
         * and clearing a resident page's slot would make it refault and leave
         * two queue entries naming the same phy_rom, breaking the m10map
         * bijection outright; and small non-power-of-two ROMs write into the
         * band AT LOAD TIME (B = 3 reaches 6656, B = 511 reaches 7165). This is
         * a SESSION-START PRECONDITION, not an invariant repair, and it is why
         * a periodic or post-load scrub would be actively wrong.
         *
         * ***** IT BELONGS HERE AND NOT AT TEARDOWN. ***** Exactly the argument
         * the 0xFF backup fill above makes for itself: a session-start clear is
         * unconditional and self-defending -- correct after a clean boundary,
         * after a failed load, and after a session that aborted at any
         * session_fatal site -- and no path back into the loop can skip it.
         * Placed here rather than beside m4_rom_buffer_init() so that nothing
         * can re-pollute the band between the clear and the load, and so that
         * it provably sits AFTER m4_rom_probe_pre() has read slot 4096. */
        m16c_maptail_cleared = m16c_clear_map_tail();
        printf("M16C: cartridge map tail %u..%u -- %u stale slot(s) cleared\n",
               M16C_MAPTAIL_LO, M16C_MAPTAIL_HI, m16c_maptail_cleared);

        /* (i) ***** THE LOAD. THE OPERATOR'S OWN PATH REACHES gpSP. ***** */
        rc = m4_rom_load_named(m16c_chosen);
        printf("M16C: m4_rom_load_named() returned %d\n", rc);
        printf("M16C: gpSP WAS ASKED TO OPEN: %s\n", m4_rom_name());

        if (rc != 0) {
            /* ***** A FAILED LOAD IS SESSION-FATAL, AND THE BOUNDARY IS NOT
               OPTIONAL HERE. ***** M16-1's finding: gpSP leaves the PREVIOUS
               mapping intact when a load fails. Returning to the picker without
               memory_term() would leave the last cartridge's bytes mapped, and
               the next game would run them under its own identity. */
            klog("LUAport M16C: the cartridge did not load. The session is being "
                 "torn down IN FULL before the picker returns -- a failed load "
                 "leaves the previous mapping intact, and it must not survive "
                 "into the next game\n");
            if (!m16c_session_fatal(ext, M16C_ROM_LOAD_FAIL, M16C_H_ROM_BAD,
                    "THE CARTRIDGE COULD NOT BE LOADED.",
                    "THE FILE OPENED BUT THE EMULATOR REFUSED IT.",
                    "RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        /* ***** THE CARTRIDGE WINDOW IS NOW A PERMANENT SURVIVOR. ***** From this
           point on NOTHING in the process ever unmaps 0x08000000 again:
           memory_term() closes the file and releases the buffers but leaves
           gamepak_size and the window alone, and init_memory()'s
           map_region/map_null calls stop at 0x08000000 and resume at 0xE000000,
           never covering the gamepak range (gba_session.h:199-224). That is why
           M4_PRE_SIZE_SET, M4_PRE_ROM_MAPPED and M3_MAP_ROM_PRESENT are EXPECTED
           from here on.

           ***** IT IS SET ONLY ON SUCCESS, AND THAT IS CORRECT RATHER THAN
           MERELY CONSERVATIVE -- A FAILED LOAD PROVABLY LEAVES BOTH THE WINDOW
           AND gamepak_size UNTOUCHED. ***** Verified in gpSP rather than assumed.
           load_gamepak_raw() does not reach its map_null(read, 0x8000000,
           0xD000000) until gba_memory.c:2771, and every failure return sits
           ABOVE that line:

             open failed            gamepak_file_large NULL -- nothing is written
             bad file size          return -1 at :2696, which is BEFORE
                                    gamepak_size is assigned at :2704
             ldblks == 0            return -1 at :2767, whose own comment is
                                    "leave the existing mapping alone" -- and it
                                    is unreachable from here anyway, because it
                                    needs gamepak_buffer_count == 0 and step (c)
                                    above already refused anything below 2

           So after a refused load the three bits read exactly as they did before
           it, which is precisely what this flag already records. A session that
           follows a failed load therefore inherits the right expectation from the
           last SUCCESSFUL load -- the previous cartridge's mapping, which is
           M16-1's finding and the reason the boundary is not optional above. */
        rom_ever = 1;

        /* (j) ***** THE IDENTITY LATCH, IMMEDIATELY AFTER THE LOAD. *****
         *
         * RIGHT HERE, AND NOWHERE ELSE, for the reason gba_save.h:265-311
         * derives: with -DROM_BUFFER_SIZE=2 a cartridge over 2 MB is DEMAND
         * PAGED, and evict_gamepak_page() can reclaim gamepak_buffers[0] -- the
         * first buffer is NOT pinned. An identity computed later would be
         * CACHE-DEPENDENT and the same cartridge would produce a different .sav
         * filename on different sessions, silently.
         *
         * ***** THE IDENTITY IS <GAMECODE>_<ROMHASH8> AND IS DERIVED FROM THE
         * CARTRIDGE BYTES. ***** It does not contain, depend on or vary with the
         * filename. THAT IS WHAT MAKES A -> B -> A CORRECT: relaunching A after
         * B recomputes the identical identity and therefore opens the identical
         * .sav, and M16-1 proved exactly that on hardware (session 1 identity
         * 9299BB73 == session 3 identity 9299BB73, with B's 27617F81 in
         * between).
         *
         * ***** gba_save_latch() CAN NOW BE REACHED MORE THAN ONCE PER PROCESS,
         * AND THAT IS WHY THE UNLATCH AT THE BOUNDARY IS LOAD-BEARING. *****
         * It REFUSES to re-latch while a previous identity is still held
         * (gba_session.h:58-61), so without gba_save_unlatch() session 2 would
         * keep session 1's identity and commit game B's memory into game A's
         * save. m16c_boundary() checks GBA_SESS_R_LATCHED on every boundary for
         * precisely this reason, and treats it as RUNTIME-fatal. */
        gba_save_latch();

        if (!gba_save_latched()) {
            if (!m16c_session_fatal(ext, M16C_NO_LATCH, M16C_H_NOTREADY,
                    "THE SAVE IDENTITY COULD NOT BE DERIVED.",
                    "WITHOUT IT NO SAVE COULD BE FOUND OR WRITTEN, SO THE",
                    "GAME WAS NOT STARTED. RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        gba_save_id_copy(save_id, (unsigned int)sizeof save_id);
        m4_rom_title_copy(title, (unsigned int)sizeof title);
        m4_rom_code_copy(code, (unsigned int)sizeof code);

        /* THE PRE-RESET REFERENCE VALUES. Captured HERE -- at the latch, before
           reset #2 -- because "unchanged" is only a meaningful claim against a
           value recorded at a known instant. */
        romhash_before      = gba_save_rom_hash();
        gamepak_size_before = m4_rom_read(M4_RD_SIZE);

        /* ***** THE SESSION LEDGER TAKES ITS IDENTITY HERE, AND IT IS
           CONTENT-DERIVED. ***** Never a pointer: the arena hands back identical
           addresses every session, so an address proves nothing. */
        {
            unsigned k;
            for (k = 0; k + 1u < (unsigned)sizeof m16c_rec.save_id &&
                        save_id[k]; k++)
                m16c_rec.save_id[k] = save_id[k];
            m16c_rec.save_id[k] = '\0';
            for (k = 0; k + 1u < (unsigned)sizeof m16c_rec.code && code[k]; k++)
                m16c_rec.code[k] = code[k];
            m16c_rec.code[k] = '\0';
        }
        m16c_rec.romhash = romhash_before;

        /* (k) THE PROBES. */
        m  = report("ROM METADATA", m4_rom_probe_meta(rom_size), metabit);
        m |= report("ROM CONTENT",  m4_rom_probe_content(rom_size), contentbit);

        mapmask_before = m4_rom_probe_map();
        m |= report("ROM MAPPING (before reset 2)", mapmask_before, mapbit);

        printf("M16C: TITLE      '%s'\n", title);
        printf("M16C: GAME CODE  '%s'\n", code);
        printf("M16C: FILE SIZE  %u bytes\n", rom_size);
        printf("M16C: ROM FNV    0x%08X\n", m4_rom_fnv1a(rom_size));
        printf("M16C: SAVE ID    '%s'\n", save_id);
        printf("M16C: SAVE HASH  0x%08X over %u bytes\n",
               gba_save_rom_hash(), gba_save_read(GBA_SAVE_RD_HASHED));

        ext->dbg[0] = (u64)rom_size;
        ext->dbg[1] = (u64)m4_rom_fnv1a(rom_size);
        ext->dbg[2] = (u64)gba_save_rom_hash();
        ext->dbg[4] = (u64)mapmask_before;

        if (m) {
            if (!m16c_session_fatal(ext, M16C_ASSERT_FAILED, M16C_H_ROM_BAD,
                    "THE CARTRIDGE DID NOT PASS ITS CHECKS AFTER LOADING.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        /* ---- RESET #2 AND THE SURVIVAL ASSERTIONS ----------------------
         *
         * ***** THIS IS NOT A FORMALITY AND IT IS NOT OPTIONAL. ***** reset_gba()
         * calls init_memory(), which REBUILDS memory_map_read from scratch, and
         * execute_arm() before reset_gba()/init_memory() SPINS FOREVER on
         * hardware (gba_exec.h:125-132). Every check below is a hard
         * session-fatal, so control cannot reach the first emulated instruction
         * with any of them merely assumed. */
        m2_call_reset_gba();

        mapmask_after = m4_rom_probe_map();
        ext->dbg[6] = (u64)mapmask_after;

        if (report("ROM MAPPING (after reset 2)", mapmask_after, mapbit)) {
            if (!m16c_session_fatal(ext, M16C_MAP_LOST, M16C_H_NOTREADY,
                    "THE CARTRIDGE WAS UNMAPPED BY THE EMULATOR RESET.",
                    "STARTING THE GAME NOW WOULD RUN AGAINST NO MEMORY.",
                    "RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        /* ***** THIS PROBE CANNOT BE USED RAW AFTER A ROM IS LOADED. *****
         * m3_bios_probe_map() sets M3_MAP_ROM_PRESENT whenever the 0x08000000
         * window is non-NULL (gba_bios.c:314-315), because M3 was FORBIDDEN to
         * load a ROM. Here a ROM is loaded ON PURPOSE, so that bit is the
         * CORRECT state. It is split out and its sense INVERTED rather than
         * merely ignored: after a load it MUST be set, and its absence is a
         * failure caught below. No coverage is lost -- the other two bits still
         * assert memory_map_read[0] == bios_rom and all 512 window entries. */
        biosmap_raw   = m3_bios_probe_map();
        biosmap_after = biosmap_raw & ~(unsigned)M3_MAP_ROM_PRESENT;

        if (report("BIOS MAPPING (after reset 2)", biosmap_after, biosmap_bit)) {
            if (!m16c_session_fatal(ext, M16C_MAP_LOST, M16C_H_NOTREADY,
                    "THE BIOS WAS UNMAPPED BY THE EMULATOR RESET.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }
        if (!(biosmap_raw & M3_MAP_ROM_PRESENT)) {
            if (!m16c_session_fatal(ext, M16C_MAP_LOST, M16C_H_NOTREADY,
                    "THE CARTRIDGE WINDOW WENT EMPTY ACROSS THE RESET.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        buffers_after = m4_rom_read(M4_RD_BUFFER_COUNT);
        printf("M16C: gamepak buffers before reset %u, after reset %u\n",
               buffers, buffers_after);
        if (buffers_after < 2u) {
            ext->dbg[7] = (u64)buffers_after;
            if (!m16c_session_fatal(ext, M16C_BUFFERS_LOST, M16C_H_NOTREADY,
                    "THE CARTRIDGE WINDOW SHRANK ACROSS THE RESET.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        metamask_after    = m4_rom_probe_meta(rom_size);
        contentmask_after = m4_rom_probe_content(rom_size);
        m  = report("ROM METADATA (after reset 2)", metamask_after, metabit);
        m |= report("ROM CONTENT (after reset 2)", contentmask_after, contentbit);

        gamepak_size_after = m4_rom_read(M4_RD_SIZE);
        if (gamepak_size_after != gamepak_size_before) {
            printf("M16C: ***** gamepak_size CHANGED ACROSS RESET: %u -> %u "
                   "*****\n", gamepak_size_before, gamepak_size_after);
            m |= 1u;
        }

        /* ***** THE PATH gpSP STILL BELIEVES IT OPENED. ***** m4_rom_name()
           echoes back the name the loader was given, so comparing it to the
           latched path proves the selection the OPERATOR made is still the
           cartridge in memory -- not a neighbour, not an index, and -- in a
           looping launcher -- NOT THE PREVIOUS GAME. */
        if (strcmp(m4_rom_name(), m16c_chosen) != 0) {
            printf("M16C: ***** THE LOADED NAME NO LONGER MATCHES THE CHOSEN "
                   "PATH\n");
            printf("M16C:   chosen '%s'\n", m16c_chosen);
            printf("M16C:   loaded '%s'\n", m4_rom_name());
            m |= 1u;
        }

        if (m) {
            if (!m16c_session_fatal(ext, M16C_ASSERT_FAILED, M16C_H_ROM_BAD,
                    "THE CARTRIDGE STATE IS NOT COHERENT AFTER THE RESET.",
                    "RETURNING TO PICKER.", 0,
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        /* ***** THE SAVE IDENTITY MUST NOT MOVE ACROSS THE RESET. *****
         * If it can, the SAME cartridge maps to a DIFFERENT save depending on
         * when the id happened to be read, and the launcher would write a save
         * under one name and look for it under another. Compared against values
         * captured at the latch, string AND hash, so a partial drift cannot hide
         * behind a matching hash. */
        gba_save_id_copy(save_id_after, (unsigned int)sizeof save_id_after);
        romhash_after = gba_save_rom_hash();

        id_survived = gba_save_latched() &&
                      (strcmp(save_id_after, save_id) == 0) &&
                      (romhash_after == romhash_before);

        printf("M16C: SAVE ID before '%s' after '%s'\n", save_id, save_id_after);
        printf("M16C: SAVE HASH before 0x%08X after 0x%08X\n",
               romhash_before, romhash_after);

        if (!id_survived) {
            if (!m16c_session_fatal(ext, M16C_ID_DRIFT, M16C_H_NOTREADY,
                    "THE SAVE IDENTITY CHANGED DURING STARTUP.",
                    "THIS GAME WOULD NOT FIND ITS OWN SAVE, SO IT WAS NOT",
                    "STARTED. RETURNING TO PICKER.",
                    mark, mark_taken, rfile_before))
                return;
            continue;
        }

        klog("LUAport M16C: the cartridge is loaded, mapped and its identity is "
             "stable across the reset\n");
        printf("M16C: SESSION %u IDENTITY -- CODE '%s' ID '%s' ROMHASH "
               "0x%08X, %u bytes. CONTENT-DERIVED, NOT ADDRESS-DERIVED\n",
               m16c_sessions_started, code, save_id, romhash_before, rom_size);

        /* =================================================================
         * step 452 -- THE INFORMATION SCREEN, WITH REAL METADATA
         * =================================================================
         *
         * M13C's step 371, unchanged in content. ***** THIS IS THE SCREEN THAT
         * HOLDS EVERY FIELD THE PICKER CANNOT KNOW. ***** The picker states
         * only SIZE and STATUS, because those are all it can measure without a
         * resident cartridge; each row below needs the loaded image and so can
         * only exist here:
         *
         *   TITLE      m4_rom_title_copy()  -- offset 0xA0 of the resident image
         *   CODE       m4_rom_code_copy()   -- offset 0xAC
         *   SIZE       the re-measured true file length
         *   SAVE DATA  gba_restorefile_run(VALIDATE)
         *
         * ***** THE SAVE PROBE IS THE VALIDATE MODE OF THE PROVEN VALIDATOR, NOT
         * A NEW ONE. ***** gba_restorefile.h:109-126: VALIDATE reads and checks
         * EVERYTHING -- both files, every header field, the ROM identity, the
         * class compatibility, the exact length and the full payload hash -- and
         * then DELIBERATELY DOES NOT APPLY. It opens both files "rb" against a
         * read-only mount and writes nothing anywhere. So the panel's FOUND /
         * NONE / REJECTED answer is the SAME ladder that will run for real at the
         * restore, and the two can never disagree.
         *
         * ***** IN A LOOPING LAUNCHER IT IS ALSO THE FIRST PROOF THE IDENTITY
         * MOVED WITH THE CARTRIDGE. ***** The .sav name printed here is derived
         * from THIS cartridge's content, so an A -> B -> A sequence shows A's
         * name, then B's, then A's again -- and it is read from gba_save_name_sav()
         * rather than rebuilt, so the panel cannot disagree with the writer.
         *
         * ***** THE SAVE CLASS AND CONFIDENCE ARE A DIAGNOSTIC NOW, NOT A ROW.
         * ***** gba_save.h:94-98: BACKUP_UNKN only collapses to a real class on
         * the first backup-region access, which needs the game to be RUNNING.
         * Before launch the pair therefore reads "UNKNOWN / UNKNOWN" on very
         * nearly every cartridge -- a CORRECT measurement that an operator
         * cannot distinguish from a broken field, sitting directly above the
         * row that answers the question they actually have. It is not a
         * measurement worth a line of the panel at the one moment it can never
         * say anything, so it MOVED TO THE SERIAL LOG rather than being
         * deleted: printed once, just below, beside the probe result.
         *
         * ***** NOTHING ABOUT SAVE DETECTION CHANGED. ***** gba_save_class(),
         * gba_save_confidence(), the class collapse, the restore validation,
         * the restore ordering and the commit path are all untouched -- this is
         * a PRESENTATION change and the values are still read from the same two
         * calls. The capture screen at SESSION COMPLETE still prints the class
         * on screen, where the game HAS run and the value is real. */
        ext->step = 452;

        (void)gba_save_name_sav(sav_name, (unsigned int)sizeof sav_name);
        if (sav_name[0])
            printf("M16C: THIS SESSION'S SAVE FILE IS %s\n", sav_name);

        probe_rc = gba_restorefile_run(GBA_RESTOREFILE_MODE_VALIDATE);
        printf("M16C: save probe (VALIDATE) returned %d (%s)\n",
               probe_rc, gba_restorefile_name(probe_rc));

        /* ***** THE CLASS AND CONFIDENCE ARE STILL MEASURED AND STILL
           REPORTED -- TO THE LOG. ***** This is the diagnostic that used to be
           a panel row. It is READ-ONLY: two accessors, no detection logic, and
           it runs on the same pre-launch state the row was drawn from, so the
           log line carries exactly what the screen used to. The wording says
           PROVISIONAL out loud because that is the whole reason it is no longer
           on the panel -- see the block above. It also keeps m16c_conf_text()
           referenced; m16c_class_text() is additionally used by the capture
           screen at SESSION COMPLETE. */
        printf("M16C: save class %s / confidence %s -- PROVISIONAL before the "
               "game runs, diagnostic only, not shown on the panel\n",
               m16c_class_text(gba_save_class()),
               m16c_conf_text(gba_save_confidence()));

        if (probe_rc == GBA_RESTOREFILE_OK)              save_word = "FOUND";
        else if (probe_rc == GBA_RESTOREFILE_ENOSAVE)    save_word = "NONE";
        else                                             save_word = "REJECTED";

        /* ---- R4: ENTERING THE CONFIRMATION -------------------------------
         *
         * ***** THE CROSS THAT CHOSE THE CARTRIDGE MUST NOT ALSO CONFIRM IT.
         * *****
         *
         * Two independent mechanisms make that true, and the order they are
         * set up in matters:
         *
         *   1. m13b_input_reset() puts a FRESH machine in M13B_IN_IDLE. This is
         *      the one that would bite: a machine in IDLE that is stepped with
         *      CROSS already down goes straight to M13B_IN_WAIT, and the
         *      operator's RELEASE of the picker press would then read as a
         *      clean launch edge. The arming gate below is what stops the
         *      machine from ever being stepped while CROSS is down, so IDLE is
         *      only ever entered from a genuinely quiet pad.
         *
         *   2. det_prev IS PRIMED FROM A LIVE READ, NOT FROM ZERO -- exactly as
         *      m16c_picker_reentry() does, and for exactly the same reason.
         *      `pressed = buttons & ~det_prev` can then only report an edge
         *      that really happened AFTER this instant, so a CIRCLE held on
         *      arrival produces no press and cannot cancel.
         *
         * ***** THE BLIND WINDOW IS WHY BOTH ARE NEEDED. ***** Between the
         * picker's release edge and this line the code opened the ROM, loaded
         * the BIOS, reset the emulator and hashed the image, and the pad was
         * NOT SAMPLED ONCE during any of it (the loading screen at step 450 is
         * draw-only). An impatient second CROSS press inside that window is
         * invisible to the edge detector -- priming adopts whatever is down
         * right now, and the arming gate refuses to act until it comes up. */
        m13b_input_reset(&det_in);
        (void)plat_pad_read(&pad);
        det_prev = pad.buttons;

        printf("M16C: details/confirm up -- det_prev primed to 0x%08X from a "
               "live pad read; CROSS launches on RELEASE, CIRCLE returns to "
               "the picker, and neither is accepted until both are seen clear "
               "for %u frames\n",
               (unsigned)det_prev, (unsigned)M16C_RELEASE_SETTLE);

        for (frame = 0; frame < M16C_DETAILS_MAX_FRAMES; frame++) {
            int y;
            u32 pressed;

            /* ---- THE CONFIRMATION STATE MACHINE ---------------------------
             *
             *   ENTER -> [unarmed]  CROSS or CIRCLE down     -> stay unarmed
             *                       both clear x SETTLE      -> ARMED
             *            [ARMED]    CROSS pressed + RELEASED -> LAUNCH
             *                       new CIRCLE press         -> CANCEL
             *                       frame bound reached      -> CANCEL
             *
             * There is no sleep and no frame-count debounce anywhere in it:
             * every transition is a button state or a button EDGE. */
            (void)plat_pad_read(&pad);
            pressed  = pad.buttons & ~det_prev;
            det_prev = pad.buttons;

            if (!det_armed) {
                /* ***** THE CROSS MACHINE IS NOT STEPPED WHILE UNARMED, AND
                   THAT IS THE ENTIRE HELD-BUTTON FIX. ***** Stepping it with
                   CROSS down is what would arm a launch on the release of the
                   picker's own press. Not calling it at all leaves it in IDLE,
                   so the first edge it can ever see is one made after the pad
                   went quiet. */
                if ((pad.buttons & M16C_DETAILS_ARM_MASK) == 0u) {
                    det_clear++;
                    if (det_clear >= M16C_RELEASE_SETTLE) {
                        det_armed = 1;
                        klog("LUAport M16C: the pad is quiet -- the launch "
                             "confirmation is now armed. CROSS launches, "
                             "CIRCLE goes back\n");
                    }
                } else {
                    det_clear = 0u;
                }
            } else {
                unsigned st;

                /* ***** LAUNCH IS ON THE RELEASE EDGE, VIA THE PROVEN MACHINE.
                   ***** m13b_picker.inc is READ-ONLY here -- this is a second
                   instance of it, not a change to it. The release contract
                   matters MORE on this screen than on the picker: gameplay
                   begins within a few frames of this decision, so a launch on
                   the press edge would hand the starting cartridge a phantom A
                   through its own title screen. */
                st = m13b_input_step(&det_in,
                                     (pad.buttons & PAD_CROSS) ? 1 : 0);

                if (st == M13B_IN_LAUNCH) {
                    det_action = M16C_DET_LAUNCH;
                } else if (st == M13B_IN_TIMEOUT) {
                    /* Handled EXACTLY as the picker handles it at step 449:
                       the terminal states are deliberately inert, so a machine
                       left in TIMEOUT would make this screen permanently deaf
                       to CROSS while CIRCLE still worked. Reset it. */
                    klog("LUAport M16C: CROSS was held for the whole release "
                         "window on the details screen and never came up. "
                         "Suspect a stuck button. Nothing was launched\n");
                    m13b_input_reset(&det_in);
                }

                /* ***** CIRCLE IS A PRESS EDGE, AND IT IS DOUBLY PROTECTED.
                   ***** It is measured against det_prev (so a CIRCLE held from
                   before this screen never produces an edge) AND it can only be
                   reached after arming, which itself required CIRCLE to be
                   clear for 12 consecutive frames. A stale or stuck CIRCLE
                   therefore cannot back out. Tested after CROSS so that a
                   launch already decided on this frame wins -- the two cannot
                   both fire. */
                if (det_action == M16C_DET_NONE && (pressed & PAD_CIRCLE))
                    det_action = M16C_DET_CANCEL;
            }

            /* Decided -- leave without drawing another frame, exactly as the
               picker's launch edge does at step 449. */
            if (det_action != M16C_DET_NONE)
                break;

            m16c_masthead();
            draw_hline(m16c_surface, 16, 4, UI_W - 4, M16C_C_INDIGO);

            m16c_display_name(lab, (unsigned)sizeof(lab), m16c_chosen_name);
            draw_str(m16c_surface, COL_CURSOR, 32, lab, M16C_C_SEL);

            /* ***** THIS PANEL IS ALL MEASUREMENT, SO IT IS ALL METADATA COLOUR
               -- EXCEPT THE TITLE. ***** The cartridge's own name is what the
               operator came to see, so it reads in the primary colour; the code
               and the size are supporting facts and take cyan. */
            y = 52;
            draw_str(m16c_surface, COL_CURSOR, y, "TITLE", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y, title[0] ? title : "NOT PRESENT",
                     title[0] ? M16C_C_TEXT : M16C_C_MUTED);
            y += 12;

            draw_str(m16c_surface, COL_CURSOR, y, "CODE", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y, code[0] ? code : "NOT PRESENT",
                     code[0] ? M16C_C_INFO : M16C_C_MUTED);
            y += 12;

            draw_str(m16c_surface, COL_CURSOR, y, "SIZE", M16C_C_MUTED);
            ln_reset(&l); ln_size(&l, rom_size);
            draw_str(m16c_surface, PANEL_X, y, l.b, M16C_C_INFO);
            y += 12;

            /* ***** ONE SAVE ROW, AND IT ANSWERS THE OPERATOR'S QUESTION.
               ***** What is wanted here is "will my save be there?", and that
               is precisely what VALIDATE returns. The class/confidence row that
               used to sit above this one is now a log line -- see the block
               above the loop for why a correct UNKNOWN was the wrong thing to
               put on screen at the one moment it cannot be anything else.

               FOUND is the only green on this screen. NONE is a neutral fact,
               not a success and not a fault, so it stays muted; anything else
               really is a rejected save file and takes the error colour. The
               colour is switched on the SAME probe_rc the word was built from,
               so the two cannot disagree. */
            draw_str(m16c_surface, COL_CURSOR, y, "SAVE DATA", M16C_C_MUTED);
            draw_str(m16c_surface, PANEL_X, y, save_word,
                     (probe_rc == GBA_RESTOREFILE_OK)      ? M16C_C_OK
                   : (probe_rc == GBA_RESTOREFILE_ENOSAVE) ? M16C_C_MUTED
                                                           : M16C_C_ERR);
            y += 12;

            /* The reason gets a row ONLY when there is one. An empty labelled
               row reads as a missing measurement. */
            if (probe_rc != GBA_RESTOREFILE_OK &&
                probe_rc != GBA_RESTOREFILE_ENOSAVE) {
                draw_str(m16c_surface, COL_CURSOR, y, "REASON", M16C_C_MUTED);
                draw_str(m16c_surface, PANEL_X, y, M16C_H_SAVE_BAD, M16C_C_ERR);
                y += 12;
                draw_str(m16c_surface, PANEL_X, y,
                         gba_restorefile_name(probe_rc), M16C_C_ERR);
                y += 12;
            }

            /* ***** THE CONTROLS LEGEND IS DRAWN AT FIXED COORDINATES AND
             * DELIBERATELY DOES NOT USE THE RUNNING `y`. ***** `y` is the
             * measurement panel's cursor and it MOVES: the REASON block above
             * adds two rows when a save file is rejected. Chaining this block
             * onto `y` would make its position depend on whether the operator's
             * save happened to validate, so the layout would be correct on the
             * common path and drift down the screen on the failure path -- the
             * exact case nobody tests. Anchoring it instead means the panel and
             * the legend CANNOT collide regardless of which rows the panel drew,
             * and the arithmetic can be checked here once:
             *
             *   worst case panel   TITLE 52, CODE 64, SIZE 76, SAVE DATA 88,
             *                      REASON 100, reason text 112
             *   last glyph bottom  112 + 8 = 120   (the font is 8x8)
             *   this rule          178            -- 58 px clear of the panel
             *
             * The clearance GREW from 6 px to 18 px when the save class row was
             * retired, and from 18 px to 58 px when the two retired legend rows
             * let this rule move down to 178, so this anchoring is now much
             * further from collision than the layout it was calculated for. The
             * numbers are restated rather than left stale: a comment that still
             * lists a row the loop no longer draws, or an old coordinate for a
             * row that moved, is how the next edit gets its arithmetic wrong.
             *   legend rows        188 .. 212 + 8 = 220
             *   footer rule        UI_H - 34 = 236 -- 16 px clear of the legend
             *
             * ***** THIS BLOCK REPORTS THE MAPPING, IT DOES NOT DEFINE IT.
             * ***** The bindings live in adapters/gba/gba_input.c:70-80 and are
             * pinned there by three _Static_asserts on M8_MAP_N. NOTHING here
             * reads, includes or re-derives them -- this is draw_str() and
             * nothing else -- so this screen can never be the reason a button
             * changes meaning. If the adapter's tables are ever edited, THIS
             * TEXT MUST BE EDITED TO MATCH; it is a legend, and a legend that
             * disagrees with the machine is worse than no legend.
             *
             * ***** THE UNUSED-BUTTON LINE AND THE "CONTROLS" HEADING ARE
             * ***** RETIRED, DELIBERATELY. They were both true and neither was
             * load-bearing. The heading labelled a table that is self-evidently
             * a control table, and the note named buttons the game ignores --
             * a fact the operator never has to ACT on, because the footer one
             * row below already states the return chord in full. Between them
             * they cost the widest string on the screen and two of its rows on
             * the panel the operator reads most. This is recorded so neither
             * row is "restored" later as a fix for a gap that was made on
             * purpose.
             *
             * ***** R7 CHANGED WHAT THIS TABLE HAS TO SAY, AND THIS COMMENT
             * USED TO GET IT WRONG. ***** The frozen table in
             * adapters/gba/gba_input.c:382-385 does still assert that SQUARE,
             * TRIANGLE, L2 and R2 contribute nothing, and it is UNCHANGED --
             * but it stopped being the whole machine-side truth the moment R7
             * put an alias layer in front of it. adapters/gba/gba_r7input.c
             * folds CROSS and CIRCLE onto one GBA A and SQUARE and TRIANGLE
             * onto one GBA B before the frozen table is ever consulted. The
             * rows below therefore describe the ALIAS LAYER, because that is
             * what the operator's thumbs actually meet; the "( O ) -> B" row
             * that stood here until this milestone was plainly FALSE on
             * shipping hardware, which is the whole reason this block moved.
             * Nothing about the frozen table or the alias layer is touched
             * here -- only the description of them. */
            draw_hline(m16c_surface, 178, 4, UI_W - 4, M16C_C_INDIGO);

            draw_str(m16c_surface, COL_CURSOR,    188, "D-PAD",    M16C_C_MUTED);
            draw_str(m16c_surface, M16C_LEG_VALX, 188, "D-PAD",    M16C_C_TEXT);
            draw_str(m16c_surface, 248,           188, "L1 / R1",  M16C_C_MUTED);
            draw_str(m16c_surface, 344,           188, "L / R",    M16C_C_TEXT);

            draw_str(m16c_surface, COL_CURSOR,    200, "( X ) / ( O )", M16C_C_MUTED);
            draw_str(m16c_surface, M16C_LEG_VALX, 200, "A",        M16C_C_TEXT);
            draw_str(m16c_surface, 248,           200, "OPTIONS",  M16C_C_MUTED);
            draw_str(m16c_surface, 344,           200, "START",    M16C_C_TEXT);

            /* ***** THE FRAMING IS A STRING; ONLY THE TWO SYMBOLS ARE DRAWN.
               ***** The parentheses and the slash come from the font in ONE
               draw_str, spaced so that the two empty cells land at exactly the
               indices the X and the O occupy on the row above -- cell 2 and
               cell 10. The symbols are then placed into those holes at the
               cell origin plus the font's one-pixel ink offset, so they sit
               pixel-for-pixel where the letters do, in the same colour. */
            draw_str(m16c_surface, COL_CURSOR,    212, "(   ) / (   )", M16C_C_MUTED);
            m16c_sym_square  (COL_CURSOR +  2 * 8 + 1, 212, M16C_C_MUTED);
            m16c_sym_triangle(COL_CURSOR + 10 * 8 + 1, 212, M16C_C_MUTED);
            draw_str(m16c_surface, M16C_LEG_VALX, 212, "B",        M16C_C_TEXT);
            draw_str(m16c_surface, 248,           212, "TOUCHPAD", M16C_C_MUTED);
            draw_str(m16c_surface, 344,           212, "SELECT",   M16C_C_TEXT);

            draw_hline(m16c_surface, UI_H - 34, 4, UI_W - 4, M16C_C_INDIGO);

            /* ***** "STARTING..." IS RETIRED BECAUSE R4 MADE IT A LIE. *****
               It described a screen that was already on its way into the game
               whatever the operator did. This screen now WAITS, so the row
               states the two things that can happen and who causes them.
               Occupying the SAME ROW keeps the layout arithmetic above (panel
               worst case ends at 120, this rule at UI_H-34) exactly as it was
               derived -- R4 adds no row and moves none.

               ***** THE TWO TRANSIENT PROMPTS ARE THE PICKER'S OWN WORDS,
               VERBATIM. ***** "RELEASE THE BUTTONS" and "RELEASE ( X ) TO
               LAUNCH" are what m16c_draw_picker() shows for the identical two
               conditions one screen earlier, in the identical two colours. An
               operator who has just met them in the picker meets no new
               vocabulary here, and the release contract reads as ONE rule the
               launcher applies everywhere rather than a quirk of this panel.

               The WAIT prompt in particular has to be visible: the launch
               fires on the CROSS-UP edge, so a HELD CROSS produces no other
               on-screen change whatsoever. */
            if (!det_armed)
                draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                         "RELEASE THE BUTTONS", M16C_C_WARN);
            else if (det_in.state == M13B_IN_WAIT)
                draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                         "RELEASE ( X ) TO LAUNCH", M16C_C_SEL);
            else
                draw_str(m16c_surface, COL_CURSOR, UI_H - 26,
                         "( X )  LAUNCH GAME        ( O )  BACK",
                         M16C_C_SEL);
            /* ***** THE ONE LINE OF ON-SCREEN TEXT THIS MILESTONE CHANGES.
               ***** The chord is M13C's, the hold is M13C's, the debounce is
               M13C's; only what it MEANS has moved, and the operator is told so
               in the words they will act on. */
            draw_str(m16c_surface, COL_CURSOR, UI_H - 14,
                     "HOLD L1 + R1 + L2 + R2 TO RETURN TO PICKER",
                     M16C_C_MUTED);

            m16c_flip();
        }

        /* ---- R4: THE CANCEL PATH -----------------------------------------
         *
         * ***** ONE TEST COVERS BOTH WAYS OF NOT LAUNCHING. ***** CIRCLE
         * leaves M16C_DET_CANCEL and the frame bound leaves M16C_DET_NONE, so
         * testing for "not LAUNCH" makes the fail-safe and the operator's
         * cancel literally the same code. Two separate exits could drift apart
         * on a later edit; this one cannot, and it is also why an unattended
         * timeout can never fall through into a game.
         *
         * ***** THE TEARDOWN IS NOT DUPLICATED HERE. IT IS DELEGATED. *****
         * By this point the cartridge is genuinely open: an arena mark is live,
         * the ROM buffers exist, the ROM FILE IS OPEN AND MAPPED, and the save
         * identity is LATCHED. A `break` or a `goto` back to the picker would
         * leak every one of those, and gba_save_latched() would still be 1 when
         * the next cartridge latched its own identity -- the exact data-loss
         * bit m16c_boundary() exists to assert clear. So cancelling calls the
         * SAME m16c_boundary() that every session-fatal on this path already
         * calls, and it owns gba_session_end(), the ROM close, the save
         * unlatch, the restore disarm, the buffer release, the input
         * neutralisation, the observer reset, the arena rewind and the RFILE
         * restoration. R4 adds NO teardown of its own.
         *
         * ***** IT IS THE EASIEST BOUNDARY THIS CODE EVER RUNS. ***** Every
         * claim it makes is trivially true here because no game has perturbed
         * anything: restore is not armed (that happens at step 453, strictly
         * later), REG_P1 is still neutral (m8_input_* is first touched in the
         * gameplay loop), and the rfile pool is back at its own baseline the
         * moment the ROM closes.
         *
         * ***** NOTHING THAT COULD TOUCH A SAVE HAS HAPPENED. ***** The only
         * save-layer call before this line is gba_restorefile_run(VALIDATE),
         * which opens both files "rb" against a read-only mount and
         * deliberately does not apply. No .sav and no .hdr is opened for
         * writing anywhere before the restore at step 453. Cancelling is
         * therefore incapable of altering an existing save.
         *
         * ***** THE LEDGER IS RESTORED, NOT RE-ARCHITECTED. ***** The
         * increment at step 450 stays exactly where it is, because the
         * "SESSION %u -- CHOSEN" lines and the load diagnostics between there
         * and here all name that number and would otherwise print 0. A cancel
         * simply gives it back, so m16c_sessions_started keeps meaning "games
         * actually started" for the payload ledger at step 459 and for
         * ext->dbg[5]. The underflow guard is belt-and-braces -- the increment
         * provably ran earlier in this same iteration -- but a counter that
         * could wrap to 4 billion on some future path is not worth the two
         * bytes saved. NO new persistent counter is introduced: a file-scope
         * static would have moved .bss off its frozen figure.
         *
         * ***** THE RETURN IS SILENT. ***** No CANCELLED screen, no failure
         * screen, no acknowledgement delay. A cancel is not a fault -- it is
         * the operator changing their mind -- so m16c_sessions_failed is NOT
         * incremented and m16c_session_fatal() is NOT used. `continue` goes to
         * the top of the session loop, which re-runs m16c_session_reset() and
         * then m16c_picker_reentry(): the full release gate AND the live
         * priming. A CIRCLE still held at the moment of cancel therefore
         * cannot exit the payload on arrival at the picker -- that case was
         * already solved there and is not re-solved here. */
        if (det_action != M16C_DET_LAUNCH) {
            if (det_action == M16C_DET_CANCEL)
                printf("M16C: CIRCLE on the details screen -- the operator "
                       "cancelled. The cartridge is being closed and the "
                       "picker is coming back. Nothing was played\n");
            else
                printf("M16C: the details screen reached its %u-frame bound "
                       "with no decision. An unattended confirmation is "
                       "treated as a CANCEL -- a timeout must NEVER launch\n",
                       (unsigned)M16C_DETAILS_MAX_FRAMES);

            if (m16c_sessions_started)
                m16c_sessions_started--;

            if (!m16c_boundary(ext, mark, mark_taken, rfile_before))
                return;

            continue;
        }

        printf("M16C: CROSS released on the details screen -- LAUNCH "
               "CONFIRMED after %u frame(s) on the panel\n", frame);

        /* =================================================================
         * steps 453-457 -- THE SESSION
         *
         * ***** EVERYTHING FROM HERE TO THE BOUNDARY IS M13C's, IN M13C's
         * ORDER. ***** The differences are exactly three, and every one of them
         * is a CONSEQUENCE of the loop rather than a new mechanism:
         *
         *   1. plat_time_init() and plat_audio_init() are NOT here. Both were
         *      hoisted to step 446/448 because both are properties of the
         *      PROCESS. What remains is the clock READ, which is per-session and
         *      stays exactly where M13C read it.
         *   2. An unclean session is SESSION-fatal, not payload-fatal. This is
         *      the whole point of the milestone: the game ends, LUAport does not.
         *   3. Every figure is also copied into m16c_rec, so the payload ledger
         *      at step 459 can report what happened across ALL sessions.
         *
         * ***** THE BLOCK SCOPE IS PROTECTION #1 AND IT IS LOAD-BEARING. *****
         * end_reason, clean, modified, commit_state, restore_state, every frame
         * counter, every hash and every arena figure is declared HERE, inside the
         * for(;;) body, so C re-initialises all of them on every iteration. There
         * is no function-scope copy of any of them for a previous game's result
         * to survive in.
         * ================================================================= */
        {
            struct gba_audio_stats aud;

            unsigned iter;
            unsigned frames_emulated  = 0u;
            unsigned frames_presented = 0u;
            unsigned flip_fail        = 0u;
            unsigned pad_reads_ok     = 0u;
            unsigned pad_reads_bad    = 0u;
            unsigned cur_keys = 0u, prev_keys = 0u, raw_pad = 0u;
            int      pad_conn   = 0;
            int      pad_ok     = 0;
            unsigned fr_irq     = 0u;
            unsigned irq_union  = 0u;
            unsigned input_edges = 0u;
            unsigned in_frames  = 0u;
            unsigned grains_frame = 0u;
            unsigned grains_total = 0u;
            unsigned rom_page_loads = 0u;
            unsigned page_delta = 0u;
            unsigned exit_hold  = 0u;
            unsigned skip_seen  = 0u;
            unsigned seedmask = 0u, seedgate = 0u;
            unsigned m8mask = 0u, m10mask = 0u, m10rep = 0u;
            unsigned map_fail_frame = 0u, map_probes = 0u;
            unsigned resident = 0u, resident_exp = 0u, file_blocks = 0u;
            unsigned lru_evicts = 0u;
            unsigned warm_done  = 0u;
            unsigned fps_x100   = 0u;
            unsigned arena_grow_frame = 0u;
            unsigned play_active = 0u;
            int      clean = 0;

            /* ***** PROTECTION #2, AND THE SEED IS THE WHOLE POINT. ***** This
               is read FROM the record m16c_session_reset() wrote and
               m16c_session_is_fresh() then verified, NOT from the literal
               M16C_END_WATCHDOG. If the two ever drifted apart the check at the
               top of the loop would be checking a value nothing used. Seeding
               from the verified record makes that impossible by construction --
               and WATCHDOG is the correct pre-load, because falling out of the
               frame loop naturally must be recorded as the backstop firing
               rather than as a completed session. */
            unsigned end_reason = m16c_rec.end_reason;

            u64 run_t0 = 0, run_t1 = 0, elapsed_us = 0;
            u64 arena_prev  = 0, arena_now = 0;
            u64 arena_play  = 0, arena_end = 0;
            u64 arena_grow_from = 0, arena_grow_to = 0;
            u64 arena_after_warm = 0;

            /* M17 TIER-1. SESSION-SCOPE, LIKE EVERY OTHER COUNTER IN THIS
               BLOCK -- declared inside the for(;;) body so C re-initialises
               them per session and no previous game's drift can survive into
               the next one. These are DERIVED, not measured: nothing writes
               them but the arithmetic below, and nothing reads them but the
               diagnostic printf. They are NOT terms in the clean verdict. */
            s64 aud_drift_frames = 0, aud_drift_ms = 0;

            int      restore_rc  = GBA_RESTOREFILE_ENOSAVE;
            int      commit_rc   = GBA_SAVEFILE_ENOTREADY;
            unsigned rf_latch    = 0u;
            unsigned sf_latch    = 0u;
            unsigned restored_bytes = 0u;
            unsigned commit_bytes   = 0u;
            unsigned base_kind   = GBA_RESTORE_BASE_NEWGAME;
            unsigned start_hash  = 0u, end_hash = 0u;
            unsigned modified    = 0u;
            unsigned live_class  = 0u, hdr_class = 0u;
            unsigned commit_pay  = 0u;
            int      commit_tried = 0;

            unsigned restore_state = 0u;  /* 0 NONE, 1 LOADED, 2 REJECTED      */
            unsigned commit_state  = 0u;  /* 0 NOT NEEDED, 1 UPDATED, 2 FAILED */

            /* ---- THE COMMIT-SIDE SRAM CAPTURE ---------------------------
             *
             * Every one of these is READ from an existing accessor -- the
             * observer, gba_save.h or gba_restore.h. NOT ONE OF THEM IS
             * RECOMPUTED HERE, so the screen cannot disagree with the adapter it
             * is reporting on.
             *
             * `cap_verdict` applies the decision rule ONCE, where the values are
             * in scope, so the on-screen line and the log line cannot drift
             * apart. */
            unsigned cap_taken       = 0u;  /* was T3 actually sampled          */
            unsigned cap_low_nonff   = 0u, cap_high_nonff = 0u;
            unsigned cap_low_fnv     = 0u, cap_high_fnv   = 0u;
            unsigned cap_live_class  = 0u, cap_raw_type   = 0u;
            unsigned cap_class       = 0u, cap_region     = 0u;
            unsigned cap_region_hash = 0u;
            unsigned cap_wr_base     = 0u, cap_wr_exit    = 0u;
            unsigned cap_bits        = 0u;
            unsigned cap_verdict     = 0u;  /* see M16C_CAP_* above             */

            /* ---- step 453 (1) THE CLOCK READ. THE INIT IS AT STEP 448. ----
             *
             * ***** M13C CALLS plat_time_init() HERE AND M16-2 DOES NOT -- SEE
             * STEP 448 FOR THE ARGUMENT. ***** What is left is the per-session
             * READ, which stays exactly where M13C took it. run_t0 is re-read at
             * step 455 immediately before the frame loop; this early read exists
             * only to prove the clock is STILL live for THIS session, because
             * `elapsed_us > 0` is a term in the clean verdict and a clock that
             * died between games must not be discovered after the operator has
             * already played one.
             *
             * ***** A CLOCK THAT DIES MID-PAYLOAD IS RUNTIME-FATAL, NOT
             * SESSION-FATAL. ***** gettimeofday does not become unresolvable for
             * one cartridge and resolvable for the next, so returning to the
             * picker would offer a list of games every one of which would be
             * refused at its own verdict. */
            ext->step = 453;

            if (plat_time_us() == 0u) {
                klog("LUAport M16C: ***** THE CLOCK READ ZERO AFTER HAVING BEEN "
                     "LIVE AT STEP 448. ***** Every session's verdict depends on "
                     "a real elapsed time, so this ends the payload rather than "
                     "one game\n");
                ext->dbg[4] = 0;
                m16c_runtime_fatal(ext, M16C_CLOCK_DEAD, M16C_H_TIMING,
                                   "THE SYSTEM CLOCK STOPPED RESPONDING.",
                                   "LUAPORT STOPPED RATHER THAN RUN A SESSION",
                                   "IT COULD NOT TIME.");
                return;
            }
            printf("M16C: NO session duration bound -- the operator's chord is "
                   "the only normal exit; emergency watchdog %u iterations "
                   "(%u s at the %u Hz ceiling, ~%u min at the measured rate)\n",
                   (unsigned)M16C_PLAY_MAX_ITERS, (unsigned)M16C_WD_SECONDS,
                   (unsigned)M16C_WD_FPS_CEIL, (unsigned)M16C_WD_MINUTES);

            /* ---- (2) THE PRE-EXECUTION ENVIRONMENT GATE -----------------
             *
             * ***** TWO BITS OF m5_exec_probe_seed() ARE USED AND THE OTHER TEN
             * ARE DELIBERATELY NOT. THIS IS AN EXTRACTION, NOT A MASKING. *****
             *
             *   M5_SEED_WS_ZERO     ws_cyc_seq/nseq[8][1] == 0, which means
             *                       reload_timing_info() never ran. gba_exec.h:
             *                       125-132: every ROM instruction would cost
             *                       ZERO cycles, cycles_remaining would never
             *                       fall, and the interpreter WOULD SPIN FOREVER
             *                       ON THE CONSOLE. This is the single most
             *                       important gate before the first instruction.
             *   M5_SEED_NOT_MAPPED  memory_map_read[0x08] == 0.
             *
             * The remaining bits describe M5's ARTIFICIAL single-instruction
             * seed, which freezes the machine at one decodable branch with
             * skip_next_frame forced to 1 so NO PIXEL is written. Gameplay is the
             * exact opposite, so requiring the whole mask to be zero would refuse
             * to start on PERFECTLY CORRECT state. The probe is a PURE READER
             * (gba_exec.c:338-395 only compares globals), so extracting two bits
             * cannot perturb what it measures, and the RAW mask is logged in full
             * so nothing is concealed.
             *
             * ***** IT IS SESSION-FATAL. ***** reset_gba() ran for THIS
             * cartridge, so a failed gate describes this session's emulator
             * state. The boundary tears it down and the picker comes back. */
            seedmask = m5_exec_probe_seed();
            seedgate = seedmask & (unsigned)(M5_SEED_WS_ZERO |
                                             M5_SEED_NOT_MAPPED);

            printf("M16C: m5_exec_probe_seed() RAW 0x%08X, environment bits "
                   "0x%08X\n", seedmask, seedgate);
            printf("M16C:   waitstate tables loaded %s, gamepak window mapped "
                   "%s\n",
                   (seedmask & M5_SEED_WS_ZERO)    ? "NO" : "YES",
                   (seedmask & M5_SEED_NOT_MAPPED) ? "NO" : "YES");

            if (seedgate) {
                ext->dbg[4] = (u64)seedmask;
                if (!m16c_session_fatal(ext, M16C_EXEC_UNSAFE, M16C_H_NOTREADY,
                        "THE EMULATOR WAS NOT SAFE TO START.",
                        "STARTING WOULD HAVE LOCKED THE CONSOLE, SO NOTHING",
                        "RAN. RETURNING TO PICKER.",
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }

            /* ---- (3) THE PAGING BASELINE --------------------------------
             *
             * TWO PROBES, AND THEY ARE NOT REDUNDANT. m8map_probe() predates
             * demand paging: its M8MAP_SWAP bit is SET whenever
             * gamepak_must_swap() is true, which for an 8 MB cartridge with
             * ROM_BUFFER_SIZE=2 is the CORRECT and expected state, so it is
             * REPORTED and never gated on -- treating it as failure would refuse
             * Metroid Fusion for being large. m10map_probe() is the
             * demand-paging-aware one and IS the gate: it compares the resident
             * page set against the LRU queue and asserts that NON-resident pages
             * have NULL slots. */
            m8mask  = m8map_probe();
            m10mask = m10map_probe();
            m10rep  = m10map_report();

            file_blocks  = m10map_read(M10MAP_RD_FILEBLOCKS);
            resident     = m10map_read(M10MAP_RD_RESIDENT);
            resident_exp = m10map_expected_resident();

            printf("M16C: m8map 0x%08X (REPORTED), m10map 0x%08X report 0x%08X "
                   "(GATED)\n", m8mask, m10mask, m10rep);
            printf("M16C: file blocks %u, resident %u, expected %u\n",
                   file_blocks, resident, resident_exp);

            if (m10mask) {
                /* ***** THE DIAGNOSTIC LINE IS BUILT HERE AND NOWHERE ELSE.
                   ***** M16-2 could not be diagnosed from hardware because the
                   one number that names the FAILURE CLASS never reached a
                   surface the operator can read. ext->dbg[4] is written on the
                   line below, but m16c_session_fatal() OVERWRITES it with the
                   status code (-633) before returning, and m16c_fail_screen()
                   draws no dbg[] slot at all -- so the mask was unreachable on
                   both the error screen and the exit screen.

                   The fix is deliberately NOT to preserve dbg[4] globally: that
                   write is shared plumbing used by every other failure site and
                   changing it would alter unrelated paths. Instead the values
                   are formatted HERE, on this path only, and handed to the
                   EXISTING d3 field. d3 previously carried "SO NOTHING WAS
                   STARTED. RETURNING TO PICKER." -- a sentence the tail line
                   "( O ) RETURNS TO PICKER" already tells the operator, so no
                   operator-facing meaning is lost. d1 and d2 are untouched.

                   ***** THIS CHANGES NOTHING ABOUT THE FAILURE. ***** The gate
                   still fires on the same condition, still abandons the session,
                   still runs the complete boundary, and still returns to the
                   picker. Nothing is scrubbed, NULLed, masked or weakened -- the
                   -633 remains fail-closed and merely becomes legible.

                   BADSLOT is rendered as text when it is M10MAP_NOPAGE, because
                   0xFFFFFFFF there means "no single slot was blamed" (the mask
                   came from a counting or geometry assertion, not a slot walk)
                   and printing 4294967295 would read as a real slot number. */
                struct m16c_line ml;
                unsigned badslot = m10map_read(M10MAP_RD_BADSLOT);

                ext->dbg[4] = (u64)m10mask;

                ln_reset(&ml);
                ln_puts(&ml, "M10MAP ");
                ln_hex32(&ml, m10mask);
                ln_puts(&ml, " SLOT ");
                if (badslot == M10MAP_NOPAGE) ln_puts(&ml, "NONE");
                else                          ln_udec(&ml, badslot);
                ln_puts(&ml, " STR ");
                ln_udec(&ml, m10map_read(M10MAP_RD_STRIDE));
                ln_puts(&ml, " RES ");
                ln_udec(&ml, resident);
                ln_putc(&ml, '/');
                ln_udec(&ml, resident_exp);
                /* TL -- how many stale map-tail slots THIS session cleared.
                   TL 0 here means the clear never ran; TL non-zero with a SLOT
                   still inside 6656..7167 means something re-polluted the band
                   after the clear, which would be a DIFFERENT mechanism. */
                ln_puts(&ml, " TL ");
                ln_udec(&ml, m16c_maptail_cleared);

                printf("M16C: %s\n", ml.b);

                if (!m16c_session_fatal(ext, M16C_EXEC_UNSAFE, M16C_H_NOTREADY,
                        "THE CARTRIDGE MEMORY MAP WAS INCONSISTENT.",
                        "EVERY FRAME WOULD HAVE COME FROM THE WRONG MEMORY,",
                        ml.b,
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }

            /* ---- (4) THE AUDIO RING. THE PORT IS ALREADY OPEN. ----------
             *
             * ***** THERE IS NO plat_audio_init() HERE, AND ITS ABSENCE IS
             * DELIBERATE. ***** The port was opened once at step 446 and
             * gba_session.h:98-101 requires it to stay open across the boundary.
             * The RING, however, is per-session state -- it is step 2 of
             * gba_session_end() -- so by the time control reaches this point the
             * ring has already been cleared: at step 446 for session 1, and by
             * the previous session's boundary for every session after it.
             *
             * ***** THE STATISTICS ARE ZEROED BY THAT SAME CALL, AND THAT IS
             * WHAT MAKES THE CLEAN VERDICT WORK AT ALL IN A LOOPING LAUNCHER.
             * ***** gba_audio.c:349-360 zeroes the WHOLE counter block --
             * produced, consumed, pulled, grains, underruns, OVERRUNS, nonzero,
             * peak and fill. That matters far more than it looks: `aud.overruns
             * == 0` is both a per-frame break condition in the loop below AND a
             * term in the seven-term clean verdict. Had gba_audio_reset() cleared
             * only the ring and left the counters running, ONE dropped frame in
             * session 1 would have broken out of session 2's frame loop on its
             * FIRST iteration and refused to commit its save -- and every session
             * after it, for the rest of the payload. The counters are
             * per-session because that call makes them per-session.
             *
             * ***** SO IT IS ASSERTED RATHER THAN ASSUMED. ***** Both halves are
             * checked: a stale RING would play the previous cartridge's samples
             * over the first frames of this one, and a stale OVERRUN COUNT would
             * silently condemn this session before it began. Both are exactly the
             * class of cross-session leakage this milestone exists to rule out,
             * and both are far too cheap to check to be left to inference. */
            gba_audio_stats(&aud);
            printf("M16C: audio on entry -- fill %u, peak %u, underruns %llu, "
                   "overruns %llu (the PORT stayed open; the RING and every "
                   "COUNTER are per-session)\n",
                   aud.fill, aud.peak,
                   (unsigned long long)aud.underruns,
                   (unsigned long long)aud.overruns);
            if (aud.fill != 0u || aud.overruns != 0u || aud.underruns != 0u ||
                aud.peak != 0u) {
                printf("M16C: ***** THE AUDIO STATE WAS NOT CLEAR AT SESSION "
                       "START: fill %u, overruns %llu, underruns %llu, peak "
                       "%u *****\n",
                       aud.fill, (unsigned long long)aud.overruns,
                       (unsigned long long)aud.underruns, aud.peak);
                klog("LUAport M16C: gba_session_end() step 2 is gba_audio_reset() "
                     "and it runs on every path out of a session, so anything "
                     "non-zero here means the boundary did not do what it "
                     "reported. This session would either hear the PREVIOUS "
                     "cartridge's samples or be condemned by its overrun count "
                     "before a single frame ran\n");
                ext->dbg[4] = (u64)aud.fill;
                m16c_runtime_fatal(ext, M16C_BOUNDARY_DIRTY, M16C_H_BOUNDARY,
                                   "THE SOUND FROM THE LAST GAME DID NOT CLEAR.",
                                   "LUAPORT STOPPED RATHER THAN CARRY IT INTO",
                                   "THIS GAME.");
                return;
            }

            /* ---- (5) THE PRESENTATION HANDOFF ---------------------------
             *
             * ***** BOTH FRAMEBUFFERS ARE CLEARED TO THE BORDER COLOUR, AND IN A
             * LOOPING LAUNCHER THIS STOPS BEING COSMETIC AND BECOMES REQUIRED.
             * *****
             *
             * gba_present_blit() writes ONLY the centred 1440x960 rectangle
             * (gba_present.h:116-121). The 240-pixel side bars and the 60-pixel
             * top and bottom bands are never touched by it. The picker and the
             * information screen, in contrast, drew over the WHOLE surface
             * through blit_ui(). Without this clear the ROM LIST would stay
             * burned into the letterbox borders for the entire session, in both
             * buffers -- and in M16-2 it would be the ROM list the operator was
             * looking at seconds ago, which reads as a launcher that failed to
             * change screens.
             *
             * ***** THIS IS ALSO WHERE THE UI ENDS. ***** Nothing is drawn again
             * until the session is over. No FPS counter, no hash, no frame
             * number, no page count. Every one of those figures is still measured
             * and still printed to the log below; none of it reaches the picture.
             *
             * VideoOut is NOT reopened -- these are the SAME two framebuffers
             * plat_video_init() handed over at step 446, on every session. */
            plat_video_fill(0, GBA_PRESENT_BORDER);
            plat_video_fill(1, GBA_PRESENT_BORDER);
            for (i = 0; i < M16C_BLACK_FRAMES; i++) {
                if (plat_video_present((int)(i & 1u), m16c_frame_id++) < 0)
                    flip_fail++;
            }

            /* ---- (6) THE INPUT HANDOFF ----------------------------------
             *
             * ***** REG_P1 IS DRIVEN TO NEUTRAL BEFORE THE FIRST FRAME. *****
             * m8_input_apply(0) writes 0x3FF, and the GBA keypad is ACTIVE LOW,
             * so 0x3FF means "no key held". THE M8 MAPPING IS REUSED UNCHANGED --
             * no second mapping policy is created and no button is remapped.
             *
             * ***** IN A LOOPING LAUNCHER THIS IS BELT AND BRACES, AND BOTH ARE
             * WANTED. ***** m16c_picker_reentry() already held the picker until
             * every one of L1+R1+L2+R2, CROSS and CIRCLE came up, and the launch
             * itself is on the CROSS RELEASE edge, so the operator's fingers are
             * provably off the pad. Independently, step 1 of gba_session_end() is
             * m8_input_apply(0) and m16c_boundary() gates on GBA_SESS_R_INPUT, so
             * the register was already neutral when the picker appeared. This
             * makes the guarantee explicit AT THE REGISTER for the frame that is
             * about to run, rather than inferred from two state machines. */
            m8_input_apply(0u);
            prev_keys = 0u;
            printf("M16C: REG_P1 forced neutral = 0x%03X before the first "
                   "frame\n", m8_input_read_reg_p1());

            /* ---- (7) THE PAGING OBSERVER -------------------------------
               Snapshots the gamepak block queue so the page-load count measures
               THIS session and not the load itself. A PURE OBSERVER: it cannot
               cause a fault. It is ALSO step 7 of gba_session_end(), so this is
               the same belt-and-braces as the input handoff above. */
            m10map_paging_reset();

            /* ---- step 454: THE RESTORE ----------------------------------
             *
             * ***** THE CALL SITE IS M12C's AND M13C's, AND IT IS NOT MOVED.
             *
             *   AFTER RESET #2, because init_memory() restores backup_type from
             *   backup_type_reset (gba_memory.c:2438) and re-defaults eeprom_size
             *   (:2441), flash_bank_num, flash_mode, eeprom_mode, eeprom_address
             *   and eeprom_counter. Restoring before it would be restoring into
             *   state that is about to be re-defaulted.
             *
             *   AND IT IS SAFE TO BE AFTER IT, because init_memory() PROVABLY
             *   NEVER WRITES gamepak_backup[]. The only writers in gpsp/ are
             *   gba_memory.c:581, :587, :1195, :1206, :1220, :1239 and :1247 --
             *   all reachable only from EXECUTING code -- plus the
             *   blank-normalise at :2906, which load_gamepak() already ran.
             *
             *   BEFORE THE LOOP, because the game must not read blank save
             *   memory first. This is the LAST point at which that is still
             *   guaranteed.
             *
             *   AFTER THE AUDIO AND PRESENTATION BRING-UP, because both of those
             *   can fail; doing the file work first would mean a restore had
             *   already happened on a session that never played a frame.
             *
             * ***** IN A LOOPING LAUNCHER THE 0xFF FILL AT STEP 451 (h) IS WHAT
             * MAKES THIS CORRECT. ***** gamepak_backup[] is not re-zeroed by
             * init_memory(), so without that fill this restore would apply THIS
             * cartridge's save file on top of the PREVIOUS cartridge's residual
             * bytes, and the baseline below would be computed over the mixture.
             * An A -> B -> A sequence would then restore A's own save over bytes
             * B had left behind. The fill sits immediately before the load and
             * the load sits immediately before this, so there is no window.
             *
             * ***** THE ADAPTER IS ARMED EXPLICITLY, AND THE DISARM IS THE
             * BOUNDARY'S JOB. ***** gba_restore_apply() refuses every byte while
             * disarmed (gba_restore.h:152-167), so the ability to mutate
             * gamepak_backup is a property of THIS CALL and not of the fixture
             * happening to reach a call site. Step 5 of gba_session_end() is
             * gba_restore_disarm() and m16c_boundary() gates on
             * GBA_SESS_R_ARMED, which is what stops the NEXT session's
             * validate-only probe at step 452 -- documented as provably passive
             * -- from running ARMED.
             *
             * NOTHING IS WRITTEN HERE. A pair of plain fopen(..., "rb") reads
             * against a read-only mount. */
            ext->step = 454;

            live_class = gba_restore_live_class();
            printf("M16C: live save class %u -- raw UNIT COUNTS backup_type %u, "
                   "flash_bank_cnt %u, eeprom_size %u\n",
                   live_class,
                   gba_restore_read(GBA_RESTORE_RD_BACKUP_TYPE),
                   gba_restore_read(GBA_RESTORE_RD_BANKCNT),
                   gba_restore_read(GBA_RESTORE_RD_EEPSIZE));

            if (sav_name[0])
                printf("M16C: the restore will look for %s (and the .hdr beside "
                       "it)\n", sav_name);

            /* ***** THE OBSERVER IS CLEARED BEFORE THE FIRST SAMPLE, NOT AFTER.
               ***** m12c_sramobs_sample() marks a slot TAKEN the first time it
               is written, and an untaken slot is what makes "UNKNOWN"
               distinguishable from "measured zero" further down. Resetting after
               a sample would silently erase that distinction. Step 6 of
               gba_session_end() does this too; the boundary's copy protects the
               NEXT session and this one protects THIS session, and neither
               removes the need for the other. */
            m12c_sramobs_reset();

            gba_restore_arm();
            restore_rc = gba_restorefile_run(GBA_RESTOREFILE_MODE_RESTORE);
            rf_latch   = gba_restorefile_latch();
            hdr_class  = gba_restorefile_read(GBA_RESTOREFILE_RD_CLASS);

            printf("M16C: gba_restorefile_run() returned %d (%s), latch "
                   "0x%08X\n", restore_rc, gba_restorefile_name(restore_rc),
                   rf_latch);

            if (restore_rc == GBA_RESTOREFILE_OK) {
                restored_bytes = gba_restore_applied();
                base_kind      = GBA_RESTORE_BASE_RESTORED;
                restore_state  = 1u;
                printf("M16C: restored %u bytes, header class %u, payload FNV "
                       "0x%08X\n", restored_bytes, hdr_class,
                       gba_restorefile_read(GBA_RESTOREFILE_RD_HDRPAYFNV));
            } else if (restore_rc == GBA_RESTOREFILE_ENOSAVE) {
                klog("LUAport M16C: no save exists for this cartridge yet. That "
                     "is the expected FIRST-RUN result and it is NOT a failure "
                     "-- the game boots as a new game and whatever it creates "
                     "will be committed\n");
            } else {
                /* ***** A REJECTION IS THE VALIDATOR WORKING, NOT FAILING.
                   ***** gamepak_backup was not mutated (or was rolled back to
                   0xFF), the files on disk were not touched, and the game boots
                   clean. */
                restore_state = 2u;
                printf("M16C: the save on disk was REJECTED -- %s\n",
                       gba_restorefile_name(restore_rc));
                klog("LUAport M16C: ***** THE RESTORE WAS DECLINED AND THAT IS "
                     "THE MECHANISM WORKING. ***** Persisted state was found "
                     "that could not be vouched for, so it was REFUSED rather "
                     "than applied. The file on disk is UNCHANGED and is not "
                     "overwritten unless this session genuinely creates new save "
                     "data\n");
            }

            /* ---- THE BASELINE. ***** THE SINGLE MOST IMPORTANT LINE. *****
             *
             * ***** IT IS TAKEN HERE, AFTER THE RESTORE, AND IT IS NOT
             * gba_save_latch()'s. ***** gba_save_latch() ran before the restore,
             * over 131,072 bytes that were still entirely 0xFF. Using that
             * baseline would mean the instant 8192 real bytes were restored the
             * array differed from it and EVERY SESSION WOULD REPORT DIRTY --
             * announcing that the game modified the save when nothing but the
             * restore had happened, and rewriting the very file it had just read.
             *
             * THE FULL 131,072 BYTES, NOT THE REGION, for gba_save.h:374-380's
             * reason: the region CAN CHANGE SIZE mid-session when backup_type or
             * eeprom_size resolves upward. A baseline hashed over one size is not
             * comparable with an exit hash over another, and comparing them would
             * report DIRTY for a pure RECLASSIFICATION in which not one byte
             * changed. Hashing the whole array is immune to that. */
            gba_restore_baseline(base_kind);
            start_hash = gba_restore_baseline_hash();
            printf("M16C: START HASH 0x%08X over the full %u bytes, baseline "
                   "%s\n", start_hash, (unsigned)GBA_RESTORE_BACKUP_BYTES,
                   (base_kind == GBA_RESTORE_BASE_RESTORED) ? "RESTORED"
                                                            : "NEWGAME");

            /* ---- T1: THE STATE THE GAME IS ABOUT TO BOOT INTO -----------
               Taken after the restore and after the baseline hash, exactly as
               gba_sramobs.h:75-77 specifies. This is the row that answers "was
               the upper half already dirty before a single instruction ran" --
               if it is, the T2->T3 comparison no longer starts from a known
               state and no conclusion about region size may be drawn from it. */
            m12c_sramobs_sample(GBA_SRAMOBS_T1);
            printf("M16C: SRAM T1 low nonFF %u hash 0x%08X, high nonFF %u "
                   "hash 0x%08X\n",
                   m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF
                                     + GBA_SRAMOBS_T1),
                   m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV
                                     + GBA_SRAMOBS_T1),
                   m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF
                                     + GBA_SRAMOBS_T1),
                   m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV
                                     + GBA_SRAMOBS_T1));

            arena_play = arena_used();
            arena_prev = arena_play;

            klog("LUAport M16C: ==============================================\n");
            printf("M16C: THE CARTRIDGE IS NOW RUNNING -- SESSION %u, '%s', "
                   "SAVE ID %s\n", m16c_sessions_started,
                   title[0] ? title : m16c_chosen_name, save_id);
            klog("LUAport M16C: TO END THE SESSION: hold L1+R1+L2+R2. THAT "
                 "RETURNS YOU TO LUAPORT -- it does not close it.\n");
            klog("LUAport M16C: SAVE IN-GAME BEFORE RETURNING -- the commit "
                 "persists what the GAME wrote, and a game that never saved has "
                 "nothing to persist.\n");
            klog("LUAport M16C: ==============================================\n");

            /* =============================================================
             * step 455 -- THE PER-FRAME LOOP. M10D's ORDER, UNCHANGED.
             * =============================================================
             *
             *     1 plat_pad_read        LAYER A -- the physical DualSense
             *     2 r7_input_from_pad    LAYER B -- raw bits -> 10-bit key mask
             *                            (R7's alias layer; it DELEGATES to
             *                            m8_input_from_pad, which is still the
             *                            only pad-to-key authority)
             *     3 m8_input_apply       LAYER B -- REG_P1, ACTIVE LOW
             *     4 m8_input_irq_edge    LAYER C -- EXACTLY ONCE (side effect)
             *     5 m5_exec_run          ONE GBA frame, gpSP's interpreter
             *     6 gba_present_blit     M7's conversion, unmodified
             *     7 plat_video_present   M0's flip, unmodified
             *     8 gba_audio_pull       \
             *     9 gba_audio_resample    >  AFTER the flip. Never before it.
             *    10 gba_audio_drain      /
             *
             * ***** EXACTLY ONE LINE OF THIS LOOP DIFFERS FROM M13C's: STEP 2
             * NOW CALLS r7_input_from_pad() INSTEAD OF m8_input_from_pad().
             * *****
             * Through M16C's freeze this loop was byte-for-byte M13C's, and the
             * claim recorded here was that the ONLY change in the whole session
             * was what the `break` at the bottom falls INTO. R7 adds the second
             * change, and it is confined to that single identifier:
             *
             *   - the ORDER of the ten steps is unchanged;
             *   - no step is added, removed, merged or moved;
             *   - the replacement has m8_input_from_pad()'s exact signature and
             *     contract and returns the same kind of 10-bit key mask, so
             *     steps 3 and 4 consume precisely what they consumed before;
             *   - m8_input_from_pad() is STILL CALLED every frame -- by
             *     r7_input_from_pad(), one level down -- so the frozen
             *     translation still happens, it simply receives a pad word in
             *     which the two intentional equivalence classes have already
             *     been collapsed onto their representatives.
             *
             * Everything else the operator experiences for the next hour is
             * still the frozen launcher's inner loop.
             *
             * ***** STEP 4 IS CALLED ONCE PER FRAME. ***** m8_input_irq_edge()
             * calls flag_interrupt() when the condition matches, so calling it
             * twice would raise the keypad interrupt TWICE.
             *
             * ***** ALL THREE AUDIO STEPS LIE AFTER plat_video_present() HAS
             * RETURNED. ***** sceAudioOutOutput BLOCKS until its queued grain is
             * consumed, and the reference implementation uses exactly that as its
             * frame pacer. LUAport must NOT be paced by it. Submitting
             * immediately past the vsync edge means any blocking consumes time
             * the loop would otherwise spend idle before the next flip, so the
             * two waits OVERLAP instead of stacking. Moving audio above the flip
             * would silently re-pace the emulator.
             *
             * ***** ONE m5_exec_run() PER ITERATION AND NOTHING ELSE IN THAT
             * STEP. ***** execute_arm() does not return on cycle exhaustion; its
             * only return path is the frame wrap (gba_exec.h:23-48), so one call
             * is exactly one emulated frame. There is no second call, no
             * frameskip and NO HUD DRAWN OVER THE PICTURE. */
            ext->step = 455;

            /* ---- T2: BEFORE ONE EMULATED INSTRUCTION HAS RUN -------------
             *
             * ***** IT IS TAKEN BEFORE run_t0, NOT AFTER. ***** Hashing 64 KB is
             * cheap but it is not free, and `elapsed_us` feeds both the reported
             * frame rate and the `elapsed_us > 0` term of the clean verdict.
             * Sampling inside the timed window would charge the observer's cost
             * to the emulator's measured performance -- an observer that alters
             * the number it is standing next to. T2 is the comparison base for
             * "did the GAME change this", because only T2->T3 brackets a window
             * in which emulated cartridge code actually ran. */
            m12c_sramobs_sample(GBA_SRAMOBS_T2);

            run_t0 = plat_time_us();

            for (iter = 0; iter < M16C_PLAY_MAX_ITERS; iter++) {

                /* ---- 1: the physical pad ------------------------------ */
                pad_ok = plat_pad_read(&pad);
                if (pad_ok) {
                    pad_reads_ok++;
                    pad_conn = pad.connected ? 1 : 0;
                    raw_pad  = pad_conn ? pad.buttons : 0u;
                } else {
                    pad_reads_bad++;
                    pad_conn = 0;
                    raw_pad  = 0u;
                }

                /* ---- 2 and 3: raw bits -> GBA keys -> REG_P1 (ACTIVE LOW)
                 *
                 * ***** STEP 2 IS R7's ALIAS LAYER, WHICH DELEGATES TO THE
                 * FROZEN TRANSLATOR. ***** r7_input_from_pad() canonicalises
                 * CROSS|CIRCLE onto one GBA A and SQUARE|TRIANGLE onto one GBA
                 * B, then calls m8_input_from_pad() -- so the frozen table in
                 * gba_input.c is still the only thing that decides what a
                 * button MEANS. See adapters/gba/gba_r7input.h.
                 *
                 * ***** raw_pad IS NOT REWRITTEN AND MUST NEVER BE. ***** The
                 * aliasing happens entirely INSIDE the call; `raw_pad` still
                 * holds the untouched physical word, which is what the return
                 * chord below tests. Assigning a canonicalised word back into
                 * raw_pad would silently feed aliased bits to the chord. */
                cur_keys = r7_input_from_pad(raw_pad, pad_conn);
                m8_input_apply(cur_keys);

                /* ---- 4: the keypad IRQ edge. EXACTLY ONCE. ------------ */
                fr_irq     = m8_input_irq_edge(cur_keys, prev_keys);
                irq_union |= fr_irq;
                if (fr_irq & M8_IRQ_EDGE) input_edges++;

                /* ---- 5: ONE EMULATED FRAME. THE ONLY execute_arm IN
                     LUAPORT. */
                m5_exec_run(m2_probe_read(M2_RD_EXEC_CYCLES));
                frames_emulated++;

                /* ---- 6: M7's 240x160 -> 1440x960 conversion ----------- */
                play_active = iter & 1u;
                gba_present_blit(play_active ? m16c_fb1 : m16c_fb0,
                                 m16c_gba_screen);

                /* ---- 7: M0's flip ------------------------------------- */
                if (plat_video_present((int)play_active,
                                       m16c_frame_id++) < 0) {
                    flip_fail++;
                } else {
                    frames_presented++;
                }

                /* ---- 8: drain gpSP's mix bus -------------------------- */
                in_frames = gba_audio_pull();

                /* ---- 9: the frozen 512:375 conversion ----------------- */
                if (in_frames) gba_audio_resample(in_frames);

                /* ---- 10: submission, WHOLE GRAINS, CAPPED ------------- */
                grains_frame  = gba_audio_drain(GBA_AUDIO_MAX_GRAINS);
                grains_total += grains_frame;

                /* ---- bookkeeping. NOT part of any of the ten steps. --- */

                /* THE RING MUST NEVER SATURATE. An overrun means output frames
                   were DROPPED, which is audible and is a STABILITY FAILURE
                   rather than a cost. The counter is per-session because
                   gba_audio_reset() zeroes it -- see (4) above. */
                gba_audio_stats(&aud);
                if (aud.overruns) {
                    end_reason = M16C_END_OVERRUN;
                    break;
                }

                /* THE FRAMESKIP GATE, RE-ASSERTED EVERY FRAME. skip_next_frame
                   is the gate inside update_scanline() (gpsp/video.cc:2354).
                   M16C NEVER WRITES IT. If it moves off 0 something is dropping
                   frames. */
                if (skip_next_frame != 0u) {
                    skip_seen  = skip_next_frame;
                    end_reason = M16C_END_SKIPFRAME;
                    break;
                }

                /* ---- DEMAND PAGING, SAMPLED PER FRAME ----------------
                   Every page load writes gamepak_blk_queue[entry].phy_rom
                   (gba_memory.c:2336), so the number of queue entries whose ROM
                   page changed since the previous frame IS that frame's
                   page-load count. A PURE OBSERVER. */
                page_delta = m10map_paging_delta();
                if (page_delta) rom_page_loads += page_delta;

                /* ---- THE ARENA TRIPWIRE ------------------------------
                   STEADY STATE MUST ALLOCATE NOTHING. Nothing on the render,
                   input or audio path calls malloc, so after the warm-up the
                   bump allocator's cursor must not move.

                   ***** IN A LOOPING LAUNCHER THIS IS ALSO THE EARLY WARNING
                   FOR THE BOUNDARY. ***** A session that grows the arena during
                   play cannot be rewound to its mark, so catching it here --
                   with the session named and the frame number recorded -- is
                   what keeps m16c_boundary()'s CLAIM 1 from being the first
                   place anyone hears about it. */
                arena_now = arena_used();
                if (warm_done) {
                    if (arena_now != arena_prev) {
                        arena_grow_frame = frames_emulated;
                        arena_grow_from  = arena_prev;
                        arena_grow_to    = arena_now;
                        end_reason       = M16C_END_ARENA;
                        break;
                    }
                } else if (frames_emulated >= M16C_PLAY_WARMUP) {
                    arena_after_warm = arena_now;
                    warm_done        = 1u;
                }
                arena_prev = arena_now;

                /* ---- THE IN-SESSION MAPPING PROBE --------------------
                   FATAL, and periodic rather than once. A demand-paged
                   cartridge rewrites twenty map slots on every fault, so a map
                   that was coherent at startup proves almost nothing about
                   minute 12. */
                if (frames_emulated &&
                    (frames_emulated % M16C_MAP_PROBE_EVERY) == 0u) {
                    map_probes++;
                    m10mask = m10map_probe();
                    if (m10mask) {
                        map_fail_frame = frames_emulated;
                        end_reason     = M16C_END_MAPFAIL;
                        break;
                    }
                }

                prev_keys = cur_keys;

                /* ---- THE OPERATOR'S EXIT -- NOW THE RETURN TO PICKER -
                 *
                 * ***** THE PROVEN CHORD, UNCHANGED: ALL FOUR SHOULDERS, HELD
                 * FOR 15 FRAMES. ***** The hold requirement is what stops a
                 * brush against the pad from ending the session, and the chord
                 * is one no cartridge asks the player to make.
                 *
                 * ***** WHAT CHANGED IS ONLY WHERE THIS `break` GOES. ***** In
                 * M13C it fell into a teardown and a return, and returning from
                 * _start() IS the payload exiting. Here it falls into the
                 * commit, then the boundary, then the picker. The chord, the
                 * hold, the debounce, the counter and the end_reason it sets
                 * are all byte-identical to M13C's.
                 *
                 * ***** AND IT IS STILL NOT SAVE PERMISSION. ***** end_reason =
                 * OPERATOR is ONE of the seven terms of `clean` and never a
                 * commit trigger on its own. */
                if (pad_conn &&
                    (raw_pad & M16C_EXIT_CHORD) == M16C_EXIT_CHORD) {
                    exit_hold++;
                    if (exit_hold >= M16C_EXIT_HOLD) {
                        end_reason = M16C_END_OPERATOR;
                        break;
                    }
                } else {
                    exit_hold = 0u;
                }

                /* ---- THERE IS DELIBERATELY NO DURATION TEST HERE ------
                 *
                 * ***** THIS IS WHERE M13B-5's 30-SECOND EXIT USED TO BE, AND
                 * ITS ABSENCE IS THE POINT. ***** M13B-5 read plat_time_us() at
                 * the bottom of every iteration and broke out once the window
                 * had elapsed. A launcher must not do that: hardware showed a
                 * timed window closing Metroid Fusion mid-room. The loop now
                 * leaves only by the operator's chord above, by one of the fault
                 * tripwires, or by exhausting the iteration cap -- and that cap
                 * is the EMERGENCY WATCHDOG, which end_reason is pre-loaded with
                 * so that falling out of the loop naturally is recorded as
                 * WATCHDOG rather than as a completed session. NOTHING here
                 * reads the clock, so nothing here can end the session. */
            }

            run_t1     = plat_time_us();
            elapsed_us = (run_t1 > run_t0) ? (run_t1 - run_t0) : 0u;
            arena_end  = arena_used();

            /* ============================================================
             * T3 -- ***** THE DECISIVE SAMPLE, AND IT IS UNCONDITIONAL. *****
             * ============================================================
             *
             * ***** IT IS DELIBERATELY NOT INSIDE THE `clean && modified`
             * COMMIT BRANCH. ***** That branch is a SEVEN-TERM CONJUNCTION
             * whose `end_reason` is pre-loaded with WATCHDOG, so a long session
             * that runs to the iteration cap, drops one audio grain or skips one
             * frame takes NONE of it. Those are precisely the runs this
             * measurement exists to explain. Sampling next to the commit call
             * would mean the failure cases -- the interesting ones -- produced no
             * measurement at all and the whole hardware run was wasted. The
             * bytes in gamepak_backup[] are just as real when the session ended
             * badly; whether they get WRITTEN is a separate question, asked
             * separately, and reported separately.
             *
             * It sits AFTER run_t1/arena_end for T2's reason -- so the
             * observer's own cost lands outside every figure that is graded --
             * and BEFORE the `clean` verdict, so nothing it reads can have been
             * disturbed by the commit path. The observer does not allocate (all
             * state is .bss), so taking it after arena_end cannot move the arena
             * terms either. */
            m12c_sramobs_sample(GBA_SRAMOBS_T3);

            /* A SESSION THAT ENDED BEFORE THE WARM-UP COMPLETED HAS NO STEADY
               STATE TO JUDGE. Anchoring on the end reading makes its growth read
               0, which is correct -- the verdict is then carried entirely by
               end_reason, which in that case is already one of the failures. */
            if (!warm_done) arena_after_warm = arena_end;

            /* ---- THE NUMBERS. ALL TO THE LOG, NONE TO THE SCREEN. ------- */
            gba_audio_stats(&aud);
            lru_evicts   = m10map_read(M10MAP_RD_LRUEVICTS);
            resident     = m10map_read(M10MAP_RD_RESIDENT);
            resident_exp = m10map_expected_resident();
            file_blocks  = m10map_read(M10MAP_RD_FILEBLOCKS);

            if (elapsed_us)
                fps_x100 = (unsigned)(((u64)frames_presented * 100u * 1000000u)
                                      / elapsed_us);

            /* ---- M17 TIER-1: SUBMISSION-SIDE DRIFT ----------------------- *
             * PURE ARITHMETIC OVER NUMBERS THE PAYLOAD ALREADY KEEPS. No new
             * measurement, no new state in the adapter, no new call on the
             * hot path: `aud` was filled by the gba_audio_stats() above.
             *
             * WHY IT IS THE RIGHT QUESTION. The sink runs at exactly
             * GBA_AUDIO_DST_HZ and never faster, so over `elapsed_us` of wall
             * clock a system in equilibrium must have handed the backend
             * elapsed_us * 48000 / 1e6 output frames. The signed difference
             * from that is the drift, and it is the only figure that can tell
             * a ring OSCILLATING about its target from one WALKING toward
             * saturation -- the two look identical in `overruns 0`.
             *
             * THE ARITHMETIC IS M10's, VERBATIM (apps/m10gpsp/main.c), so the
             * two milestones' drift figures are directly comparable rather
             * than merely similar.
             *
             * OVERFLOW IS BOUNDED: even a full 240-minute watchdog window is
             * 1.44e10 us, and the largest product below is 1.44e10 * 48000 =
             * 6.9e14 -- two orders inside u64. The guard is `elapsed_us`
             * non-zero, which is already a term in the clean verdict, so a
             * dead clock cannot manufacture a drift figure here either. */
            if (elapsed_us) {
                u64 expected_out = (elapsed_us * (u64)GBA_AUDIO_DST_HZ)
                                 / 1000000ULL;

                aud_drift_frames = (s64)aud.consumed - (s64)expected_out;
                aud_drift_ms     = (aud_drift_frames * 1000LL)
                                 / (s64)GBA_AUDIO_DST_HZ;
            }

            /* ***** THE VERDICT IS A CONJUNCTION AND EVERY TERM IS MEASURED.
               ***** A clean ending alone is not enough: the session must also
               have presented exactly as many frames as it emulated, dropped no
               audio, never closed the render gate, kept the cartridge mapped and
               allocated nothing in steady state. Any one failing makes the
               others unsafe to believe, so they are ANDed. `elapsed_us > 0` is a
               term in its own right -- without it a dead clock could still
               produce a clean verdict through the exit chord while reporting a
               zero duration.
               ***** SPEED IS NOT A TERM. ***** A session that emulated every
               frame, presented every frame and dropped no audio is correct
               whether it achieved 59.7 fps or 40.
               ***** THE SEVEN TERMS ARE M13C's, VERBATIM, AND THE LOOP DOES NOT
               ADD AN EIGHTH. ***** Returning to the picker is not a term,
               because returning to the picker is not evidence about the session
               that just ended. */
            clean = (end_reason == M16C_END_OPERATOR &&
                     elapsed_us > 0u &&
                     frames_emulated == frames_presented &&
                     frames_emulated > 0u &&
                     aud.overruns == 0u &&
                     skip_seen == 0u &&
                     arena_end == arena_after_warm);

            printf("M16C: END REASON %u (0 RETIRED/never, 1 OPERATOR=RETURN TO "
                   "LUAPORT, 2 WATCHDOG, 3 OVERRUN, 4 SKIPFRAME, 5 MAPFAIL, "
                   "6 ARENA) -- 1 IS THE ONLY CLEAN ENDING\n", end_reason);
            printf("M16C: frames emulated %u, presented %u, flips rejected %u\n",
                   frames_emulated, frames_presented, flip_fail);
            printf("M16C: elapsed %u ms, present rate %u.%02u fps\n",
                   (unsigned)(elapsed_us / 1000u), fps_x100 / 100u,
                   fps_x100 % 100u);
            printf("M16C: pad reads OK %u, BAD %u, input edges %u, IRQ union "
                   "0x%08X\n", pad_reads_ok, pad_reads_bad, input_edges,
                   irq_union);
            printf("M16C: audio grains %u, underruns %llu, overruns %llu, peak "
                   "%u, fill %u\n", grains_total,
                   (unsigned long long)aud.underruns,
                   (unsigned long long)aud.overruns, aud.peak, aud.fill);

            /* ===== M17 TIER-1 AUDIO ENVELOPE. MEASUREMENT ONLY. ===========
             * EVERY VALUE BELOW ALREADY EXISTED AND WAS ALREADY BEING
             * MAINTAINED. M17 adds no counter, no .bss, no behaviour and no
             * verdict term -- it prints what the payload was already keeping
             * and, until now, throwing away at session end.
             *
             * ***** FILL_MAX IS THE DECISIVE NUMBER. ***** `overruns 0` with
             * fill_max 1800 and `overruns 0` with fill_max 8100 are the SAME
             * LOG LINE without it, and they are radically different systems:
             * the second is one frame away from a session-fatal overrun. The
             * line above cannot distinguish them because it reports only the
             * instantaneous fill at the moment the session ended.
             *
             * READING fill_min: gba_audio_stats() normalises the 0xFFFFFFFF
             * seed to 0 when no grain was ever submitted, so a 0 here means
             * "never started" and NOT "starved to empty". Read it together
             * with the grain count above, never alone.
             *
             * ***** NOTHING HERE IS A TERM IN THE VERDICT. ***** The clean
             * conjunction above is M13C's seven, unchanged. Underruns stay
             * deliberately outside it: with a blocking sink and no rate servo
             * they are the CORRECTIVE SIGNAL of the pacing loop, not a
             * defect, and grading them would fail a correctly behaving
             * system. M17 characterises that loop; it does not touch it. */
            printf("M17: audio fill min %u, max %u of %u frames (ring %u ms, "
                   "target fill %u) -- fill_max IS THE MARGIN\n",
                   aud.fill_min, aud.fill_max,
                   (unsigned)GBA_AUDIO_RING_FRAMES,
                   (unsigned)((GBA_AUDIO_RING_FRAMES * 1000u)
                              / GBA_AUDIO_DST_HZ),
                   (unsigned)GBA_AUDIO_TARGET_FILL);

            /* THE RING BIJECTION. Every output frame pushed is eventually
               either drained or dropped, so produced - consumed must equal
               the frames still resident. A mismatch means the ring's own
               bookkeeping is wrong, which would make every other audio figure
               in this report unsafe to believe. Printed as a cross-check on
               the instrument itself, not on the audio. */
            printf("M17: ring produced %llu, consumed %llu, residue %llu "
                   "(REQUIRE residue == fill %u)\n",
                   (unsigned long long)aud.produced,
                   (unsigned long long)aud.consumed,
                   (unsigned long long)(aud.produced - aud.consumed),
                   aud.fill);

            /* ***** A DEAD PORT IS SILENT AND READS PERFECTLY CLEAN. *****
               The drain removes frames from the ring BEFORE it inspects the
               submit result, so a backend rejecting every grain produces
               overruns 0, underruns 0, a clean verdict and no sound at all.
               plat_audio_errors() is the ONLY value in the payload that can
               distinguish that case, and nothing read it before M17.
               BOTH ARE PAYLOAD-LIFETIME, NOT SESSION-LIFETIME: the port is a
               property of the process and is never closed between games, so
               these two accumulate across every session and are expected to
               exceed this session's grain count. That is correct, not a
               leak. */
            printf("M17: backend grains submitted %llu, submit errors %llu "
                   "(payload-lifetime, REQUIRE errors 0)\n",
                   (unsigned long long)plat_audio_submitted(),
                   (unsigned long long)plat_audio_errors());

            /* Sign is carried by an explicit character and the magnitude is
               printed unsigned, because a negated s64 formatted through an
               unsigned conversion would otherwise print as a huge positive
               number. NEGATIVE means the backend consumed fewer frames than
               the 48 kHz clock called for -- the emulator ran slow. POSITIVE
               means it ran ahead, which is the direction that ends in a
               fatal overrun. */
            printf("M17: A/V DRIFT (SUBMISSION SIDE) -- audio %s%llu output "
                   "frames (%s%llu ms) vs the %u Hz clock over %llu us\n",
                   (aud_drift_frames < 0) ? "-" : "+",
                   (unsigned long long)((aud_drift_frames < 0)
                                        ? -aud_drift_frames
                                        : aud_drift_frames),
                   (aud_drift_ms < 0) ? "-" : "+",
                   (unsigned long long)((aud_drift_ms < 0) ? -aud_drift_ms
                                                           : aud_drift_ms),
                   (unsigned)GBA_AUDIO_DST_HZ,
                   (unsigned long long)elapsed_us);
            printf("M16C: ROM page loads %u, LRU evictions %u, resident %u of "
                   "%u (expected %u)\n", rom_page_loads, lru_evicts, resident,
                   file_blocks, resident_exp);
            printf("M16C: in-session mapping probes %u every %u frames\n",
                   map_probes, (unsigned)M16C_MAP_PROBE_EVERY);
            printf("M16C: arena before %u, after warm-up %u, at end %u, "
                   "steady-state growth %u (REQUIRE 0)\n",
                   (unsigned)arena_play, (unsigned)arena_after_warm,
                   (unsigned)arena_end,
                   (unsigned)(arena_end - arena_after_warm));
            printf("M16C: skip_next_frame %u (require 0)\n", skip_seen);

            if (end_reason == M16C_END_MAPFAIL)
                printf("M16C: ***** THE CARTRIDGE MAP WENT INCOHERENT AT FRAME "
                       "%u, MASK 0x%08X *****\n", map_fail_frame, m10mask);
            if (end_reason == M16C_END_ARENA)
                printf("M16C: ***** THE ARENA GREW AT FRAME %u: %u -> %u "
                       "*****\n", arena_grow_frame, (unsigned)arena_grow_from,
                       (unsigned)arena_grow_to);

            /* =============================================================
             * step 456 -- THE COMMIT. ***** STRICTLY BEFORE THE BOUNDARY.
             * =============================================================
             *
             * ***** IT IS GATED ON `clean`, AND THAT GATE IS THE WHOLE SAFETY
             * ARGUMENT. ***** A session that ended because the cartridge mapping
             * went incoherent has a gamepak_backup whose contents nothing can
             * vouch for, and writing it would overwrite a KNOWN GOOD save with
             * bytes produced by a machine that was already misbehaving. The
             * existing file is always the safer thing to keep.
             *
             * ***** AND IT IS GATED ON `modified`, WHICH IS M12C's POST-RESTORE
             * COMPARISON AND NOT gba_save_is_dirty(). ***** gba_save_is_dirty()
             * measures against the pre-restore all-0xFF latch and would read
             * DIRTY on every restored session. This comparison is against the
             * bytes as they stood when play began, so it answers the only
             * question worth asking: DID THE GAME CHANGE THE SAVE DURING THIS
             * SESSION. A session that changed nothing writes nothing and opens NO
             * WINDOW.
             *
             * ***** THIS STEP MUST PRECEDE gba_session_end(), AND IN THIS FILE
             * THAT IS STRUCTURAL RATHER THAN CONVENTIONAL. ***** Step 4 of
             * gba_session_end() is gba_save_unlatch(), and gba_session.c:115-126
             * is explicit: the commit path builds /savedata0/gba/<ID>.sav from
             * the identity the unlatch destroys, and an out-of-order controller
             * gets GBA_SAVEFILE_ENOID -- a refusal rather than a misfile, but the
             * save is still lost. m16c_boundary() is the ONLY caller of
             * gba_session_end() in this file and it is reached from exactly two
             * places: m16c_session_fatal(), which is only ever called BEFORE this
             * step, and the end of the loop body BELOW this step. There is no
             * path on which the unlatch precedes the commit. */
            ext->step = 456;

            gba_restore_finish();
            end_hash = gba_restore_exit_hash();
            modified = gba_restore_modified();

            printf("M16C: START HASH 0x%08X, END HASH 0x%08X, DIRTY %s\n",
                   start_hash, end_hash, modified ? "YES" : "NO");

            if (!clean) {
                klog("LUAport M16C: ***** NOTHING IS COMMITTED -- THE SESSION "
                     "DID NOT END CLEANLY. ***** A save is only as trustworthy "
                     "as the session that produced it. Whatever is already on "
                     "disk is left exactly as it was\n");
            } else if (!modified) {
                klog("LUAport M16C: ***** THE SAVE DID NOT CHANGE, SO NO WINDOW "
                     "WAS OPENED. ***** Rewriting an unchanged region would put "
                     "the console through a mount/unmount cycle to replace a "
                     "good file with an identical one\n");
            } else {
                commit_tried = 1;

                /* gba_save_finish() re-measures the writer's OWN exit hash.
                   Called explicitly rather than left to gba_save_is_dirty()'s
                   self-call so the number the log prints is provably the one the
                   refusal ladder acted on. */
                gba_save_finish();

                klog("LUAport M16C: the game modified its save. Committing. The "
                     "window is opened now, held for exactly two file writes, "
                     "and closed on every path. The .sav goes first and the .hdr "
                     "LAST -- the header is the commit marker, and its payload "
                     "hash is what lets a later session detect a torn write\n");

                commit_rc    = gba_savefile_commit();
                sf_latch     = gba_savefile_latch();
                commit_bytes = gba_savefile_read(GBA_SAVEFILE_RD_SAV_BYTES);
                commit_pay   = gba_savefile_read(GBA_SAVEFILE_RD_PAYFNV);

                printf("M16C: gba_savefile_commit() returned %d, latch "
                       "0x%08X\n", commit_rc, sf_latch);
                printf("M16C: begin rc %d, mkdir rc %d, end rc %d\n",
                       (int)gba_savefile_read(GBA_SAVEFILE_RD_BEGIN_RC),
                       (int)gba_savefile_read(GBA_SAVEFILE_RD_MKDIR_RC),
                       (int)gba_savefile_read(GBA_SAVEFILE_RD_END_RC));
                printf("M16C: .sav %u bytes, .hdr %u bytes, payload FNV "
                       "0x%08X, chunks %u\n", commit_bytes,
                       gba_savefile_read(GBA_SAVEFILE_RD_HDR_BYTES), commit_pay,
                       gba_savefile_read(GBA_SAVEFILE_RD_CHUNKS));

                if (commit_rc == GBA_SAVEFILE_OK) {
                    commit_state = 1u;
                    m16c_commits_done++;
                    klog("LUAport M16C: the save was committed and the "
                         "read-only mount is back\n");
                } else if (commit_rc == GBA_SAVEFILE_ENOTDIRTY) {
                    /* ***** THE ONE DISAGREEMENT THE TWO DIRTY MODELS CAN HAVE.
                       ***** M12C's comparison says the bytes changed since play
                       began; M12B's writer compares against the pre-restore
                       all-0xFF latch and says they did not. Both are right: the
                       game ERASED its save back to the blank state. Persisting
                       that would destroy a good file on disk, so the writer's
                       refusal is CORRECT and is left to stand. */
                    klog("LUAport M16C: ***** THE WRITER REFUSED: the region is "
                         "back to its blank state. ***** The game changed the "
                         "save during this session and then erased it. Writing "
                         "blank over a good file would destroy it, so nothing "
                         "was written and the existing save is intact\n");
                } else {
                    commit_state = 2u;
                    printf("M16C: ***** THE COMMIT FAILED WITH %d *****\n",
                           commit_rc);
                }
            }

            /* =============================================================
             * step 456b -- THE COMMIT-SIDE SRAM CAPTURE
             * =============================================================
             *
             * ***** THIS BLOCK READS. IT DOES NOT DECIDE ANYTHING. ***** It runs
             * on EVERY path out of the commit step above -- committed, refused,
             * not-modified and not-clean alike -- because "what did the game
             * leave in SRAM" and "was it written to disk" are different
             * questions and conflating them is what made the previous hardware
             * runs unreadable.
             *
             * IT IS PLACED AFTER THE COMMIT ATTEMPT SO THE WRITER'S OWN FIGURES
             * ARE POPULATED WHEN THERE ARE ANY. gba_save_exit_hash() is zero
             * until gba_save_finish() has run, and gba_save_finish() is called
             * only on the commit path. Reading it here means it is REAL when a
             * commit happened and honestly zero when none did -- and the screen
             * labels it as the writer's, never as the session's. The session's
             * own full-array hashes are start_hash/end_hash, which
             * gba_restore_finish() computes UNCONDITIONALLY and which are
             * therefore always valid.
             *
             * NOTHING HERE PERTURBS THE RUN. gba_save_class(), _region_bytes(),
             * _baseline_hash(), _exit_hash() and _region_hash() are pure reads
             * (gba_save.c:399-417, :531-549); gba_restore_live_class() is
             * documented read-only; and the observer holds gamepak_backup through
             * a `const` pointer. backup_type is READ, never written --
             * read_backup/write_backup/read_eeprom/write_eeprom all MUTATE it and
             * NONE of them is called.
             *
             * ***** IT IS ALSO READ BEFORE THE BOUNDARY, WHICH IS THE ONLY
             * PLACE IT COULD BE. ***** Step 6 of gba_session_end() is
             * m12c_sramobs_reset(), so every figure below would read zero if this
             * block ran after it. */
            cap_taken = m12c_sramobs_read(GBA_SRAMOBS_RD_TAKEN
                                          + GBA_SRAMOBS_T3);

            cap_low_nonff   = m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_NONFF
                                                + GBA_SRAMOBS_T3);
            cap_high_nonff  = m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_NONFF
                                                + GBA_SRAMOBS_T3);
            cap_low_fnv     = m12c_sramobs_read(GBA_SRAMOBS_RD_LOW_FNV
                                                + GBA_SRAMOBS_T3);
            cap_high_fnv    = m12c_sramobs_read(GBA_SRAMOBS_RD_HIGH_FNV
                                                + GBA_SRAMOBS_T3);
            cap_bits        = m12c_sramobs_read(GBA_SRAMOBS_RD_BITS);

            cap_live_class  = gba_restore_live_class();
            cap_raw_type    = gba_restore_read(GBA_RESTORE_RD_BACKUP_TYPE);
            cap_class       = gba_save_class();
            cap_region      = gba_save_region_bytes();
            cap_region_hash = gba_save_region_hash();
            cap_wr_base     = gba_save_baseline_hash();
            cap_wr_exit     = gba_save_exit_hash();

            /* ---- THE DECISION RULE, APPLIED EXACTLY ONCE -----------------
             *
             * ***** ORDER MATTERS AND IT IS NOT ARBITRARY. ***** "NOT TAKEN" is
             * tested FIRST because an unsampled slot reads as all-zero, and zero
             * high-half bytes is indistinguishable from a measured blank upper
             * half -- which would REFUTE the hypothesis on the strength of a run
             * that never measured it. "WRONG FAMILY" is tested SECOND because if
             * the cartridge is not SRAM at all then the 32 KB region, the 64 KB
             * window and the upper/lower split are all the wrong frame and any
             * verdict drawn inside it would be meaningless. */
            if (!cap_taken) {
                cap_verdict = M16C_CAP_NOTTAKEN;
            } else if (cap_live_class != GBA_SAVE_SRAM) {
                cap_verdict = M16C_CAP_FAMILY;
            } else if (cap_high_nonff > 0u && cap_region == 32768u) {
                cap_verdict = M16C_CAP_TRUNCATED;
            } else if (cap_high_nonff == 0u && cap_low_nonff > 0u) {
                cap_verdict = M16C_CAP_REFUTED;
            } else {
                cap_verdict = M16C_CAP_INCONCLUSIVE;
            }

            printf("M16C: SRAM CAPTURE T3 -- LOW nonFF %u hash 0x%08X, "
                   "HIGH nonFF %u hash 0x%08X, bits 0x%02X, taken %u\n",
                   cap_low_nonff, cap_low_fnv, cap_high_nonff, cap_high_fnv,
                   cap_bits, cap_taken);
            printf("M16C: SRAM CAPTURE -- live class %u, raw backup_type %u, "
                   "writer class %u, commit region %u bytes\n",
                   cap_live_class, cap_raw_type, cap_class, cap_region);
            printf("M16C: SRAM CAPTURE -- region hash 0x%08X, writer base "
                   "0x%08X, writer exit 0x%08X, session base 0x%08X, session "
                   "exit 0x%08X\n", cap_region_hash, cap_wr_base, cap_wr_exit,
                   start_hash, end_hash);
            printf("M16C: SRAM CAPTURE -- commit rc %d (%s), bytes %u, "
                   "payload FNV 0x%08X\n",
                   commit_rc, m16c_commit_text(commit_rc), commit_bytes,
                   commit_pay);

            /* ---- THE SESSION LEDGER ------------------------------------
             *
             * ***** WRITTEN HERE, WHERE EVERY FIGURE IS FINAL AND STILL IN
             * SCOPE, AND BEFORE ANY PATH CAN LEAVE THIS BLOCK. ***** These are
             * the only values that outlive the session, and they exist for one
             * purpose: the payload ledger at step 459 has to be able to say what
             * happened in the LAST session after its locals are gone. Note what
             * is recorded -- a save ID, a game code and a ROM hash, all
             * CONTENT-DERIVED. Never a pointer: the arena hands back identical
             * addresses every session, so an address would prove nothing about
             * isolation. That is M16-1's finding. */
            m16c_rec.end_reason      = end_reason;
            m16c_rec.clean           = clean ? 1u : 0u;
            m16c_rec.modified        = modified ? 1u : 0u;
            m16c_rec.commit_state    = commit_state;
            m16c_rec.restore_state   = restore_state;
            m16c_rec.frames_emulated = frames_emulated;
            m16c_rec.frames_presented= frames_presented;
            m16c_rec.elapsed_ms      = (unsigned)(elapsed_us / 1000u);
            m16c_rec.commit_bytes    = commit_bytes;
            m16c_rec.restored_bytes  = restored_bytes;

            /* ---- THE SCREEN. THE ONLY CHANNEL THAT DOES NOT NEED UDP. ----
             *
             * ***** klog() AND printf() ABOVE ARE NOT A GUARANTEED CHANNEL.
             * ***** runtime/shim.c:21-22 returns early unless a UDP socket
             * resolved, so every line above can vanish with no indication. The
             * capture is the whole point of the investigation it belongs to, so
             * it is ALSO drawn -- photographable, and independent of the network.
             *
             * ***** IT IS DRAWN ONLY WHEN THE SESSION IS UNCLEAN. ***** The
             * measurement above is ALWAYS taken and ALWAYS logged -- nothing
             * about the capture, the hashes, the truncation verdict or the
             * T1/T2/T3 evidence is conditional. Only the SCREEN is. A clean
             * session is a player finishing a game, and a player must not be
             * shown fifteen SRAM forensics numbers before being told their save
             * was written; they go straight to the concise summary. An unclean
             * session is exactly the case whose SRAM state is worth seeing, so
             * the screen survives intact on every path where something actually
             * went wrong. CIRCLE dismisses it, as on every other screen. */
            if (!clean && m16c_video_up && m16c_pad_up) {
                unsigned f;
                u32 prev = 0u;
                const char *upper_line;
                const char *verdict_line;
                const char *file_line;

                upper_line = !cap_taken
                           ? "SRAM CAPTURE: NOT MEASURED"
                           : (cap_high_nonff > 0u
                                ? "SRAM CAPTURE: UPPER DATA PRESENT"
                                : "SRAM CAPTURE: UPPER DATA BLANK");

                switch (cap_verdict) {
                case M16C_CAP_TRUNCATED:
                    verdict_line = "UPPER-HALF TRUNCATION CONFIRMED"; break;
                case M16C_CAP_REFUTED:
                    verdict_line = "UPPER-HALF THEORY REFUTED";       break;
                case M16C_CAP_FAMILY:
                    verdict_line = "SAVE FAMILY / REGION MISMATCH";   break;
                case M16C_CAP_NOTTAKEN:
                    verdict_line = "CAPTURE NOT TAKEN -- NO CONCLUSION"; break;
                default:
                    verdict_line = "INCONCLUSIVE -- NO SAVE BYTES WRITTEN";
                    break;
                }

                /* ***** THE FILE STATUS IS REPORTED SEPARATELY FROM THE
                   CAPTURE, ALWAYS. ***** "We measured the bytes" and "the bytes
                   reached the disk" are independent facts, and the previous
                   investigation lost a whole hardware run to the two being
                   conflated. */
                if (!clean)         file_line = "COMMIT SKIPPED / SESSION UNCLEAN";
                else if (!modified) file_line = "COMMIT SKIPPED / SAVE UNCHANGED";
                else if (commit_state == 1u)
                                    file_line = "FILE UPDATED";
                else                file_line = "COMMIT ATTEMPTED / FILE NOT UPDATED";

                for (f = 0; f < M16C_CAPTURE_FRAMES; f++) {
                    u32 pressed;
                    int y;

                    (void)plat_pad_read(&pad);
                    pressed = pad.buttons & ~prev;
                    prev    = pad.buttons;
                    if (pressed & PAD_CIRCLE) break;

                    m16c_masthead();
                    draw_str(m16c_surface, 4, 18,
                             "COMMIT-SIDE SRAM CAPTURE  T3", M16C_C_SEL);
                    draw_hline(m16c_surface, 28, 4, UI_W - 4, M16C_C_INDIGO);

                    y = 34;

                    draw_str(m16c_surface, 4, y, "SAVE CLASS", M16C_C_MUTED);
                    ln_reset(&l); ln_puts(&l, m16c_class_text(cap_class));
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "LIVE TYPE", M16C_C_MUTED);
                    ln_reset(&l);
                    ln_puts(&l, m16c_class_text(cap_live_class));
                    ln_puts(&l, "   RAW ");
                    ln_udec(&l, cap_raw_type);
                    draw_str(m16c_surface, 136, y, l.b,
                             (cap_live_class == GBA_SAVE_SRAM) ? M16C_C_OK
                                                               : M16C_C_WARN);
                    y += 10;

                    draw_str(m16c_surface, 4, y, "COMMIT REGION", M16C_C_MUTED);
                    ln_reset(&l); ln_udec(&l, cap_region);
                    ln_puts(&l, " BYTES");
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 14;

                    draw_str(m16c_surface, 4, y, "LOW NONFF", M16C_C_MUTED);
                    ln_reset(&l); ln_udec(&l, cap_low_nonff);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    /* THE ONE NUMBER THE CAPTURE EXISTS TO PRODUCE. It is the
                       only row that changes colour on its value, so a photograph
                       of this screen is readable at a glance. */
                    draw_str(m16c_surface, 4, y, "HIGH NONFF", M16C_C_MUTED);
                    ln_reset(&l); ln_udec(&l, cap_high_nonff);
                    draw_str(m16c_surface, 136, y, l.b,
                             (cap_high_nonff > 0u) ? M16C_C_SEL : M16C_C_TEXT);
                    y += 14;

                    draw_str(m16c_surface, 4, y, "LOW HASH", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, cap_low_fnv);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "HIGH HASH", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, cap_high_fnv);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "REGION HASH", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, cap_region_hash);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "FULL BASE HASH", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, start_hash);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "FULL EXIT HASH", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, end_hash);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "WRITER BASE", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, cap_wr_base);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "WRITER EXIT", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, cap_wr_exit);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 14;

                    draw_str(m16c_surface, 4, y, "COMMIT RC", M16C_C_MUTED);
                    ln_reset(&l); ln_dec(&l, commit_rc);
                    ln_puts(&l, "  ");
                    ln_puts(&l, m16c_commit_text(commit_rc));
                    draw_str(m16c_surface, 136, y, l.b,
                             (commit_rc == GBA_SAVEFILE_OK) ? M16C_C_OK
                                                            : M16C_C_ERR);
                    y += 10;

                    draw_str(m16c_surface, 4, y, "COMMITTED BYTES",
                             M16C_C_MUTED);
                    ln_reset(&l); ln_udec(&l, commit_bytes);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 10;

                    draw_str(m16c_surface, 4, y, "PAYLOAD FNV", M16C_C_MUTED);
                    ln_reset(&l); ln_hex32(&l, commit_pay);
                    draw_str(m16c_surface, 136, y, l.b, M16C_C_TEXT); y += 12;

                    draw_hline(m16c_surface, y, 4, UI_W - 4, M16C_C_INDIGO);
                    y += 8;

                    draw_str(m16c_surface, 4, y, upper_line,
                             (cap_taken && cap_high_nonff > 0u) ? M16C_C_SEL
                                                                : M16C_C_TEXT);
                    y += 10;
                    draw_str(m16c_surface, 4, y, verdict_line, M16C_C_TEXT);
                    y += 10;
                    draw_str(m16c_surface, 4, y, file_line,
                             (commit_state == 1u) ? M16C_C_OK : M16C_C_WARN);

                    draw_str(m16c_surface, 4, UI_H - 12, "( O ) CONTINUES",
                             M16C_C_MUTED);

                    m16c_flip();
                }
            }

            /* ---- THE dbg[] VERDICT, PACKED ONCE -------------------------
             *
             * struct ext_args has exactly eight u64s (runtime/core.h:158).
             *
             *   dbg[3]  WAS the userId INPUT, and it has TWO readers: step 444
             *           (plat_savedata_init) and step 446 (plat_pad_init). BOTH
             *           run in the bring-up, ABOVE the loop, and neither is ever
             *           re-executed, so by the time any session writes here the
             *           input has been fully consumed and overwriting it costs
             *           nothing -- on session 1 and on session 8 alike. The
             *           packing is M13C's layout EXACTLY, so every decoder
             *           written for the frozen launcher reads every field here
             *           correctly.
             *
             *     [3:0]   restore  0 NONE      1 LOADED   2 REJECTED
             *     [7:4]   commit   0 NOT NEEDED 1 UPDATED 2 FAILED
             *     [8]     dirty
             *     [11:9]  flow     0 EXITED (no launch)   1 SESSION COMPLETE
             *     [14:12] capture  0 NOT TAKEN  1 TRUNCATED  2 REFUTED
             *                      3 WRONG FAMILY  4 INCONCLUSIVE
             *     [15]    upper half held non-0xFF data at T3
             *     [39:16] restored bytes
             *     [63:40] committed bytes
             *
             *   ***** IT DESCRIBES THE SESSION THAT JUST ENDED, AND IT IS
             *   OVERWRITTEN BY THE NEXT ONE. ***** In M13C this was the final
             *   word on the payload, because there was exactly one session. Here
             *   it is a rolling record of the LATEST session, which is why the
             *   cross-session facts -- how many games ran, how many were clean,
             *   how many committed -- are reported separately by the ledger at
             *   step 459 and never packed into these bits. A single set of dbg
             *   fields cannot describe eight games, and pretending otherwise
             *   would be worse than not trying. */
            ext->dbg[0] = (u64)frames_emulated;
            ext->dbg[1] = (u64)frames_presented;
            ext->dbg[2] = (u64)(elapsed_us / 1000u);
            ext->dbg[4] = (u64)end_reason;
            ext->dbg[5] = (u64)(u32)start_hash;
            ext->dbg[6] = (u64)(u32)end_hash;
            ext->dbg[7] = (u64)fps_x100;
            ext->dbg[3] = (u64)restore_state
                        | ((u64)commit_state << 4)
                        | ((u64)(modified ? 1u : 0u) << 8)
                        | ((u64)1u << 9)             /* flow 1 -- COMPLETE   */
                        | (((u64)cap_verdict & 7u) << 12)
                        | ((u64)((cap_taken && cap_high_nonff > 0u) ? 1u : 0u)
                           << 15)
                        | ((u64)restored_bytes << 16)
                        | ((u64)commit_bytes   << 40);

            /* ***** THE READ-ONLY MOUNT IS THE ONE FAILURE THAT OUTRANKS
               EVERYTHING, INCLUDING THE LOOP. ***** GBA_SAVEFILE_ERESTORE means
               the bytes committed but the container is still mounted read-write,
               and the operator may not be able to close the game from the PS
               menu. Its remedy is a POWER CYCLE, so it is RUNTIME-fatal: there
               is no version of "return to the picker" that is safe or even
               useful when the console cannot be exited, and offering another
               game would invite a SECOND read-write mount on top of the one that
               did not close. Reported ahead of everything else, with its own
               status, because nothing on a summary screen would convey it. */
            if (commit_tried && commit_rc == GBA_SAVEFILE_ERESTORE) {
                klog("LUAport M16C: ***** THE READ-ONLY MOUNT DID NOT COME "
                     "BACK. ***** The save was committed, but /savedata0 is "
                     "still mounted read-write. THE GAME MAY NOT CLOSE FROM THE "
                     "PS MENU -- POWER CYCLE THE CONSOLE. The picker is NOT "
                     "coming back: starting another game would open a second "
                     "write window on top of one that never closed\n");
                ext->dbg[4] = (u64)(u32)(s32)commit_rc;
                m16c_runtime_fatal(ext, M16C_RO_NOT_BACK, M16C_H_SAVE_WRITE,
                                   "THE SAVE WAS WRITTEN BUT STORAGE DID NOT "
                                   "CLOSE.",
                                   "THE GAME MAY NOT CLOSE FROM THE PS MENU.",
                                   "POWER CYCLE THE CONSOLE.");
                return;
            }

            /* =============================================================
             * step 457 -- THE END-OF-SESSION SUMMARY
             * =============================================================
             *
             * ***** SHORT, USER-FACING, AND CARRYING EXACTLY ONE FACT THE
             * OPERATOR CANNOT SEE ANY OTHER WAY: WHETHER THEIR SAVE WAS WRITTEN.
             * ***** Every other figure -- frames, duration, rate, grains, page
             * loads, hashes, arena -- is in the UDP log and in the notification.
             * Putting them here would turn a two-second confirmation back into
             * the diagnostic dump this launcher exists to remove.
             *
             * ***** THE UNCLEAN CASE IS NOT SHOWN HERE. ***** It falls through
             * to the session-fatal screen below, because a session that ended on
             * a watchdog, an audio overrun, a frameskip, a lost map or arena
             * growth is a real fault and must not be dressed up as SESSION
             * COMPLETE.
             *
             * ***** THE ONE LINE THIS MILESTONE ADDS TELLS THE OPERATOR WHERE
             * THEY ARE GOING. ***** In M13C this screen was the last thing before
             * the payload exited, so it needed no navigation. Here the picker is
             * about to come back, and a summary that did not say so would leave
             * the operator wondering whether LUAport had closed. */
            if (clean) {
                m16c_sessions_clean++;
                m16c_last_session_status = M16C_OK;

                ext->step = 457;
                prev_buttons = 0u;
                for (frame = 0; frame < M16C_SUMMARY_FRAMES; frame++) {
                    u32 pressed;

                    (void)plat_pad_read(&pad);
                    pressed      = pad.buttons & ~prev_buttons;
                    prev_buttons = pad.buttons;
                    if (pressed & PAD_CIRCLE) break;

                    m16c_masthead();

                    /* ***** THE MASTHEAD RULE, WHICH THIS SCREEN ALONE LACKED.
                       ***** m16c_draw_empty(), m16c_draw_loading(), the failure
                       screen and the details screen all follow m16c_masthead()
                       with exactly this line, so the title bar is closed by an
                       edge on every screen the operator sees. The clean summary
                       was the single exception, and the inconsistency was most
                       visible precisely where it mattered least to the code and
                       most to the eye: the last screen of a successful session.

                       IT IS A RULE AND NOTHING ELSE. No value, no counter and no
                       diagnostic is added here -- M16C_SHOW_AUDIO_DIAG is still
                       0 and the block below it is still compiled out, so the
                       success screen keeps showing exactly the four facts it
                       showed before. Drawing an edge is not exposing telemetry.
                         masthead title    y 4, bottom 4 + 8 = 12
                         this rule         y 16 -- 4 px below the title
                         first content     y 60 -- 44 px clear below the rule */
                    draw_hline(m16c_surface, 16, 4, UI_W - 4, M16C_C_INDIGO);

                    /* The cartridge's OWN title when it has one, the filename
                       when it does not. Never both, and never an empty row. */
                    if (title[0])
                        draw_str(m16c_surface, COL_CURSOR, 60, title,
                                 M16C_C_SEL);
                    else {
                        m16c_display_name(lab, (unsigned)sizeof(lab),
                                          m16c_chosen_name);
                        draw_str(m16c_surface, COL_CURSOR, 60, lab,
                                 M16C_C_SEL);
                    }

                    /* ***** THE ONE SCREEN THAT HAS EARNED GREEN. ***** The
                       session ran to a clean operator exit; that is the success
                       state this colour is reserved for. "SAVE UPDATED" joins it
                       because bytes really reached the disk, while "SAVE
                       UNCHANGED" is a neutral, correct outcome and stays in the
                       reading colour rather than claiming a success that did not
                       happen. */
                    draw_str(m16c_surface, COL_CURSOR, 76, "SESSION COMPLETE",
                             M16C_C_OK);

                    draw_str(m16c_surface, COL_CURSOR, 100,
                             (commit_state == 1u) ? "SAVE UPDATED"
                                                  : "SAVE UNCHANGED",
                             (commit_state == 1u) ? M16C_C_OK : M16C_C_TEXT);

                    draw_str(m16c_surface, COL_CURSOR, 124,
                             "RETURNING TO PICKER", M16C_C_MUTED);

/* ***** POLISH-0: EVERYTHING BETWEEN THIS #if AND ITS #endif IS DRAWING. *****
   See the M16C_SHOW_AUDIO_DIAG block near the top of this file. With the flag
   at its default 0 the four lines above ARE the entire success screen; with it
   at 1 the block below is M17's, unchanged.

   ***** STATED PRECISELY, BECAUSE "NOTHING IS MEASURED HERE" WOULD BE ONE WORD
   TOO STRONG. ***** No stats call, no counter increment, no arithmetic and no
   log line is inside this region. What IS inside are two READS of counters that
   are incremented elsewhere -- plat_audio_submitted() and plat_audio_errors().
   Both are side-effect-free accessors, both keep counting whether or not this
   block compiles, and both are ALREADY printed unconditionally by the reporting
   block above (the "submitted / errors" printf). So compiling this out costs
   the LOG nothing: every figure on this screen survives in the UDP capture and
   in the notification exactly as it did on the run that proved M17. */
#if M16C_SHOW_AUDIO_DIAG
                    /* ========== M17 TIER-1B: THE AUDIO ENVELOPE, ON SCREEN
                     * ==========================================================
                     *
                     * ***** PRESENTATION ONLY. EVERY VALUE BELOW WAS ALREADY
                     * COMPUTED. ***** `aud` was filled by the single
                     * gba_audio_stats() call at step 457's reporting block and
                     * aud_drift_frames/aud_drift_ms by the arithmetic beside
                     * it. NOTHING HERE MEASURES ANYTHING. There is no second
                     * sampling path, no new counter, no new .bss, and no call
                     * into the audio layer from this loop -- which matters,
                     * because a per-frame stats call inside a 600-frame
                     * display loop would be a real cost for a screen whose
                     * only job is to hold still.
                     *
                     * ***** WHY IT IS SAFE TO READ HERE. ***** This loop runs
                     * BEFORE m16c_boundary(), which is the sole owner of
                     * gba_session_end() -> gba_audio_reset(). The ring, the
                     * stats and the drift therefore still describe THE SESSION
                     * THAT JUST ENDED. No ordering was moved to achieve that:
                     * the commit at step 456 and the boundary below both keep
                     * their existing positions, and this block was inserted
                     * into the screen that already sat between them.
                     *
                     * ***** THE GLYPH TABLE HAS NO PERCENT SIGN. *****
                     * runtime/gfx.h:36-41 renders '%' as a BLANK CELL and
                     * reports no error, so every label here is confined to
                     * A-Z, 0-9, space, '.', '-', '+', '(' and ')'.
                     *
                     * ***** NO VERDICT IS RENDERED. ***** The colours below
                     * are a reading aid, not a judgement: `clean` was decided
                     * above from M13C's seven terms and is not consulted or
                     * modified here. UNDERRUN IS DELIBERATELY NEVER COLOURED
                     * AS A FAULT -- with a blocking sink and no rate servo it
                     * is the corrective signal of the pacing loop, and
                     * flagging it on screen would teach the operator to read a
                     * correctly behaving system as broken. */
                    draw_str(m16c_surface, COL_CURSOR, 148, "AUDIO DIAG",
                             M16C_C_INFO);

                    ln_reset(&l); ln_puts(&l, "FILL MIN  ");
                    ln_udec(&l, aud.fill_min);
                    draw_str(m16c_surface, COL_CURSOR, 162, l.b, M16C_C_TEXT);

                    /* ***** FILL MAX IS THE ONE NUMBER THIS WHOLE MILESTONE
                       TURNS ON. ***** `overruns 0` with fill_max 1800 and
                       `overruns 0` with fill_max 8100 are the same verdict and
                       radically different systems. But WHICH fill_max is
                       actually close to the edge is EXACTLY THE UNKNOWN M17
                       EXISTS TO MEASURE, so it is rendered NEUTRALLY: no
                       threshold, no colour change, no implied margin. Colouring
                       it against a number M17 has not yet established would
                       manufacture a finding instead of observing one, and the
                       first hardware read-out would be interpreted through a
                       guess. Observe the real fill envelope first; any margin
                       is derived from that evidence afterwards. */
                    ln_reset(&l); ln_puts(&l, "FILL MAX  ");
                    ln_udec(&l, aud.fill_max);
                    draw_str(m16c_surface, COL_CURSOR, 174, l.b, M16C_C_TEXT);

                    ln_reset(&l); ln_puts(&l, "FILL END  ");
                    ln_udec(&l, aud.fill);
                    draw_str(m16c_surface, COL_CURSOR, 186, l.b, M16C_C_TEXT);

                    ln_reset(&l); ln_puts(&l, "UNDERRUN  ");
                    ln_u64dec(&l, aud.underruns);
                    draw_str(m16c_surface, COL_CURSOR, 198, l.b, M16C_C_TEXT);

                    /* Always 0 on THIS screen -- a non-zero overrun count
                       fails the clean conjunction and the session is routed to
                       the fatal screen instead, so it can never be drawn here.
                       It is displayed anyway, because a diagnostic that omits
                       the value it is asserting about is not evidence. */
                    ln_reset(&l); ln_puts(&l, "OVERRUN   ");
                    ln_u64dec(&l, aud.overruns);
                    draw_str(m16c_surface, COL_CURSOR, 210, l.b,
                             (aud.overruns != 0u) ? M16C_C_ERR : M16C_C_TEXT);

                    ln_reset(&l); ln_puts(&l, "PRODUCED   ");
                    ln_u64dec(&l, aud.produced);
                    draw_str(m16c_surface, COL_DIAG2, 162, l.b, M16C_C_TEXT);

                    ln_reset(&l); ln_puts(&l, "CONSUMED   ");
                    ln_u64dec(&l, aud.consumed);
                    draw_str(m16c_surface, COL_DIAG2, 174, l.b, M16C_C_TEXT);

                    /* THE RING BIJECTION, RENDERED SO IT CAN BE CHECKED BY EYE
                       AGAINST FILL END. Every frame pushed is eventually
                       drained or dropped, so produced - consumed must equal
                       the frames still resident. If these two disagree the
                       ring's own bookkeeping is wrong and every other figure
                       on this screen is unsafe to believe. */
                    ln_reset(&l); ln_puts(&l, "RESIDUE    ");
                    ln_u64dec(&l, aud.produced - aud.consumed);
                    draw_str(m16c_surface, COL_DIAG2, 186, l.b,
                             ((aud.produced - aud.consumed) == (u64)aud.fill)
                                 ? M16C_C_TEXT : M16C_C_ERR);

                    ln_reset(&l); ln_puts(&l, "SUBMITTED  ");
                    ln_u64dec(&l, plat_audio_submitted());
                    draw_str(m16c_surface, COL_DIAG2, 198, l.b, M16C_C_MUTED);

                    /* ***** A DEAD PORT IS SILENT AND READS PERFECTLY CLEAN.
                       ***** The drain removes frames from the ring BEFORE it
                       inspects the submit result, so a backend rejecting every
                       grain yields overruns 0, underruns 0, a clean verdict
                       and no sound whatsoever. This is the ONLY value in the
                       payload that separates that case from a healthy one.
                       Both it and SUBMITTED are payload-lifetime, not
                       session-lifetime -- the port is never closed between
                       games -- so they legitimately exceed this session's own
                       grain count. That is correct, not a leak. */
                    ln_reset(&l); ln_puts(&l, "SUBMIT ERR ");
                    ln_u64dec(&l, plat_audio_errors());
                    draw_str(m16c_surface, COL_DIAG2, 210, l.b,
                             (plat_audio_errors() != 0u) ? M16C_C_ERR
                                                         : M16C_C_TEXT);

                    /* NEGATIVE means the backend consumed fewer frames than
                       the 48 kHz clock called for, i.e. the emulator ran slow
                       -- the benign direction. POSITIVE means it ran ahead,
                       which is the direction that ends in a fatal overrun. */
                    ln_reset(&l); ln_puts(&l, "DRIFT     ");
                    ln_s64dec(&l, aud_drift_frames);
                    ln_puts(&l, " FR (");
                    ln_s64dec(&l, aud_drift_ms);
                    ln_puts(&l, " MS)");
                    draw_str(m16c_surface, COL_CURSOR, 224, l.b, M16C_C_TEXT);
#endif /* M16C_SHOW_AUDIO_DIAG */

                    m16c_flip();
                }
            }

            /* ---- THE DISPLAY IS RETURNED TO THE BORDER COLOUR ------------
               So the last thing on screen is not a half-composed UI frame while
               the boundary runs. The picker's own first frame will cover the
               whole framebuffer through blit_ui() a moment later. */
            plat_video_fill(0, GBA_PRESENT_BORDER);
            plat_video_fill(1, GBA_PRESENT_BORDER);
            (void)plat_video_present(0, m16c_frame_id++);

            /* =============================================================
             * ***** THE UNCLEAN SESSION. THE SINGLE BIGGEST BEHAVIOURAL
             * DIFFERENCE BETWEEN M13C AND M16-2. *****
             * =============================================================
             *
             * M13C called m13c_abort() here and returned, ending the payload: a
             * watchdog, an audio overrun or a lost map closed LUAport. That was
             * right for a launcher that ran one game per invocation -- there was
             * nothing left to return to.
             *
             * Here it is SESSION-fatal. The reasoning is the same one that
             * classified every other failure in this file: the CARTRIDGE
             * misbehaved, LUAport did not. The complete boundary still runs, the
             * save is still untouched, the reason is still named on screen and in
             * the log, the failure is still counted -- and then the operator gets
             * their library back instead of a closed launcher.
             *
             * ***** NOTHING ABOUT THE SAVE POLICY IS RELAXED TO ACHIEVE THAT.
             * ***** `clean` was computed above, from the same seven terms, before
             * this branch existed; the commit at step 456 has already been
             * skipped; and m16c_session_fatal() -> m16c_boundary() ->
             * gba_session_end() unlatches the identity on the way out. An
             * unclean session commits NOTHING on any path. */
            if (!clean) {
                const char *why;
                const char *head = M16C_H_SESSION;
                const char *hint = "RETURNING TO PICKER. THE SAVE IS UNTOUCHED.";

                switch (end_reason) {
                /* ***** THE BACKSTOP NAMES ITSELF. ***** A watchdog exit is
                   never dressed up as a USER EXIT and never graded a PASS. It
                   carries its own heading so the operator can see at a glance
                   that the emulator STOPPED ITSELF rather than that they ended
                   the session. */
                case M16C_END_WATCHDOG:
                    head = M16C_H_WATCHDOG;
                    why  = "THE SESSION RAN PAST ITS SAFETY LIMIT AND WAS "
                           "STOPPED.";
                    hint = "USE L1 + R1 + L2 + R2 TO FINISH A GAME NEXT TIME.";
                    break;
                case M16C_END_MAPFAIL:
                    head = M16C_H_MAPPING;
                    why  = "THE CARTRIDGE MEMORY MAP WAS LOST.";
                    break;
                case M16C_END_OVERRUN:
                    why = "AUDIO OUTPUT FELL BEHIND.";                    break;
                case M16C_END_SKIPFRAME:
                    why = "THE EMULATOR STARTED DROPPING FRAMES.";        break;
                case M16C_END_ARENA:
                    why = "MEMORY GREW DURING PLAY.";                    break;
                default:
                    why = "THE SESSION DID NOT END CLEANLY.";            break;
                }

                if (end_reason == M16C_END_WATCHDOG)
                    printf("M16C: ***** WATCHDOG ***** the %u-iteration "
                           "emergency backstop was reached after %u ms. THIS IS "
                           "NOT A USER EXIT AND IT IS NOT A PASS.\n",
                           (unsigned)M16C_PLAY_MAX_ITERS,
                           (unsigned)(elapsed_us / 1000u));

                klog("LUAport M16C: the session did not end cleanly, so NOTHING "
                     "was committed and the save on disk is untouched. THE GAME "
                     "ENDED -- LUAPORT DID NOT. The full session teardown runs "
                     "now and the picker comes back\n");

                if (!m16c_session_fatal(ext, M16C_RUN_UNSTABLE, head,
                        why,
                        "NOTHING WAS WRITTEN. THE SAVE ON DISK IS UNTOUCHED.",
                        hint,
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }

            /* ***** A FAILED COMMIT OUTRANKS A CLEAN SESSION. ***** The gameplay
               was perfect and the save may still have been lost, so this must
               NOT report success. It is the only outcome in which the operator's
               progress is actually at risk.
               ***** AND IT IS SESSION-FATAL, NOT RUNTIME-FATAL. ***** The
               savedata layer is payload-scoped and still initialised; a commit
               can fail for reasons specific to ONE cartridge's save (a full
               container, a refused mount) and the next game may well commit
               perfectly. What must not happen is silence, so the operator is told
               plainly -- and the clean tally above is NOT undone, because the
               SESSION was clean and it is the WRITE that failed. The two are
               counted separately on purpose: m16c_commits_done only ever
               increments on GBA_SAVEFILE_OK. */
            if (commit_state == 2u) {
                printf("M16C: ***** THE COMMIT FAILED (%d) AFTER A CLEAN "
                       "SESSION *****\n", commit_rc);
                klog("LUAport M16C: the gameplay itself was flawless, which is "
                     "exactly why this must not be reported as a success. If the "
                     ".sav was flushed but the .hdr was not, the .sav on disk is "
                     "SUSPECT and the payload hash in the old .hdr will correctly "
                     "reject it next time\n");
                ext->dbg[4] = (u64)(u32)(s32)commit_rc;
                if (!m16c_session_fatal(ext, M16C_COMMIT_FAILED,
                        M16C_H_SAVE_WRITE,
                        "THE GAME SAVED BUT THE SAVE COULD NOT BE WRITTEN.",
                        "THE PREVIOUS SAVE ON DISK IS STILL INTACT.",
                        "TRY THE SESSION AGAIN.",
                        mark, mark_taken, rfile_before))
                    return;
                continue;
            }

            klog("LUAport M16C: ***** SESSION COMPLETE. ***** A cartridge the "
                 "operator chose on screen was loaded by its exact full path, "
                 "had its CONTENT-DERIVED save identity latched before any "
                 "execution, had its persisted save validated and restored, was "
                 "emulated, presented, heard and controlled, and then had its "
                 "save committed -- or correctly declined to commit one that had "
                 "not changed. No other cartridge's save was read, written or "
                 "deleted\n");
            printf("M16C: RESTORE %u (%u bytes), DIRTY %s, COMMIT %u (%u "
                   "bytes)\n", restore_state, restored_bytes,
                   modified ? "YES" : "NO", commit_state, commit_bytes);

            /* ***** THE BOUNDARY, ON THE CLEAN PATH, AFTER THE COMMIT. *****
               Every session-fatal path above reached it through
               m16c_session_fatal(); this is the only other call site. It is
               strictly AFTER step 456 -- which is what keeps gba_save_unlatch()
               behind the writer -- and its failure is RUNTIME-fatal, so a
               contaminated boundary ends the payload rather than seeding the
               next game. */
            if (!m16c_boundary(ext, mark, mark_taken, rfile_before))
                return;
        }

        /* ***** AND ROUND AGAIN. THE BACKWARD BRANCH. ***** Control returns to
           step 449, the picker, with the cartridge torn down, the arena at its
           watermark, the identity unlatched and the operator's cursor still on
           the row they just played. */
    }

    /* =====================================================================
     * step 459 -- THE PAYLOAD EXIT, AND THE LEDGER
     * =====================================================================
     *
     * ***** REACHED BY EXACTLY ONE ROUTE: CIRCLE AT THE PICKER. ***** Every
     * other way out of this function is a `return` from inside the loop, and all
     * of them are runtime-fatal. There is no fall-through: the `for (;;)` above
     * has no condition, so the only `break` is the user_exit one at step 449.
     *
     * ***** THE LEDGER IS THE EVIDENCE THAT THE PAYLOAD DID NOT RESTART. *****
     * This is what M16-2 has to demonstrate that M16-0 and M16-1 could not, and
     * the strongest single number in it is m16c_frame_id: it is monotonic across
     * the whole process, so a log whose frame ids climb through three games
     * cannot have come from three separate launches. The session identities are
     * CONTENT-DERIVED hashes, so an A -> B -> A run shows the same identity for
     * the first and third games with a different one in between -- which is the
     * isolation claim, stated in numbers that no address could provide. */
    ext->step = 459;

    klog("LUAport M16C: ================================================\n");
    klog("LUAport M16C: ***** CIRCLE AT THE PICKER. THE OPERATOR IS CLOSING "
         "LUAPORT. ***** This is the payload exit and it is the only one that is "
         "not the power button.\n");
    printf("M16C: SESSIONS STARTED %u, CLEAN %u, FAILED %u, COMMITS WRITTEN "
           "%u\n", m16c_sessions_started, m16c_sessions_clean,
           m16c_sessions_failed, m16c_commits_done);
    printf("M16C: TOTAL FRAMES PRESENTED BY THIS PAYLOAD %u -- MONOTONIC ACROSS "
           "EVERY SESSION, WHICH IS WHAT PROVES THE PROCESS NEVER RESTARTED\n",
           (unsigned)m16c_frame_id);
    printf("M16C: LAST SESSION -- STATUS %d, END REASON %u, CLEAN %u, CODE '%s' "
           "ID '%s' ROMHASH 0x%08X\n",
           m16c_last_session_status, m16c_rec.end_reason, m16c_rec.clean,
           m16c_rec.code, m16c_rec.save_id, m16c_rec.romhash);
    printf("M16C: LAST SESSION -- %u frames in %u ms, RESTORE %u (%u bytes), "
           "COMMIT %u (%u bytes)\n",
           m16c_rec.frames_presented, m16c_rec.elapsed_ms,
           m16c_rec.restore_state, m16c_rec.restored_bytes,
           m16c_rec.commit_state, m16c_rec.commit_bytes);
    printf("M16C: ARENA -- payload baseline %u, session 1 mark %u (taken %d), "
           "at exit %u\n",
           (unsigned)m16c_arena_baseline, (unsigned)m16c_mark_first,
           m16c_mark_first_taken, (unsigned)arena_used());
    printf("M16C: RFILE in use at exit %u of %u (expect 0)\n",
           gba_rfile_inuse(), gba_rfile_slots());
    klog("LUAport M16C: ================================================\n");

    /* ***** THE FLOW NIBBLE IS THE ONE dbg FIELD THE EXIT OWNS. ***** Bits 11:9
       are M13C's `flow` field and 0 means "exited without completing a session".
       It is written here ONLY when no session ever completed, so a payload that
       played three games and then exited keeps the last session's packing -- the
       notification then describes a real session, and the counters above describe
       the payload. Overwriting it unconditionally would throw away the only
       machine-readable record of the last game the operator played. */
    if (m16c_sessions_clean == 0u && m16c_sessions_failed == 0u) {
        ext->dbg[3] = 0;
        ext->dbg[0] = 0;
        ext->dbg[1] = 0;
        ext->dbg[2] = 0;
    }
    ext->dbg[5] = (u64)m16c_sessions_started;
    ext->dbg[6] = (u64)m16c_sessions_clean;
    ext->dbg[7] = (u64)m16c_frame_id;

    /* ***** THE RUNTIME COMES DOWN HERE, AND THIS IS THE ONLY NON-FATAL PLACE
       IT EVER DOES. ***** Video, pad and the audio port have been up since step
       446 and have survived every session boundary in between, exactly as
       gba_session.h:98-101 requires. m16c_teardown() is idempotent by flag and
       is the sole owner of all three shutdowns. */
    m16c_teardown();

    klog("LUAport M16C: ***** THE PICKER CAME BACK, AND THEN THE OPERATOR "
         "CLOSED IT. ***** One payload, one process, one arena: it brought the "
         "runtime up ONCE, scanned the library ONCE, loaded the BIOS ONCE, and "
         "then ran every cartridge the operator chose -- each one loaded by its "
         "exact full path, each one with its own CONTENT-DERIVED save identity "
         "latched before a single instruction executed, each one restored and "
         "committed under that identity alone, and each one torn down to a proven-"
         "clean boundary before the next began. No cartridge's save was read, "
         "written or deleted under another cartridge's name\n");

    ext->status = M16C_EXIT;
}
