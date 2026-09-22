/* LUAport R7 -- adapters/gba/gba_r7input.h
 *
 * THE GAMEPLAY BUTTON-ALIAS LAYER. ONE FUNCTION, NO TABLE, NO STATE.
 *
 * WHAT IT DELIVERS
 * ----------------
 * Inside a running cartridge, two DualSense face buttons drive GBA A and two
 * drive GBA B:
 *
 *     { CROSS, CIRCLE }     -> GBA A        (jump, in the motivating example)
 *     { SQUARE, TRIANGLE }  -> GBA B        (run / action)
 *
 * Everything else is exactly what M8 already shipped and is NOT restated here:
 *
 *     D-pad U/D/L/R -> D-pad,  L1 -> L,  R1 -> R,
 *     OPTIONS -> START,        TOUCHPAD -> SELECT,
 *     L2, R2, L3, R3 -> NOTHING.
 *
 * ***** THIS IS GAMEPLAY ONLY. ***** The picker and the Game Details screen
 * read plat_pad_read()'s word directly and are not routed through this file at
 * all, so ( X ) LAUNCH / ( O ) EXIT and ( X ) LAUNCH GAME / ( O ) BACK keep
 * their distinct meanings. Frontend navigation and cartridge input are two
 * different contexts and this header governs only the second.
 *
 * ============================================================================
 * WHY THIS FILE EXISTS INSTEAD OF AN EDIT TO gba_input.c
 * ============================================================================
 *
 * adapters/gba/gba_input.c IS NOT M16C's FILE. The Makefile compiles it
 * SEPARATELY INTO EVERY MILESTONE IMAGE -- M8 (:2997), M9 (:3608), M9-D
 * (:4194), M10 (:4894), M11 (:5792), M12B (:6983), M12C (:7932), M13B-4
 * (:10100), M13B-5 (:10775), M13C (:11607) and M16C (:15520) -- from ONE
 * source. M13C links it at Makefile:11425.
 *
 * Editing the mapping table would therefore change the FROZEN M13C BINARY,
 * whose SHA-256 is an acceptance criterion, and would perturb five further
 * hardware-verified images at the same time. The table is not edited. Not one
 * byte. gba_input.c and gba_input.h are untouched by R7.
 *
 * THE PRECEDENT IS THIS REPOSITORY'S OWN. adapters/gba/gba_m8map.h:20-24 faced
 * the identical problem -- frozen gba_rom.c, a probe that only one milestone
 * needed reshaped -- and answered it with an ADDITIVE unit linked into ONE
 * image. gba_m8map.h:38-46 then argues, for gba_input.c specifically, that
 * bolting extra policy onto it "would pass those checks mechanically while
 * destroying the single-purpose claim they exist to defend. One adapter, one
 * gpSP surface, is the rule M2-M7 established." R7 obeys that rule rather than
 * making an exception to it.
 *
 * ============================================================================
 * THE MECHANISM: CANONICALISATION, NOT A SECOND MAPPING TABLE
 * ============================================================================
 *
 * There is NO second table here, and deliberately so -- a duplicate table is
 * the thing that silently drifts. This layer rewrites the PAD WORD and then
 * hands it to the frozen translator, which stays the single authority on what
 * every button means:
 *
 *     CROSS or CIRCLE     --> canonical CROSS  --\
 *     SQUARE or TRIANGLE  --> canonical CIRCLE --+-> m8_input_from_pad()
 *     everything else     --> unchanged        --/          |
 *                                                           v
 *                                             the ordinary 10-bit GBA key mask
 *
 * CONSEQUENCES THAT FALL OUT OF THAT SHAPE, RATHER THAN BEING ADDED:
 *
 *   THE FROZEN INVARIANTS STAY LITERALLY TRUE. m8_input_selftest()'s
 *   M8_ST_UNIQUE (gba_input.c:343-351) proves the table is INJECTIVE in both
 *   directions. Because the table is never edited, that proof still holds
 *   verbatim -- it is NOT deleted, disabled, weakened or special-cased. R7 adds
 *   a SECOND layer of proof above a first layer that still passes unchanged.
 *   M8_ST_UNMAPPED (gba_input.c:385-387), which asserts that SQUARE, TRIANGLE,
 *   L2, R2, L3 and R3 map to nothing, is equally untouched and equally true OF
 *   THE TABLE: the aliasing happens before the table is consulted.
 *
 *   L2, R2, L3 AND R3 CANNOT BECOME GAMEPLAY BUTTONS. They are passed through
 *   untouched and the frozen table has no row for them. This is a STRUCTURAL
 *   guarantee -- there is no line to delete that would expose them -- not a
 *   check that must be remembered.
 *
 *   SIMULTANEOUS PRESSES ARE CORRECT BY CONSTRUCTION. m8_input_from_pad()
 *   accumulates with |= (gba_input.c:146-148), so holding CROSS and CIRCLE
 *   together canonicalises to a single CROSS bit and asserts GBA A ONCE. Two
 *   physical buttons for one GBA key produce one asserted key bit, which is the
 *   only thing a GBA keypad can represent.
 *
 *   THE RETURN CHORD IS NOT ROUTED THROUGH HERE. apps/m16cgpsp/main.c tests
 *   L1+R1+L2+R2 against the ORIGINAL raw pad word, never against this
 *   function's input or output, so the chord cannot observe the alias layer.
 *   L1 and R1 keep driving GBA L and R while also being chord members, exactly
 *   as they already did before R7.
 *
 *   THE KEYPAD IRQ PATH IS UNCHANGED AND UNCONSULTED. m8_input_irq_edge()
 *   takes 10-bit GBA key masks, not pad bits. This layer only changes WHICH GBA
 *   bits are set, so every edge, enable and AND/OR test downstream is evaluated
 *   on exactly the domain it was written for. Holding CROSS and then adding
 *   CIRCLE raises NO second edge, because GBA A is already down -- which is
 *   what real hardware does with one physical A button.
 *
 * ============================================================================
 * WHAT THIS FILE DOES NOT DO
 * ============================================================================
 *
 * No mapping table. No font, bitmap or lookup array of any kind. No logging and
 * no diagnostic string -- the shipping image gains no .rodata from R7. No
 * allocation. No global, no static, no persistent state: the function is a
 * TOTAL, STATELESS FUNCTION OF ITS TWO ARGUMENTS, in the same sense
 * gba_input.h:272-278 means it, and can be called before the pad is opened.
 * No runtime self-test is linked -- the exhaustive proof is HOST-SIDE, in
 * tools/r7_input_equiv.c, and costs the PS5 image nothing.
 *
 * NO gpSP HEADER IS INCLUDED AND NO PS5 API IS CALLED. This unit sits strictly
 * between the host pad word and the frozen adapter, so it compiles with
 * $(M16CFLAGS) rather than the gpSP flags, and it touches neither end of the
 * boundary gba_input.c is pinned against.
 *
 * THE ARCHITECTURE IS UNCHANGED, NOT REFACTORED:
 *
 *     DualSense -> plat_pad_read -> [ R7 alias ] -> m8_input_from_pad -> gpSP
 *
 * The alias is one link inserted inside the GBA ADAPTER LAYER. Nothing
 * GBA-specific moved into the generic runtime, and no file under runtime is
 * modified by R7.
 */
#ifndef LUAPORT_GBA_R7INPUT_H
#define LUAPORT_GBA_R7INPUT_H

/* Gameplay pad word -> 10-bit GBA key mask, with the two intentional
 * equivalence classes applied.
 *
 * `pad_buttons` is runtime/platform.h's raw button word; `connected` is the
 * pad-presence flag, and a FALSE value yields 0u -- a NEUTRAL keypad -- before
 * `pad_buttons` is examined at all. The return value is an ordinary GBA key
 * mask inside M8_KEY_MASK and is consumed by m8_input_apply() and
 * m8_input_irq_edge() exactly as m8_input_from_pad()'s return value was.
 *
 * DROP-IN: this has m8_input_from_pad()'s signature and contract, so the
 * gameplay call site changes by one identifier and nothing else. */
unsigned int r7_input_from_pad(unsigned int pad_buttons, int connected);

#endif /* LUAPORT_GBA_R7INPUT_H */
