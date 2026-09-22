/* LUAport R7 -- tools/r7_input_equiv.c
 *
 * EXHAUSTIVE OFFLINE PROOF OF THE GAMEPLAY BUTTON-ALIAS LAYER.
 *
 * TEST ONLY. THIS FILE IS NOT PART OF ANY PS5 BUILD. It appears in NO object
 * list, no payload rule builds it, and it calls host libc (stdio), which no
 * target in this tree is allowed to link. It exists so that the intentional
 * aliases can be proved over EVERY reachable input on a workstation, WITHOUT
 * spending a single byte of the shipping image -- the R7 margin above the
 * 2,000-byte floor is ~508 bytes, so an on-target self-test table was
 * deliberately not built.
 *
 * ============================================================================
 * THE QUESTION THIS ANSWERS
 * ============================================================================
 *
 * R7 intentionally introduces ALIASES: two physical DualSense buttons drive one
 * GBA key.
 *
 *     { CROSS, CIRCLE }     -> GBA A
 *     { SQUARE, TRIANGLE }  -> GBA B
 *
 * The frozen adapter's M8_ST_UNIQUE (gba_input.c:343-351) proves the mapping
 * TABLE is injective -- no two rows share a GBA key, no two share a pad bit.
 * That invariant is NOT deleted, NOT disabled, NOT weakened and NOT
 * special-cased by R7, because R7 never edits the table: it rewrites the PAD
 * WORD before the table is consulted. Section 1 below therefore RE-PROVES the
 * entire frozen self-test, M8_ST_UNIQUE included, and every later section
 * builds on top of a first layer that still passes verbatim.
 *
 * The danger with any aliasing scheme is that it silently creates aliases
 * NOBODY ASKED FOR. Section 8 is the answer to that: it encodes the PERMITTED
 * equivalence classes as data and rejects every duplicate relationship outside
 * them. The suite is therefore strictly MORE PRECISE than the invariant it
 * sits above -- it constrains a property (which buttons may share a key) that
 * M8_ST_UNIQUE could only ever answer with "none may".
 *
 * ============================================================================
 * WHY THIS HARNESS CAN INCLUDE THE gpSP-SIDE FILE
 * ============================================================================
 *
 * adapters/gba/gba_input.c opens with #include "common.h". This file
 * pre-defines that header's own guard (COMMON_H, gpsp/common.h:20-21) BEFORE
 * including the production .c, so common.h's body -- and every header it chains
 * to -- expands to NOTHING, and the handful of types, constants and globals the
 * adapter actually uses are supplied here instead. That is exactly the
 * technique tools/gba_restore_equiv.c:25-38 already established.
 *
 * The preprocessor must still FIND common.h before it can skip it, which is why
 * the build rule passes -I gpsp. THAT IS A BUILD RULE ONLY: no shipping source
 * is modified to suit this harness, nothing is copied out of gpsp/, and NO
 * TEST-ONLY #ifdef EXISTS IN ANY PRODUCTION FILE.
 *
 * BOTH PRODUCTION UNITS ARE INCLUDED VERBATIM, so the code under test is
 * literally the code that ships -- including the real m8_map_pad/m8_map_key
 * tables. Nothing is reimplemented, so the harness cannot drift from the
 * adapter it is testing.
 *
 * ============================================================================
 * WHAT THIS CANNOT PROVE, STATED PLAINLY
 * ============================================================================
 *
 * The BUTTON_* enum below is TRANSCRIBED from gpsp/input.h:27-37. In this
 * harness, gba_input.c's _Static_asserts therefore check the adapter against
 * THIS TRANSCRIPTION rather than against gpSP's real enum. That cross-check
 * keeps its full force in the REAL build, where gba_input.c sees the genuine
 * header -- see gba_input.c:90-100. A drift in gpSP would be caught by
 * `make m16c`, not here.
 *
 * It also cannot prove anything about hardware: that the PS5 reports the bit
 * values runtime/platform.h claims is a controller question, settled on a
 * television, not in this file.
 *
 * BUILD AND RUN (host, not the PS5 toolchain), from the repository root:
 *
 *     cc -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
 *        -o /tmp/r7_input_equiv tools/r7_input_equiv.c && /tmp/r7_input_equiv
 *
 * Exit status 0 = every check passed. Non-zero = a failure, located and named.
 */

#include <stdio.h>

/* ---- neutralise gpsp/common.h ------------------------------------------- */
#define COMMON_H

/* ---- the types gpSP would have supplied ---------------------------------
 * NOTE u64 IS `unsigned long long` HERE, matching gpsp/common.h:100 and NOT
 * runtime/core.h. gba_input.c uses only u16, but the set is given in full so
 * the neutralisation matches the other harnesses. */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;

/* ---- gpsp/gba_memory.h's register numbers, transcribed ------------------ */
#define REG_P1      0x098
#define REG_P1CNT   0x099
#define REG_IE      0x100
#define REG_IF      0x101
#define REG_IME     0x104

/* ---- gpsp/cpu.h:72 ------------------------------------------------------- */
#define IRQ_KEYPAD  0x1000

/* ---- gpsp/gba_memory.h:275, and the two accessors from common.h:176-177 --
 * eswap16 is the identity on a little-endian host, so it is simply omitted
 * rather than stubbed: these macros are needed only so that the functions
 * AROUND the ones under test still compile and link. m8_input_from_pad(),
 * r7_input_from_pad() and m8_input_selftest() touch none of them. */
u16 io_registers[512];
#define read_ioreg(regnum)        (io_registers[(regnum)])
#define write_ioreg(regnum, val)  (io_registers[(regnum)] = (u16)(val))

/* ---- gpsp/cpu.h:113-115, inert here -------------------------------------
 * The keypad IRQ path is NOT under test in this harness and is NOT modified by
 * R7: m8_input_irq_edge() consumes 10-bit GBA key masks, never pad bits, so
 * aliasing cannot reach it. These exist to satisfy the linker. */
static unsigned int irq_flagged = 0u;
static void flag_interrupt(unsigned int mask)   { irq_flagged |= mask; }
static void check_and_raise_interrupts(void)    { (void)irq_flagged; }

/* ---- gpsp/input.h:27-37, transcribed (see the caveat in the banner) ------ */
typedef enum
{
  BUTTON_L      = 0x200,
  BUTTON_R      = 0x100,
  BUTTON_DOWN   = 0x80,
  BUTTON_UP     = 0x40,
  BUTTON_LEFT   = 0x20,
  BUTTON_RIGHT  = 0x10,
  BUTTON_START  = 0x08,
  BUTTON_SELECT = 0x04,
  BUTTON_B      = 0x02,
  BUTTON_A      = 0x01,
  BUTTON_NONE   = 0x00
} input_buttons_type;

/* ---- THE PRODUCTION UNITS, INCLUDED VERBATIM ---------------------------- */
#include "gba_input.c"      /* FROZEN. Not edited by R7. The only authority   */
#include "gba_r7input.c"    /* R7's alias layer, the unit under test          */

/* ========================================================================= */

static int failures = 0;
static int checks   = 0;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void ck_u(unsigned int got, unsigned int want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s -- got 0x%03X, want 0x%03X\n", what, got, want);
    }
}

/* Whole-string compare, so class names are matched in full. Written out rather
 * than pulling <string.h> in for one call. */
static int streq(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return *a == *b;
}

/* =========================================================================
 * THE INDEPENDENT SPECIFICATION
 *
 * ***** THIS TABLE IS WRITTEN FROM THE R7 REQUIREMENT, NOT FROM THE
 * IMPLEMENTATION. ***** It is the approved mapping, restated by hand:
 *
 *     D-pad                 -> D-pad
 *     CROSS, CIRCLE         -> A
 *     SQUARE, TRIANGLE      -> B
 *     L1 -> L,  R1 -> R,  OPTIONS -> START,  TOUCHPAD -> SELECT
 *     L2, R2, L3, R3        -> nothing
 *
 * Comparing the implementation against a table derived from the same source
 * code would be circular; comparing it against THIS is a test. Every one of
 * the sixteen named DualSense bits appears exactly once, so a button silently
 * dropped from the requirement cannot hide.
 *
 * `cls` names the permitted EQUIVALENCE CLASS. Two bits may produce the same
 * non-zero GBA key IF AND ONLY IF they carry the same class name -- that is
 * the property Section 8 enforces, and it is what makes the verifier reject
 * aliases nobody asked for.
 * ========================================================================= */
struct r7_spec {
    unsigned int  pad;    /* the DualSense bit                               */
    unsigned int  key;    /* the GBA key it must produce, or 0 for none      */
    const char   *name;   /* for the failure message                         */
    const char   *cls;    /* permitted equivalence class                     */
};

static const struct r7_spec r7_expect[] = {
    { M8_PAD_UP,       M8_KEY_UP,     "UP",       "UP"     },
    { M8_PAD_DOWN,     M8_KEY_DOWN,   "DOWN",     "DOWN"   },
    { M8_PAD_LEFT,     M8_KEY_LEFT,   "LEFT",     "LEFT"   },
    { M8_PAD_RIGHT,    M8_KEY_RIGHT,  "RIGHT",    "RIGHT"  },

    /* ***** THE TWO INTENTIONAL ALIASES, AND THE ONLY TWO. ***** */
    { M8_PAD_CROSS,    M8_KEY_A,      "CROSS",    "A"      },
    { M8_PAD_CIRCLE,   M8_KEY_A,      "CIRCLE",   "A"      },
    { M8_PAD_SQUARE,   M8_KEY_B,      "SQUARE",   "B"      },
    { M8_PAD_TRIANGLE, M8_KEY_B,      "TRIANGLE", "B"      },

    { M8_PAD_L1,       M8_KEY_L,      "L1",       "L"      },
    { M8_PAD_R1,       M8_KEY_R,      "R1",       "R"      },
    { M8_PAD_OPTIONS,  M8_KEY_START,  "OPTIONS",  "START"  },
    { M8_PAD_TOUCHPAD, M8_KEY_SELECT, "TOUCHPAD", "SELECT" },

    /* ***** NOT ORDINARY GBA BUTTONS, AND MUST NEVER BECOME ONE. *****
     * L2 and R2 are members of the return chord ONLY. */
    { M8_PAD_L2,       0u,            "L2",       "none"   },
    { M8_PAD_R2,       0u,            "R2",       "none"   },
    { M8_PAD_L3,       0u,            "L3",       "none"   },
    { M8_PAD_R3,       0u,            "R3",       "none"   }
};

#define SPEC_N ((unsigned int)(sizeof(r7_expect) / sizeof(r7_expect[0])))

/* The specification as a FUNCTION: the GBA mask a pad word must produce.
 * Bits with no entry (bit 0 and bits 16-19 inside the 21-bit reportable mask)
 * contribute nothing, which is itself part of the requirement. */
static unsigned int spec_from_pad(unsigned int pad, int connected)
{
    unsigned int keys = 0u;
    unsigned int i;

    if (!connected)
        return 0u;

    for (i = 0u; i < SPEC_N; i++)
        if (pad & r7_expect[i].pad)
            keys |= r7_expect[i].key;

    return keys;
}

/* Every bit runtime/platform.c:501 can actually report. */
#define REPORTED_BITS 21u

int main(void)
{
    unsigned int w, i, j, b;
    unsigned int st;
    unsigned long swept = 0ul;
    int reported;

    printf("R7 gameplay alias equivalence -- exhaustive\n");

    /* ---- 1. THE FROZEN INVARIANTS STILL HOLD, UNWEAKENED ----------------
     * This is the layer R7 builds ON TOP OF rather than replaces. A zero mask
     * means every one of the frozen self-test's rows passed -- INCLUDING
     * M8_ST_UNIQUE (the table is still injective in both directions) and
     * M8_ST_UNMAPPED (SQUARE, TRIANGLE, L2, R2, L3 and R3 still map to
     * NOTHING in the table itself). If R7 had edited the table, this would
     * fail here, loudly, before any alias was examined. */
    printf("1. frozen m8_input_selftest()\n");
    st = m8_input_selftest();
    ck_u(st, 0u, "m8_input_selftest() must be 0 -- the frozen table is intact");
    if (st & M8_ST_UNIQUE)
        printf("  NOTE: M8_ST_UNIQUE set -- the TABLE was aliased. R7 must "
               "alias the PAD WORD, never the table.\n");
    if (st & M8_ST_UNMAPPED)
        printf("  NOTE: M8_ST_UNMAPPED set -- a row for SQUARE/TRIANGLE/L2/R2/"
               "L3/R3 appeared in the frozen table.\n");

    /* ---- 2. THE NAMED SINGLE-BUTTON REQUIREMENTS ------------------------ */
    printf("2. single buttons\n");
    ck_u(r7_input_from_pad(M8_PAD_CROSS,    1), M8_KEY_A,      "X -> A");
    ck_u(r7_input_from_pad(M8_PAD_CIRCLE,   1), M8_KEY_A,      "O -> A");
    ck_u(r7_input_from_pad(M8_PAD_SQUARE,   1), M8_KEY_B,      "SQUARE -> B");
    ck_u(r7_input_from_pad(M8_PAD_TRIANGLE, 1), M8_KEY_B,      "TRIANGLE -> B");
    ck_u(r7_input_from_pad(M8_PAD_L1,       1), M8_KEY_L,      "L1 -> L");
    ck_u(r7_input_from_pad(M8_PAD_R1,       1), M8_KEY_R,      "R1 -> R");
    ck_u(r7_input_from_pad(M8_PAD_OPTIONS,  1), M8_KEY_START,  "OPTIONS -> START");
    ck_u(r7_input_from_pad(M8_PAD_TOUCHPAD, 1), M8_KEY_SELECT, "TOUCHPAD -> SELECT");
    ck_u(r7_input_from_pad(M8_PAD_UP,       1), M8_KEY_UP,     "UP -> UP");
    ck_u(r7_input_from_pad(M8_PAD_DOWN,     1), M8_KEY_DOWN,   "DOWN -> DOWN");
    ck_u(r7_input_from_pad(M8_PAD_LEFT,     1), M8_KEY_LEFT,   "LEFT -> LEFT");
    ck_u(r7_input_from_pad(M8_PAD_RIGHT,    1), M8_KEY_RIGHT,  "RIGHT -> RIGHT");

    /* ---- 3. THE NAMED COMBINATIONS --------------------------------------
     * ***** TWO PHYSICAL BUTTONS FOR ONE GBA KEY MUST PRODUCE ONE ASSERTED
     * BIT. ***** A GBA keypad has one A and one B; there is no second bit for
     * a second button to set, and these are the cases that would expose a
     * scheme that tried to give it one. */
    printf("3. simultaneous aliases\n");
    ck_u(r7_input_from_pad(M8_PAD_CROSS | M8_PAD_CIRCLE, 1),
         M8_KEY_A, "X + O -> exactly one A");
    ck_u(r7_input_from_pad(M8_PAD_SQUARE | M8_PAD_TRIANGLE, 1),
         M8_KEY_B, "SQUARE + TRIANGLE -> exactly one B");
    ck_u(r7_input_from_pad(M8_PAD_CROSS | M8_PAD_SQUARE, 1),
         M8_KEY_A | M8_KEY_B, "X + SQUARE -> A and B");
    ck_u(r7_input_from_pad(M8_PAD_CIRCLE | M8_PAD_TRIANGLE, 1),
         M8_KEY_A | M8_KEY_B, "O + TRIANGLE -> A and B");
    ck_u(r7_input_from_pad(M8_PAD_CROSS | M8_PAD_TRIANGLE, 1),
         M8_KEY_A | M8_KEY_B, "X + TRIANGLE -> A and B");
    ck_u(r7_input_from_pad(M8_PAD_CIRCLE | M8_PAD_SQUARE, 1),
         M8_KEY_A | M8_KEY_B, "O + SQUARE -> A and B");
    ck_u(r7_input_from_pad(M8_PAD_CROSS | M8_PAD_CIRCLE |
                           M8_PAD_SQUARE | M8_PAD_TRIANGLE, 1),
         M8_KEY_A | M8_KEY_B, "all four face buttons -> exactly A and B");
    ck_u(r7_input_from_pad(M8_PAD_UP | M8_PAD_CROSS, 1),
         M8_KEY_UP | M8_KEY_A, "UP + X -> UP and A (the M8 named case)");

    /* ---- 4. L2/R2/L3/R3 ARE NOT GAMEPLAY BUTTONS ------------------------ */
    printf("4. the four that must stay silent\n");
    ck_u(r7_input_from_pad(M8_PAD_L2, 1), 0u, "L2 alone -> nothing");
    ck_u(r7_input_from_pad(M8_PAD_R2, 1), 0u, "R2 alone -> nothing");
    ck_u(r7_input_from_pad(M8_PAD_L3, 1), 0u, "L3 alone -> nothing");
    ck_u(r7_input_from_pad(M8_PAD_R3, 1), 0u, "R3 alone -> nothing");
    ck_u(r7_input_from_pad(M8_PAD_L2 | M8_PAD_R2, 1), 0u,
         "L2 + R2 -> nothing (the chord's own two shoulders)");
    ck_u(r7_input_from_pad(M8_PAD_L2 | M8_PAD_R2 | M8_PAD_L3 | M8_PAD_R3, 1),
         0u, "L2 + R2 + L3 + R3 -> nothing");

    /* ---- 5. THE RETURN CHORD INJECTS NOTHING EXTRA ----------------------
     * The chord itself is tested against the RAW pad word in
     * apps/m16cgpsp/main.c and never passes through this layer. What matters
     * here is the other direction: while the operator holds all four
     * shoulders, the GBA must see L and R -- exactly as it did before R7 --
     * and NOTHING ELSE. L2 and R2 must contribute no key. */
    printf("5. the return chord's gameplay footprint\n");
    ck_u(r7_input_from_pad(M8_PAD_L1 | M8_PAD_R1 |
                           M8_PAD_L2 | M8_PAD_R2, 1),
         M8_KEY_L | M8_KEY_R,
         "L1+R1+L2+R2 -> exactly L and R, no phantom key");
    ck_u(r7_input_from_pad(M8_PAD_L1 | M8_PAD_R1, 1),
         M8_KEY_L | M8_KEY_R, "L1 + R1 -> L and R");

    /* ---- 6. A DROPPED READ SYNTHESISES NOTHING, EXHAUSTIVELY ------------ */
    printf("6. disconnected, over every reportable word\n");
    reported = 0;
    for (w = 0u; w < (1u << REPORTED_BITS); w++) {
        if (r7_input_from_pad(w, 0) != 0u && !reported) {
            printf("  FAIL: disconnected pad produced keys for word "
                   "0x%06X\n", w);
            reported = 1;
        }
    }
    ck(!reported, "no disconnected pad word produced a key");
    ck(r7_input_from_pad(0xFFFFFFFFu, 0) == 0u,
       "every button pressed, disconnected -> neutral keypad");

    /* ---- 7. THE EXHAUSTIVE SWEEP ----------------------------------------
     * ***** ALL 2,097,152 REPORTABLE PAD WORDS, AGAINST THE INDEPENDENT
     * SPECIFICATION. ***** Not a sample. This is the check that makes the
     * equivalence classes a proved property of the shipping function rather
     * than a claim about the cases someone thought to list. */
    printf("7. exhaustive sweep of all 2^21 reportable pad words\n");
    reported = 0;
    for (w = 0u; w < (1u << REPORTED_BITS); w++) {
        unsigned int got  = r7_input_from_pad(w, 1);
        unsigned int want = spec_from_pad(w, 1);

        swept++;

        if (got != want && !reported) {
            printf("  FAIL: pad word 0x%06X -> 0x%03X, spec says 0x%03X\n",
                   w, got, want);
            reported = 1;
        }

        /* Inside the 10-bit GBA domain, always. A stray high bit would clear
         * a REG_P1 bit no GBA button owns. */
        if ((got & ~M8_KEY_MASK) && reported < 2) {
            printf("  FAIL: pad word 0x%06X produced out-of-domain 0x%X\n",
                   w, got);
            reported = 2;
        }
    }
    ck(reported == 0, "every one of the 2^21 words matches the specification");
    ck(swept == (1ul << REPORTED_BITS), "the sweep really covered 2^21 words");

    /* ---- 8. ***** THE PARTITION: REJECT UNINTENDED ALIASES ***** --------
     * THIS IS THE INVARIANT THAT REPLACES NOTHING AND ADDS PRECISION.
     *
     * M8_ST_UNIQUE says "no two buttons may share a key". R7 needs a strictly
     * finer statement: "two buttons may share a key IF AND ONLY IF the
     * requirement says so". Both directions are checked over every ordered
     * pair of the 21 reportable bits:
     *
     *   same declared class  => they MUST produce the same non-zero key
     *   different class      => they MUST NOT produce the same non-zero key
     *
     * So TOUCHPAD quietly drifting onto START, or a fifth button joining
     * class A, fails here -- neither of which M8_ST_UNIQUE alone could have
     * said anything about once aliasing was permitted at all. */
    printf("8. the permitted equivalence classes, and no others\n");
    for (i = 0u; i < REPORTED_BITS; i++) {
        unsigned int bit_i = 1u << i;
        unsigned int key_i = r7_input_from_pad(bit_i, 1);
        const char  *cls_i = "none";

        for (b = 0u; b < SPEC_N; b++)
            if (r7_expect[b].pad == bit_i) cls_i = r7_expect[b].cls;

        for (j = i + 1u; j < REPORTED_BITS; j++) {
            unsigned int bit_j = 1u << j;
            unsigned int key_j = r7_input_from_pad(bit_j, 1);
            const char  *cls_j = "none";
            int same_cls, same_key;

            for (b = 0u; b < SPEC_N; b++)
                if (r7_expect[b].pad == bit_j) cls_j = r7_expect[b].cls;

            same_cls = streq(cls_i, cls_j);
            same_key = (key_i == key_j) && (key_i != 0u);

            checks++;
            if (same_key && !same_cls) {
                failures++;
                printf("  FAIL: UNINTENDED ALIAS -- pad bits 0x%06X and "
                       "0x%06X both produce 0x%03X\n", bit_i, bit_j, key_i);
            }
            if (same_cls && !same_key && key_i != 0u) {
                failures++;
                printf("  FAIL: DECLARED ALIAS MISSING -- pad bits 0x%06X "
                       "and 0x%06X should share a key (0x%03X vs 0x%03X)\n",
                       bit_i, bit_j, key_i, key_j);
            }
        }
    }

    /* ---- 9. COMPOSITION IS OR, NOT PRIORITY -----------------------------
     * r7(a|b) == r7(a) | r7(b) over every ordered pair of reportable bits.
     * This is what makes "hold two buttons, get two keys" true in general,
     * and -- where the two buttons are aliases -- what makes "hold two
     * buttons, get ONE key" fall out as A|A == A rather than as a special
     * case someone had to write. */
    printf("9. composition over every pair of reportable bits\n");
    for (i = 0u; i < REPORTED_BITS; i++) {
        for (j = 0u; j < REPORTED_BITS; j++) {
            unsigned int a = 1u << i, c = 1u << j;
            unsigned int both = r7_input_from_pad(a | c, 1);
            unsigned int sep  = r7_input_from_pad(a, 1) |
                                r7_input_from_pad(c, 1);
            checks++;
            if (both != sep) {
                failures++;
                printf("  FAIL: composition 0x%06X|0x%06X -> 0x%03X, "
                       "expected 0x%03X\n", a, c, both, sep);
            }
        }
    }

    /* ---- 10. THE ALIASES ARE ALIASES, NOT A COLLAPSE --------------------
     * A implementation that mapped EVERYTHING to A would pass several checks
     * above. These state the separations explicitly. */
    printf("10. the classes stay distinct\n");
    ck(r7_input_from_pad(M8_PAD_CROSS, 1) !=
       r7_input_from_pad(M8_PAD_SQUARE, 1), "A and B are different keys");
    ck(r7_input_from_pad(M8_PAD_L1, 1) !=
       r7_input_from_pad(M8_PAD_R1, 1), "L and R are different keys");
    ck(r7_input_from_pad(M8_PAD_OPTIONS, 1) !=
       r7_input_from_pad(M8_PAD_TOUCHPAD, 1),
       "START and SELECT are different keys");
    ck_u(r7_input_from_pad(0u, 1), 0u, "no buttons -> no keys");

    /* ---- the verdict ----------------------------------------------------- */
    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("R7 INPUT EQUIV FAILED\n");
        return 1;
    }
    printf("R7 INPUT EQUIV PASSED -- the two declared equivalence classes "
           "hold over all 2^21 words, the frozen table is unmodified and "
           "still injective, and no unintended alias exists\n");
    return 0;
}
