/* LUAport R7 -- adapters/gba/gba_r7input.c
 *
 * The implementation only. adapters/gba/gba_r7input.h carries the full
 * derivation: why the frozen mapping table is NOT edited, why this is a
 * canonicalisation rather than a second table, and why the frozen invariants
 * M8_ST_UNIQUE and M8_ST_UNMAPPED remain literally true afterwards.
 *
 * NO gpSP HEADER IS INCLUDED HERE. gba_input.c opens with #include "common.h"
 * because it writes REG_P1; this file writes nothing and touches no emulator
 * state, so it needs only the bit names and the one declaration below. That is
 * what lets it compile with $(M16CFLAGS) instead of the gpSP flags, and it is
 * the mechanical reason R7 cannot disturb the gpSP surface.
 */

#include "gba_input.h"     /* M8_PAD_* bit names and m8_input_from_pad()   */
#include "gba_r7input.h"

/* ***** THE FOUR FACE BUTTONS, AND NOTHING ELSE, ARE REWRITTEN. *****
 *
 * Named once here so the class definition appears in exactly one place. Any
 * bit outside this mask is carried through untouched, which is what keeps the
 * D-pad, L1, R1, OPTIONS and TOUCHPAD on the frozen table's definitions and
 * keeps L2/R2/L3/R3 on its ABSENCE of a definition. */
#define R7_FACE_MASK  (M8_PAD_CROSS | M8_PAD_CIRCLE | \
                       M8_PAD_SQUARE | M8_PAD_TRIANGLE)

/* The two intentional equivalence classes, stated as the pad bits that join
 * them. The REPRESENTATIVE of each class is the button the frozen table
 * already maps to the wanted GBA key -- CROSS for A, CIRCLE for B -- so no new
 * pad-to-key relation is invented anywhere in this file. */
#define R7_CLASS_A    (M8_PAD_CROSS  | M8_PAD_CIRCLE)     /* -> GBA A */
#define R7_CLASS_B    (M8_PAD_SQUARE | M8_PAD_TRIANGLE)   /* -> GBA B */

unsigned int r7_input_from_pad(unsigned int pad_buttons, int connected) {
    unsigned int p;

    /* THE ANTI-PHANTOM-INPUT GUARD, AND IT COMES FIRST -- gba_input.c:134-144's
     * rule, applied again here rather than inherited. m8_input_from_pad() makes
     * the same check, and the duplication is deliberate: it makes THIS
     * function's contract true by itself instead of by reference to a callee,
     * which is the same "two guards, no shared assumption" reasoning that put
     * the check in the frozen adapter in the first place. */
    if (!connected)
        return 0u;

    /* Clear the four face bits, then re-assert the ONE representative of each
     * class that is present. An OR of the class is what collapses two physical
     * buttons onto one GBA key: pressing both members sets the representative
     * ONCE, so the result carries a single asserted key bit and never a
     * doubled or ambiguous one. */
    p = pad_buttons & ~(unsigned int)R7_FACE_MASK;

    if (pad_buttons & (unsigned int)R7_CLASS_A)
        p |= (unsigned int)M8_PAD_CROSS;

    if (pad_buttons & (unsigned int)R7_CLASS_B)
        p |= (unsigned int)M8_PAD_CIRCLE;

    /* ***** THE FROZEN TABLE REMAINS THE ONLY AUTHORITY ON MEANING. *****
     * Every pad-bit-to-GBA-key decision, including the two this file routes
     * traffic to, is still made by the frozen arrays in gba_input.c -- the
     * tables M8_ST_UNIQUE proves injective and M8_ST_UNMAPPED proves silent
     * about L2/R2/L3/R3. R7 chooses WHICH ROW is consulted; it never adds,
     * edits or shadows a row. */
    return m8_input_from_pad(p, connected);
}
