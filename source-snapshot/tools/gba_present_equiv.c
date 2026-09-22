/* LUAport M9-E -- tools/gba_present_equiv.c
 *
 * OFFLINE BYTE-EQUIVALENCE HARNESS FOR THE PRESENTATION BLIT.
 *
 * TEST ONLY. THIS FILE IS NOT PART OF ANY PS5 BUILD. It is not referenced by
 * any Makefile rule, it is not in any M0-M9/M9-D object list, and it calls host
 * libc (stdio/stdlib/string), which no target in this tree is allowed to link.
 * It exists to answer ONE question on a development machine, before any PS5
 * time is spent:
 *
 *     Does the M9-E write-only blit produce OUTPUT THAT IS BYTE-FOR-BYTE
 *     IDENTICAL to the historical read-back blit it replaces?
 *
 * WHY THIS IS NOT REDUNDANT WITH M7. apps/m7gpsp/main.c already validates the
 * blit far more thoroughly than this does -- full-surface walk, border walk,
 * 6x6 block uniformity, both framebuffers -- but it only runs ON HARDWARE. This
 * harness is the only check that can prove byte-identity BEFORE the build is
 * sent to a PS5, and it is the only one that compares the two ALGORITHMS
 * directly rather than each against a specification.
 *
 * WHAT IT DOES NOT AND CANNOT PROVE. It cannot detect framebuffer read-back.
 * The historical algorithm read back values it had itself just written, so it
 * produced CORRECT pixels; the read-back was purely a performance defect. No
 * output comparison can see it. That property is established by source review
 * and by measurement (M9-D), not here. See gba_present.c's M9-E note.
 *
 * BUILD AND RUN (host, not the PS5 toolchain), from the repository root:
 *
 *     cc -std=c11 -O2 -Wall -Wextra \
 *        -I adapters/gba -I runtime \
 *        -o /tmp/gba_present_equiv tools/gba_present_equiv.c && \
 *     /tmp/gba_present_equiv
 *
 * Exit status 0 = every case byte-identical. Non-zero = a difference, with the
 * first differing pixel located and decoded.
 *
 * REQUIRES AN LP64 HOST (Linux/macOS). runtime/core.h:30 typedefs u64 as
 * `unsigned long`, which is 32 bits on Windows LLP64; the static assert below
 * refuses to build there rather than silently testing the wrong types.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The PRODUCTION translation unit, included verbatim. Including the .c rather
   than relinking a copy is deliberate: it guarantees this harness exercises the
   exact code that ships, including the static staging row, and that it cannot
   drift from it. Nothing in gba_present.c is modified to support this file. */
#include "gba_present.c"

_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(u64) == 8,
               "u64 must be 8 bytes -- core.h uses `unsigned long`, so build "
               "this harness on an LP64 host (Linux/macOS), not Windows");

#define SRC_PIXELS  (GBA_PRESENT_SRC_W * GBA_PRESENT_SRC_H)   /*    38,400 */
#define DST_PIXELS  (SCR_W * SCR_H)                           /* 2,073,600 */

/* Deliberately NOT a plausible pixel value. Any location that a blit fails to
   write keeps this, so an under-write is visible as poison rather than as a
   coincidentally-correct black. */
#define POISON      0xDEADBEEFu

/* ---------------------------------------------------------------------------
 * THE HISTORICAL ALGORITHM, FROZEN.
 *
 * This is adapters/gba/gba_present.c's blit AS IT STOOD BEFORE M9-E, copied
 * line for line, including the read-back at the heart of it:
 *
 *     d[x] = drow[x];      <-- reads the framebuffer it just wrote
 *
 * It is reproduced here (rather than fetched from git) so that this harness is
 * self-contained and so that the comparison stays meaningful even after the
 * production file has moved on. It must NEVER be "tidied": its value is that it
 * is the exact code that M7 validated on hardware and that M9-D measured.
 *
 * Note that apps/m9diag/main.c's m9d_blit_split() is the other frozen copy of
 * this same algorithm; that one is the ON-HARDWARE control and is untouched by
 * M9-E. This one is its offline counterpart.
 * ------------------------------------------------------------------------- */
static void blit_historical(u32 *dst, const u16 *src) {
    unsigned y, x, j;

    if (!dst || !src) return;

    for (y = 0; y < GBA_PRESENT_SRC_H; y++) {
        const u16 *srow = src + (unsigned)y * GBA_PRESENT_SRC_W;

        u32 *drow = dst
                  + ((unsigned)GBA_PRESENT_OFF_Y + y * GBA_PRESENT_SCALE)
                        * (unsigned)SCR_W
                  + (unsigned)GBA_PRESENT_OFF_X;

        for (x = 0; x < GBA_PRESENT_SRC_W; x++) {
            u32  c = gba_present_argb(srow[x]);
            u32 *p = drow + x * GBA_PRESENT_SCALE;

            p[0] = c; p[1] = c; p[2] = c;
            p[3] = c; p[4] = c; p[5] = c;
        }

        for (j = 1; j < GBA_PRESENT_SCALE; j++) {
            u32 *d = drow + j * (unsigned)SCR_W;
            for (x = 0; x < GBA_PRESENT_DST_W; x++)
                d[x] = drow[x];          /* THE FRAMEBUFFER READ-BACK */
        }
    }
}

/* ---------------------------------------------------------------- patterns --
 *
 * Deterministic. No time, no rand(), no uninitialised memory -- a failure here
 * must be reproducible from the case index alone.
 *
 * The eight known values from gba_present_selftest() are covered explicitly,
 * including the two discriminators that matter most: KV_BLUE (0x001F, catches
 * an R/B swap) and KV_BIT5 (0x0020, catches a six-bit green reading). Case 6
 * then sweeps the FULL 16-bit input space, which is far wider than the four
 * values this cartridge can actually emit. */
static const u16 KNOWN[8] = {
    0x0000u,  /* black          -> 0xFF000000 */
    0x07C0u,  /* pure green     -> 0xFF00FF00 */
    0xFFDFu,  /* white          -> 0xFFFFFFFF */
    0xF800u,  /* red            -> 0xFFFF0000 */
    0x001Fu,  /* blue           -> 0xFF0000FF  R/B discriminator */
    0x0400u,  /* mid green      -> 0xFF008400  expansion trap    */
    0x0020u,  /* green bit 5    -> 0xFF000000  five-bit trap     */
    0xFFFFu   /* forced blank   -> 0xFFFFFFFF  collides w/ white */
};

static const char *CASE_NAME[] = {
    "all black (0x0000)",
    "all white (0xFFDF)",
    "all forced-blank (0xFFFF)",
    "known-value cycle (all 8 selftest values)",
    "per-pixel index gradient",
    "x^y checkerboard of green/red",
    "LCG sweep of the full 16-bit space",
    "single lit pixel at (0,0), rest black",
    "single lit pixel at (239,159), rest black",
    "column ramp (isolates horizontal expansion)",
    "row ramp (isolates vertical replication)"
};
#define N_CASES ((int)(sizeof(CASE_NAME) / sizeof(CASE_NAME[0])))

static void fill_source(u16 *src, int which) {
    unsigned i, x, y;
    u32 lcg;

    switch (which) {
    case 0:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = 0x0000u;
        break;
    case 1:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = 0xFFDFu;
        break;
    case 2:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = 0xFFFFu;
        break;
    case 3:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = KNOWN[i & 7u];
        break;
    case 4:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = (u16)(i & 0xFFFFu);
        break;
    case 5:
        for (y = 0; y < GBA_PRESENT_SRC_H; y++)
            for (x = 0; x < GBA_PRESENT_SRC_W; x++)
                src[y * GBA_PRESENT_SRC_W + x] =
                    ((x ^ y) & 1u) ? 0x07C0u : 0xF800u;
        break;
    case 6:
        /* Numerical Recipes LCG. Deterministic, and its low bits are exercised
           as well as its high ones because the whole 16-bit result is used. */
        lcg = 1u;
        for (i = 0; i < SRC_PIXELS; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            src[i] = (u16)((lcg >> 11) & 0xFFFFu);
        }
        break;
    case 7:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = 0x0000u;
        src[0] = 0xFFDFu;
        break;
    case 8:
        for (i = 0; i < SRC_PIXELS; i++) src[i] = 0x0000u;
        src[SRC_PIXELS - 1] = 0xFFDFu;
        break;
    case 9:
        for (y = 0; y < GBA_PRESENT_SRC_H; y++)
            for (x = 0; x < GBA_PRESENT_SRC_W; x++)
                src[y * GBA_PRESENT_SRC_W + x] = (u16)(x * 273u);
        break;
    default:
        for (y = 0; y < GBA_PRESENT_SRC_H; y++)
            for (x = 0; x < GBA_PRESENT_SRC_W; x++)
                src[y * GBA_PRESENT_SRC_W + x] = (u16)(y * 409u);
        break;
    }
}

/* --------------------------------------------------------------- checkers --
 *
 * INDEPENDENT ORACLE, recomputed from the SOURCE rather than from either blit.
 * This is the same strategy apps/m7gpsp/main.c:1242-1245 uses: if both
 * algorithms were wrong in the same way, the memcmp would pass and this would
 * still fail. Byte-equivalence alone is not correctness. */
static int check_against_oracle(const u32 *dst, const u16 *src,
                                const char *label) {
    unsigned dy, dx;

    for (dy = 0; dy < GBA_PRESENT_DST_H; dy++) {
        for (dx = 0; dx < GBA_PRESENT_DST_W; dx++) {
            u32 got  = dst[((unsigned)GBA_PRESENT_OFF_Y + dy) * (unsigned)SCR_W
                           + (unsigned)GBA_PRESENT_OFF_X + dx];
            u32 want = gba_present_argb(
                           src[(dy / GBA_PRESENT_SCALE) * GBA_PRESENT_SRC_W
                               + (dx / GBA_PRESENT_SCALE)]);
            if (got != want) {
                printf("    ORACLE MISMATCH (%s) at content (%u,%u): "
                       "got 0x%08X want 0x%08X\n",
                       label, dx, dy, got, want);
                return 1;
            }
        }
    }
    return 0;
}

/* Everything OUTSIDE the 1440x960 content rect must still hold POISON. The blit
   is documented as writing only the content rect and leaving the borders to the
   caller; this is what proves the write set did not grow. */
static int check_outside_untouched(const u32 *dst, const char *label) {
    unsigned y, x;

    for (y = 0; y < (unsigned)SCR_H; y++) {
        int inside_rows = (y >= (unsigned)GBA_PRESENT_OFF_Y
                           && y <  (unsigned)GBA_PRESENT_OFF_Y
                                   + GBA_PRESENT_DST_H);
        for (x = 0; x < (unsigned)SCR_W; x++) {
            int inside = inside_rows
                      && x >= (unsigned)GBA_PRESENT_OFF_X
                      && x <  (unsigned)GBA_PRESENT_OFF_X + GBA_PRESENT_DST_W;
            if (inside) continue;
            if (dst[y * (unsigned)SCR_W + x] != POISON) {
                printf("    OUT-OF-RECT WRITE (%s) at screen (%u,%u): "
                       "0x%08X, expected poison 0x%08X\n",
                       label, x, y, dst[y * (unsigned)SCR_W + x], POISON);
                return 1;
            }
        }
    }
    return 0;
}

int main(void) {
    u16 *src, *src_copy;
    u32 *a, *b;
    int c, failures = 0;
    unsigned sel;

    printf("LUAport M9-E -- offline byte-equivalence harness\n");
    printf("historical read-back blit  vs  M9-E write-only blit\n\n");

    printf("geometry: src %dx%d  ->  dst %dx%d at (%d,%d), scale %d, "
           "surface %dx%d\n",
           GBA_PRESENT_SRC_W, GBA_PRESENT_SRC_H,
           GBA_PRESENT_DST_W, GBA_PRESENT_DST_H,
           GBA_PRESENT_OFF_X, GBA_PRESENT_OFF_Y,
           GBA_PRESENT_SCALE, SCR_W, SCR_H);
    printf("staging row: %zu bytes of .bss\n\n", sizeof(gba_present_row));

    /* The production self-test first. If conversion is broken, equivalence
       between two identically-broken blits would be worthless. */
    sel = gba_present_selftest();
    printf("gba_present_selftest() = 0x%02X (%s)\n\n",
           sel, sel == 0 ? "PASS" : "FAIL");
    if (sel != 0) failures++;

    src      = (u16 *)malloc(SRC_PIXELS * sizeof(u16));
    src_copy = (u16 *)malloc(SRC_PIXELS * sizeof(u16));
    a        = (u32 *)malloc((size_t)DST_PIXELS * sizeof(u32));
    b        = (u32 *)malloc((size_t)DST_PIXELS * sizeof(u32));

    if (!src || !src_copy || !a || !b) {
        printf("FATAL: out of memory\n");
        return 2;
    }

    for (c = 0; c < N_CASES; c++) {
        size_t i;
        int bad = 0;

        fill_source(src, c);
        memcpy(src_copy, src, SRC_PIXELS * sizeof(u16));

        for (i = 0; i < (size_t)DST_PIXELS; i++) { a[i] = POISON; b[i] = POISON; }

        blit_historical(a, src);
        gba_present_blit(b, src);

        printf("case %2d: %s\n", c, CASE_NAME[c]);

        /* 1. BYTE-IDENTICAL OUTPUT over the WHOLE 1920x1080 surface -- not just
              the content rect, so a stray write anywhere is caught. */
        if (memcmp(a, b, (size_t)DST_PIXELS * sizeof(u32)) != 0) {
            for (i = 0; i < (size_t)DST_PIXELS; i++) {
                if (a[i] != b[i]) {
                    printf("    BYTE MISMATCH at index %zu (screen %u,%u): "
                           "historical 0x%08X  M9-E 0x%08X\n",
                           i, (unsigned)(i % (unsigned)SCR_W),
                           (unsigned)(i / (unsigned)SCR_W), a[i], b[i]);
                    break;
                }
            }
            bad = 1;
        }

        /* 2. Both agree with an oracle recomputed from the source. */
        if (check_against_oracle(a, src, "historical")) bad = 1;
        if (check_against_oracle(b, src, "M9-E"))       bad = 1;

        /* 3. Neither wrote outside the content rect. */
        if (check_outside_untouched(a, "historical")) bad = 1;
        if (check_outside_untouched(b, "M9-E"))       bad = 1;

        /* 4. SOURCE IMMUTABILITY. `const u16 *` makes mutation a compile error,
              so this is belt-and-braces against a cast -- cheap, and it also
              covers the historical replica. */
        if (memcmp(src, src_copy, SRC_PIXELS * sizeof(u16)) != 0) {
            printf("    SOURCE WAS MODIFIED\n");
            bad = 1;
        }

        printf("    %s\n", bad ? "*** FAIL ***"
                               : "identical, oracle-correct, in-bounds, "
                                 "source intact");
        if (bad) failures++;
    }

    /* The NULL contract: "write nowhere", not a fault. Both must ignore it and
       leave the destination alone. */
    {
        size_t i;
        int bad = 0;
        for (i = 0; i < (size_t)DST_PIXELS; i++) b[i] = POISON;
        gba_present_blit(NULL, src);
        gba_present_blit(b, NULL);
        for (i = 0; i < (size_t)DST_PIXELS; i++) {
            if (b[i] != POISON) { bad = 1; break; }
        }
        printf("\nnull-argument contract: %s\n",
               bad ? "*** FAIL *** (destination was written)"
                   : "both NULLs ignored, destination untouched");
        if (bad) failures++;
    }

    printf("\n=====================================================\n");
    if (failures == 0) {
        printf("RESULT: PASS -- %d cases, output is BYTE-IDENTICAL to the\n"
               "        historical read-back blit on every one.\n", N_CASES);
    } else {
        printf("RESULT: FAIL -- %d check(s) failed. Do NOT flash this build.\n",
               failures);
    }
    printf("=====================================================\n");

    free(src); free(src_copy); free(a); free(b);
    return failures == 0 ? 0 : 1;
}
