/* LUAport M8 -- adapters/gba/gba_input.h
 *
 * =============================================================================
 * THE DUALSENSE -> GBA KEYPAD ADAPTER.
 * =============================================================================
 *
 * WHY THIS HEADER EXISTS AT ALL, AND WHY IT INCLUDES NOTHING
 * ---------------------------------------------------------
 * Exactly the reason gba_probe.h (M2), gba_bios.h (M3), gba_rom.h (M4),
 * gba_exec.h (M5) and gba_present.h (M7) exist, and the rule has not moved
 * since M1: gpsp/common.h:100 typedefs u64 as `unsigned long long` while
 * runtime/core.h:30 typedefs it as `unsigned long`. Both are 64 bits on this
 * ABI but they are DISTINCT TYPES in C, so any translation unit including both
 * fails on the duplicate typedef.
 *
 * THIS FILE IS INCLUDED FROM BOTH SIDES OF THAT BOUNDARY:
 *
 *   apps/m8gpsp/main.c   includes runtime/core.h, runtime/platform.h ... and this
 *   adapters/gba/gba_input.c  includes gpsp/common.h ................ and this
 *
 * It therefore includes NEITHER, exactly as gba_exec.h does, and every
 * declaration below uses ONLY `unsigned int` and `int` -- the two types that
 * mean the same thing in both worlds. gba_present.h can afford to include
 * core.h because only the runtime side ever sees it; this one cannot.
 *
 * =============================================================================
 * THE GBA KEYPAD, DERIVED FROM gpSP's OWN SOURCE. NOTHING HERE IS ASSUMED.
 * =============================================================================
 *
 * THE REGISTER.  gpsp/gba_memory.h:185-186
 *     REG_P1    = 0x098      REG_P1CNT = 0x099
 * io_registers is `u16[512]` (gba_memory.h:275) and is indexed in HALFWORDS, so
 * 0x098 * 2 = 0x130 -- hardware address 0x04000130, and P1CNT is 0x04000132.
 *
 * THE BITS.  gpsp/input.h:27-37, input_buttons_type, reproduced value for value:
 *     L 0x200  R 0x100  DOWN 0x80  UP 0x40  LEFT 0x20  RIGHT 0x10
 *     START 0x08  SELECT 0x04  B 0x02  A 0x01
 * This ordering is bit-identical to mGBA's own GBAKey enum (A=0 .. L=9), which
 * is why mgba/cinema/gba/irq/keyirq/config.ini's `input=0:0001` and `2:0003`
 * decode directly as A and A+B under gpSP's table. That agreement between two
 * independent emulators is a free cross-check on this table, and it is the
 * reason the chosen cartridge's scripted input can be reasoned about at all.
 *
 * ACTIVE LOW.  gpsp/input.c:163, the ONE line in the tree that writes REG_P1:
 *     write_ioreg(REG_P1, (~old_key) & 0x3FF);
 * A ZERO BIT MEANS PRESSED. This is the single easiest thing in the milestone
 * to get backwards, and getting it backwards produces a cartridge that behaves
 * as though every button is held down at once -- which looks like a broken
 * emulator, not like a broken sign convention. m8_input_selftest() pins all ten
 * buttons, the neutral state and the A+B chord against hand-derived values.
 *
 * THE RESET VALUE.  gpsp/gba_memory.c:2429, inside init_memory(): 0x3FF, i.e.
 * every key released. M7 asserts that value at its step 107 and M8 keeps the
 * assertion; neither milestone WRITES it.
 *
 * READ-ONLY TO THE EMULATED CPU.  gpsp/gba_memory.c:1037-1039 is
 *     case REG_P1: break;   // Do nothing
 * so the cartridge cannot write REG_P1 back. That is what makes the read-back
 * assertion in apps/m8gpsp/main.c sound: a value written here can only be
 * changed by something on OUR side of the boundary.
 *
 * =============================================================================
 * THE FINDING THAT SHAPES THIS ENTIRE MILESTONE
 * =============================================================================
 * update_input() IS LINKED BUT IS NEVER CALLED.
 *
 * gpsp/input.c is compiled into every M-series image (Makefile: gpsp_input.o)
 * because savestate.c needs input_check/read/write_savestate. But the ONLY
 * caller of update_input() in the entire tree is gpsp/libretro/libretro.c:1373,
 * and libretro.c is excluded from every LUAport build. tools/check_forbidden.py
 * :123-142 already records this and notes that input_state_cb stays NULL.
 *
 * FOUR CONSEQUENCES, ALL OF THEM FAVOURABLE:
 *
 *   1. NOTHING IN THE RUNNING IMAGE EVER WRITES REG_P1 AFTER RESET. A value M8
 *      writes therefore PERSISTS until M8 changes it.
 *   2. THERE IS NO RACE AND NO ONE-FRAME AMBIGUITY, because there is no
 *      competing writer to race against. The entire hazard class that Task 8 of
 *      the M8 brief asks about is removed by construction rather than by
 *      careful ordering.
 *   3. REG_P1 sat at 0x3FF from init_memory() onward for every milestone up to
 *      M7, so those runs genuinely had a neutral keypad -- the "no input" claim
 *      M1-M7 made was true at the register, not merely at the call graph.
 *   4. THE KEYPAD IRQ PATH IS ALSO DEAD, because trigger_key() is `static` in
 *      input.c and is reachable only from update_input(). See below.
 *
 * =============================================================================
 * REG_P1 PROPAGATION AND KEYPAD IRQ GENERATION ARE TWO DIFFERENT THINGS
 * =============================================================================
 * They are deliberately split into two functions here, and the split is the
 * point:
 *
 *   m8_input_apply()      writes REG_P1. Nothing else. This is the operative
 *                         path and it is what M8 proves first.
 *   m8_input_irq_edge()   replicates gpsp/input.c:41-66's trigger_key() and
 *                         input.c:159-160's edge condition, and NOTHING ELSE.
 *
 * WHY THE SECOND ONE EXISTS AT ALL, GIVEN THE INSTRUCTION NOT TO DUPLICATE
 * trigger_key() MERELY BECAUSE update_input() IS ABSENT:
 *
 *   It is NOT called merely because the libretro path is gone. It is called
 *   under EXACTLY THE CONDITION gpSP ITSELF USES, and under no other:
 *   trigger_key()'s entire body is wrapped in
 *
 *       if((p1_cnt >> 14) & 0x01)          <-- input.c:45
 *
 *   so when the cartridge has NOT enabled the keypad IRQ, trigger_key() is a
 *   NO-OP. m8_input_irq_edge() reproduces that guard first and returns without
 *   touching anything if it is not met. THE REPLICATION IS THEREFORE INERT
 *   UNLESS THE CARTRIDGE ITSELF ASKS FOR IT, and "was it necessary?" is
 *   answered by MEASUREMENT on the console -- the return value says whether the
 *   enable bit was set, whether the edge condition held, and whether an
 *   interrupt was actually raised, and apps/m8gpsp/main.c reports all three.
 *
 *   THE CALL SITE IS THE PROVEN ONE. gpsp/libretro/libretro.c:1368-1373 shows
 *   retro_run() calling update_input() -- and therefore trigger_key() -- BEFORE
 *   execute_arm(), from OUTSIDE the interpreter, between frames. That is
 *   precisely where apps/m8gpsp/main.c calls this. The ordering is upstream's,
 *   not invented here.
 *
 *   gpsp/ IS NOT MODIFIED. trigger_key() is static and cannot be called, so
 *   the logic is restated here against gpSP's OWN public entry points --
 *   flag_interrupt() and check_and_raise_interrupts(), both declared at
 *   gpsp/cpu.h:113-115 and both already linked in gpsp_cpu.o. IRQ_KEYPAD is
 *   gpsp/cpu.h:72. Nothing is reimplemented; the CONDITION is restated and the
 *   ACTION is delegated.
 *
 * =============================================================================
 * WHAT THIS ADAPTER DELIBERATELY DOES NOT DO
 * =============================================================================
 *   - does NOT call any PS5 API. It never sees a scePad handle and never
 *     includes runtime/platform.h. The app reads the pad and hands this file a
 *     plain `unsigned int` of raw button bits.
 *   - does NOT touch the framebuffer, the presentation path, or VideoOut. Input
 *     may only reach the screen by changing what the EMULATOR draws, which is
 *     the whole point of M8; adapters/gba/gba_present.c is not edited and does
 *     not learn that input exists.
 *   - does NOT read the analog sticks. M8 measures and reports them (M0 already
 *     proved that on hardware) but does NOT feed them to the GBA -- the pad
 *     layer applies no deadzone (runtime/platform.c:503-507), so a resting
 *     stick that drifts would inject a direction and the resulting failure
 *     would be indistinguishable from a mapping bug.
 *   - does NOT allocate, log, or keep hidden state beyond the one edge-tracking
 *     word the IRQ condition structurally requires.
 *   - does NOT implement turbo, fast-forward, or the GBP keypad hack that
 *     update_input() also carries. Those are frontend policy and none of them
 *     is reachable without libretro.c.
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py:108-111 matches the bare
 * substrings "retro_", "libretro_", "vfs_", "path_", "string_is_",
 * "filestream_", "rtime_", "encoding_", "fopen_utf8", "gba_cc_lut" and
 * "open_gba_bios_rom", plus the dynarec patterns "translation_cache",
 * "block_lookup_address", "translate_block", "_stub" and "emit_". Every symbol
 * below is m8_input_* / M8_*, and NONE of them matches any of those. "input" on
 * its own is NOT a forbidden substring -- only the frontend-prefixed forms are.
 * NO ALLOWLIST IS WIDENED FOR M8.
 *
 * BITMASK CONVENTION: A SET BIT IS A FAILED ASSERTION, identical to M2-M7, so a
 * success is a single `== 0` test and a newly added assertion cannot read as
 * "pass" if the fixture forgets to name it.
 */

#ifndef LUAPORT_GBA_INPUT_H
#define LUAPORT_GBA_INPUT_H

/* ===================================================== the GBA key domain ==
 *
 * gpsp/input.h:27-37, value for value. RESTATED rather than imported, because
 * importing would mean including a gpSP header from apps/m8gpsp/main.c and that
 * is the exact thing the type boundary forbids. The values are cross-checked
 * against gpSP's own enum inside gba_input.c, which CAN see both -- so the
 * restatement cannot drift without failing to compile. */
#define M8_KEY_A        0x001u
#define M8_KEY_B        0x002u
#define M8_KEY_SELECT   0x004u
#define M8_KEY_START    0x008u
#define M8_KEY_RIGHT    0x010u
#define M8_KEY_LEFT     0x020u
#define M8_KEY_UP       0x040u
#define M8_KEY_DOWN     0x080u
#define M8_KEY_R        0x100u
#define M8_KEY_L        0x200u

/* Ten buttons, bits 0-9. Bits 10-15 of REG_P1 are unused on real hardware and
 * gpSP masks every write with 0x3FF (input.c:163), so they are structurally
 * zero and are never asserted as anything else. */
#define M8_KEY_MASK     0x3FFu

/* gpsp/gba_memory.c:2429 -- init_memory()'s value, all ten keys RELEASED.
 * ASSERTED by the fixture, never written by it. */
#define M8_P1_NEUTRAL   0x3FFu

/* ================================================ the DualSense bit mirror ==
 *
 * These MIRROR runtime/platform.h:162-177. They are copied rather than included
 * for the type-boundary reason above, which creates exactly one hazard: the two
 * copies could drift.
 *
 * THAT HAZARD IS CLOSED AT COMPILE TIME, NOT BY CONVENTION. apps/m8gpsp/main.c
 * includes BOTH this header and runtime/platform.h -- it is the one translation
 * unit that can see both -- and carries a _Static_assert per mapped button
 * requiring M8_PAD_x == PAD_x. A drift is therefore a BUILD FAILURE naming the
 * offending button, not a silent mismapping discovered on a television.
 *
 * PROVENANCE, AND THE FOUR THAT ARE WEAKER THAN THE OTHERS.
 * runtime/platform.h:159-160 states that twelve of these are confirmed by
 * working reference code. That claim was re-verified against
 * LuaPSX/src/main.c:157-171, which defines exactly L2, R2, L1, R1, UP, RIGHT,
 * DOWN, LEFT, CROSS, CIRCLE, TRIANGLE and SQUARE with identical values.
 *
 * L3, R3, OPTIONS and TOUCHPAD are NOT in that reference set. (platform.h's own
 * note says "the two that are not" and points at platform.c, which in fact
 * mentions none of them -- so it is four, not two, and the cross-reference is
 * stale. runtime/platform.h is frozen and is NOT edited to correct it; the
 * discrepancy is recorded here and in the M8 report instead.)
 *
 * THE CONSEQUENCE IS A DESIGN CONSTRAINT, NOT A WARNING. OPTIONS and TOUCHPAD
 * are mapped -- they are almost certainly right, and Start and Select are worth
 * having -- but M8's PASS CONDITION IS GATED ON CROSS ALONE, which rides a
 * reference-confirmed bit. If PASS depended on OPTIONS or TOUCHPAD, a wrong bit
 * would be indistinguishable from a wrong mapping, a wrong sign convention or a
 * dead cartridge. */
#define M8_PAD_L3        0x00000002u   /* not in the LuaPSX reference set */
#define M8_PAD_R3        0x00000004u   /* not in the LuaPSX reference set */
#define M8_PAD_OPTIONS   0x00000008u   /* not in the LuaPSX reference set */
#define M8_PAD_UP        0x00000010u
#define M8_PAD_RIGHT     0x00000020u
#define M8_PAD_DOWN      0x00000040u
#define M8_PAD_LEFT      0x00000080u
#define M8_PAD_L2        0x00000100u
#define M8_PAD_R2        0x00000200u
#define M8_PAD_L1        0x00000400u
#define M8_PAD_R1        0x00000800u
#define M8_PAD_TRIANGLE  0x00001000u
#define M8_PAD_CIRCLE    0x00002000u
#define M8_PAD_CROSS     0x00004000u
#define M8_PAD_SQUARE    0x00008000u
#define M8_PAD_TOUCHPAD  0x00100000u   /* not in the LuaPSX reference set */

/* runtime/platform.c:501 masks scePadRead's word to these 21 bits. Restated so
 * the selftest can prove no mapped button lies outside the reportable range --
 * a mapping to a bit the platform layer discards would silently never fire. */
#define M8_PAD_REPORTED  0x001FFFFFu

/* CREATE / SHARE IS DELIBERATELY UNMAPPED AND HAS NO CONSTANT HERE.
 * No PAD_CREATE or PAD_SHARE exists in runtime/platform.h, nothing in
 * LuaPSX/src/main.c:157-171 assigns one, and the system normally reserves that
 * button. Bits 0 and 16-19 are unassigned inside the 21-bit mask, but choosing
 * one of them would be INVENTING a value -- which is precisely how the 0xFF
 * userId bug entered the reference. It stays unmapped. */

/* ================================================== the M8 mapping policy ==
 *
 *     DualSense          GBA          bit confidence
 *     -----------------  -----------  ----------------------------
 *     D-pad U/D/L/R      D-pad        reference-confirmed
 *     CROSS              A            reference-confirmed  <-- PASS-CRITICAL
 *     CIRCLE             B            reference-confirmed
 *     L1                 L            reference-confirmed
 *     R1                 R            reference-confirmed
 *     OPTIONS            START        NOT reference-confirmed
 *     TOUCHPAD           SELECT       NOT reference-confirmed
 *
 * CROSS -> A and CIRCLE -> B follow the convention GBA front-ends conventionally
 * use, and both ride confirmed bits.
 *
 * THIS IS ADAPTER POLICY, NOT ARCHITECTURE. It lives entirely in gba_input.c,
 * touches no gpSP file, and can be changed at M9 without disturbing anything
 * this milestone proves. No PS5 constant appears anywhere in gpsp/. */
#define M8_MAP_COUNT     10u

/* ==================================================== the pure conversions ==
 *
 * Both are TOTAL, STATELESS FUNCTIONS OF THEIR ARGUMENTS. They touch no gpSP
 * state, no hardware and no globals, which is what makes m8_input_selftest()
 * able to run BEFORE the display is taken and BEFORE the pad is opened -- the
 * same ordering discipline that puts gba_present_selftest() ahead of
 * plat_video_init() in M7. */

/* Raw scePadRead button word -> 10-bit GBA logical key mask.
 *
 * TAKES RAW BITS, NOT `const struct plat_pad_state *`, and that is a deliberate
 * departure from the shape the M8 brief sketched. struct plat_pad_state is
 * declared in runtime/platform.h, which includes runtime/core.h, which
 * reintroduces the u64 typedef clash inside this adapter's translation unit.
 * The app unpacks the struct; this stays type-clean and offline-testable.
 *
 * `connected` IS THE ANTI-PHANTOM-INPUT GUARD. When it is 0 this returns 0 --
 * a neutral keypad -- REGARDLESS of what is in `pad_buttons`. A dropped
 * scePadRead must never be able to synthesise or latch a held button, because a
 * stuck direction is exactly the failure that would look like a mapping defect.
 * runtime/platform.c:484-489 already zeroes `buttons` on a failed read and
 * holds only the last good STICK values; this is the second, independent
 * guard, on our side of the boundary. */
unsigned int m8_input_from_pad(unsigned int pad_buttons, int connected);

/* 10-bit logical key mask -> the ACTIVE-LOW value REG_P1 must hold.
 * gpsp/input.c:163 exactly: (~keys) & 0x3FF. Neutral (0x000) gives 0x3FF. */
unsigned int m8_input_to_reg_p1(unsigned int gba_keys);

/* The mapping table, exposed so the fixture can PRINT it and the selftest can
 * walk it rather than restating it a third time. Returns 0 for an index at or
 * beyond m8_input_map_count(). */
unsigned int m8_input_map_count(void);
unsigned int m8_input_map_pad(unsigned int i);
unsigned int m8_input_map_key(unsigned int i);

/* ======================================================= the gpSP contact ==
 *
 * Everything below this line touches gpSP state. All of it is confined to
 * gba_input.c, and every one of these is a thin, named operation rather than a
 * general "poke the emulator" hole. */

/* THE MILESTONE. Writes REG_P1 with the active-low form of `gba_keys`, through
 * gpSP's OWN write_ioreg macro (gpsp/common.h:177). This is the ONLY write to
 * REG_P1 anywhere in the LUAport tree.
 *
 * It does NOT raise an interrupt and does NOT touch REG_IF, REG_IE or REG_IME.
 * REG_P1 PROPAGATION IS THIS FUNCTION AND NOTHING ELSE. */
void m8_input_apply(unsigned int gba_keys);

/* Read REG_P1 / REG_P1CNT / REG_IE / REG_IF / REG_IME back out, through gpSP's
 * own read_ioreg macro. Pure readers; they write nothing.
 *
 * The REG_P1 read-back is a real proof rather than a formality: the emulated
 * CPU CANNOT write REG_P1 (gba_memory.c:1037-1039 ignores the store) and
 * update_input() is never called, so if the value read back after a frame is
 * not the value applied before it, something on OUR side is clobbering it. */
unsigned int m8_input_read_reg_p1(void);
unsigned int m8_input_read_reg_p1cnt(void);
unsigned int m8_input_read_reg_ie(void);
unsigned int m8_input_read_reg_if(void);
unsigned int m8_input_read_reg_ime(void);

/* ---- the keypad interrupt, replicated only where gpSP itself would fire ----
 *
 * Reproduces gpsp/input.c:159-160's edge condition and :41-66's trigger_key()
 * body, in that order and with no additions:
 *
 *     if ((new_key | old_key) != old_key)      <-- input.c:159, the EDGE
 *         trigger_key(new_key);
 *
 *     p1_cnt = read_ioreg(REG_P1CNT);          <-- input.c:43
 *     if ((p1_cnt >> 14) & 0x01) {             <-- input.c:45  ENABLE
 *         inter = (p1_cnt & key) & 0x3FF;      <-- input.c:47
 *         if (p1_cnt >> 15) {                  <-- input.c:49  AND mode
 *             if (inter == (p1_cnt & 0x3FF)) { flag_interrupt(IRQ_KEYPAD);
 *                                              check_and_raise_interrupts(); }
 *         } else {                             <-- input.c:57  OR mode
 *             if (inter)                       { flag_interrupt(IRQ_KEYPAD);
 *                                              check_and_raise_interrupts(); }
 *         }
 *     }
 *
 * THE ENABLE TEST COMES FIRST AND IS THE WHOLE SAFETY ARGUMENT. If the
 * cartridge has not set KEYCNT bit 14, this returns having done NOTHING -- no
 * flag, no raise, no state change of any kind. It is therefore safe to call
 * unconditionally on every frame of every run, and it can never manufacture
 * behaviour the cartridge did not ask for.
 *
 * Returns a bitmask of M8_IRQ_* so the caller can REPORT what happened. That
 * return value is how the question "is keypad IRQ replication actually
 * necessary for this cartridge?" gets answered by evidence instead of by
 * assumption. */
#define M8_IRQ_ENABLED  (1u << 0)  /* KEYCNT bit 14 set -- the ROM wants IRQs  */
#define M8_IRQ_ANDMODE  (1u << 1)  /* KEYCNT bit 15 set -- ALL keys required   */
#define M8_IRQ_EDGE     (1u << 2)  /* (new|old) != old -- a key went DOWN      */
#define M8_IRQ_MATCH    (1u << 3)  /* the key-mask condition was satisfied     */
#define M8_IRQ_RAISED   (1u << 4)  /* flag_interrupt(IRQ_KEYPAD) WAS called    */
unsigned int m8_input_irq_edge(unsigned int new_keys, unsigned int old_keys);

/* ======================================================== the offline self-test
 *
 * Returns 0 when every row is exact, or a mask of M8_ST_* bits. Touches NO gpSP
 * state and NO hardware, so it runs BEFORE the pad is opened and BEFORE the
 * display is taken -- an active-low inversion or a swapped bit must be caught
 * while the host game is still on screen, exactly as M7 does with its pixel
 * conversion.
 *
 * EVERY EXPECTED VALUE WAS DERIVED BY HAND from (~keys) & 0x3FF against
 * gpsp/input.h:27-37, and NONE was captured from a run of this code. */
#define M8_ST_NEUTRAL   (1u << 0)   /* 0x000 -> 0x3FF                          */
#define M8_ST_A         (1u << 1)   /* 0x001 -> 0x3FE                          */
#define M8_ST_B         (1u << 2)   /* 0x002 -> 0x3FD                          */
#define M8_ST_AB        (1u << 3)   /* 0x003 -> 0x3FC   the chord              */
#define M8_ST_SELECT    (1u << 4)   /* 0x004 -> 0x3FB                          */
#define M8_ST_START     (1u << 5)   /* 0x008 -> 0x3F7                          */
#define M8_ST_RIGHT     (1u << 6)   /* 0x010 -> 0x3EF                          */
#define M8_ST_LEFT      (1u << 7)   /* 0x020 -> 0x3DF                          */
#define M8_ST_UP        (1u << 8)   /* 0x040 -> 0x3BF                          */
#define M8_ST_DOWN      (1u << 9)   /* 0x080 -> 0x37F                          */
#define M8_ST_R         (1u << 10)  /* 0x100 -> 0x2FF                          */
#define M8_ST_L         (1u << 11)  /* 0x200 -> 0x1FF                          */
#define M8_ST_ALL       (1u << 12)  /* 0x3FF -> 0x000   every key held         */
/* COMPOSITION IS BITWISE OR, NOT PRIORITY. gpsp/input.c:83 accumulates with
 * `new_key |= ...` and there is no precedence logic anywhere in the file, so
 * D-pad + A must yield BOTH bits. This walks every ordered pair of mapped
 * buttons and requires map(a|b) == map(a) | map(b). */
#define M8_ST_COMPOSE   (1u << 13)
#define M8_ST_MAPPING   (1u << 14)  /* a single button did not map to its key  */
/* A FAILED READ MUST NOT SYNTHESISE INPUT. m8_input_from_pad(x, 0) must be 0
 * for every x, including a fully-pressed word. */
#define M8_ST_DISCONN   (1u << 15)
#define M8_ST_RANGE     (1u << 16)  /* a mapped key escaped the 10-bit domain  */
#define M8_ST_UNIQUE    (1u << 17)  /* two DualSense buttons share a GBA key   */
/* Every mapped DualSense bit must lie inside the 21 bits scePadRead actually
 * reports (runtime/platform.c:501). A mapping to a discarded bit would never
 * fire and would look exactly like a dead button. */
#define M8_ST_REPORTED  (1u << 18)
/* The restated key values must equal gpSP's OWN enum. gba_input.c can see both
 * gpsp/input.h and this header, so it is the one place the restatement above
 * can be checked against its source. */
#define M8_ST_GPSPENUM  (1u << 19)
/* An unmapped button must contribute NOTHING. SQUARE, TRIANGLE, L2, R2, L3 and
 * R3 are deliberately unmapped at M8, and pressing all of them at once must
 * still produce a neutral keypad. */
#define M8_ST_UNMAPPED  (1u << 20)
/* to_reg_p1 must be an involution on the 10-bit domain: applying it twice
 * returns the original key mask. This is the compact statement that the
 * conversion loses nothing and inverts cleanly. */
#define M8_ST_INVOLUTE  (1u << 21)

unsigned int m8_input_selftest(void);

#endif /* LUAPORT_GBA_INPUT_H */
