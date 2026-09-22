/* LUAport M10 -- tools/gba_audio_equiv.c
 *
 * =============================================================================
 * OFFLINE CORRECTNESS HARNESS FOR THE 512:375 AUDIO DECIMATOR.
 * =============================================================================
 *
 * TEST ONLY. THIS FILE IS NOT PART OF ANY PS5 BUILD. No Makefile rule references
 * tools/*.c, it is in no M0-M10 object list, and it calls host libc (stdio),
 * which no target in this tree is allowed to link. It is the direct counterpart
 * of tools/gba_present_equiv.c and exists for the same reason: to answer a
 * question on a development machine BEFORE any PS5 time is spent.
 *
 * IT TESTS THE PRODUCTION TRANSLATION UNIT, NOT A COPY.
 * adapters/gba/gba_audio.c is #included verbatim below. A re-implementation of
 * the decimator here would prove only that two pieces of code written in the
 * same hour agree; including the shipping file guarantees the algorithm under
 * test IS the algorithm that runs on the console, including its static ring and
 * its static resampler state.
 *
 * HOW THE gpSP DEPENDENCY IS NEUTRALISED.
 * gba_audio.c opens with #include "common.h", which on the console pulls the
 * whole gpSP world. Defining gpsp/common.h's own include guard COMMON_H before
 * the include makes that a no-op, and this harness then supplies the four
 * typedefs and the two gpSP prototypes gba_audio.c actually uses. Nothing in
 * adapters/gba/gba_audio.c is modified to support this file, and no #ifdef for
 * the test exists in the production source.
 *
 * -I gpsp IS STILL REQUIRED even though the body is skipped: the preprocessor
 * must LOCATE common.h before the pre-defined guard can suppress it. Omitting
 * the flag fails with "common.h: No such file or directory". The transitive
 * path -Igpsp/libretro/libretro-common/include is NOT needed here, because the
 * body that would have reached those headers never expands.
 *
 * BUILD AND RUN (host, not the PS5 toolchain), from the repository root:
 *
 *     cc -std=c11 -O2 -Wall -Wextra \
 *        -I adapters/gba -I gpsp \
 *        -o /tmp/gba_audio_equiv tools/gba_audio_equiv.c && \
 *     /tmp/gba_audio_equiv
 *
 * Exit status 0 = every property held. Non-zero = the first failure, located
 * and decoded.
 *
 * PORTABLE TO LLP64. Unlike tools/gba_present_equiv.c, this harness does NOT
 * require an LP64 host: it supplies gpSP's typedefs, and gpsp/common.h:100
 * spells u64 `unsigned long long`, which is 64 bits on Windows too. It does not
 * include runtime/core.h and therefore never sees the `unsigned long` u64 that
 * makes the presentation harness LP64-only.
 *
 * =============================================================================
 * WHAT IS PROVEN HERE
 * =============================================================================
 *   1. RATIO      exactly 375 output frames per 512 input frames, per block,
 *                 with no block ever off by one.
 *   2. BOUNDED    the phase accumulator satisfies acc < 512 after every single
 *                 input frame, checked one frame at a time.
 *   3. PERIODIC   acc returns to EXACTLY its entry value every 512 inputs, so
 *                 the converter's own long-term drift is zero rather than
 *                 small. Verified over 4,194,304 input frames.
 *   4. GROUPING   the box group size n is only ever 1 or 2, which is what makes
 *                 the mean a shift instead of a division.
 *   5. ORDER      L stays in even slots and R in odd, end to end, including
 *                 through the grain-staging copy in gba_audio_drain().
 *   6. VECTOR     a hand-computed 13-frame input produces one exact expected
 *                 9-frame output, value by value.
 *   7. CONSERVED  produced == consumed + fill, with zero overruns, so no output
 *                 frame is invented or silently lost.
 *   8. PREFILL    gba_audio_drain() submits nothing until the ring has ONCE
 *                 reached GBA_AUDIO_TARGET_FILL, does not count that deliberate
 *                 wait as an underrun, and then LATCHES -- it never waits again
 *                 even when the ring falls far below the threshold. The latch
 *                 is what keeps it a START CONDITION rather than a rate
 *                 correction, which matters because M10C must contain no servo.
 *
 * WHAT IT CANNOT PROVE. It does not test sceAudioOut, the PS5 backend, gpSP's
 * mixer, or that the cartridge makes a sound. Those are hardware questions and
 * belong to M10A/M10B/M10C.
 */

#include <stdio.h>
#include <string.h>

/* ---- neutralise gpsp/common.h and supply exactly what gba_audio.c uses ---- */
#define COMMON_H

typedef signed short int    s16;
typedef unsigned int        u32;
typedef signed int          s32;
typedef unsigned long long  u64;

/* The two gpSP entry points gba_audio.c calls. Signatures copied from
   gpsp/sound.h:154 and :64. */
static s16 *g_src;          /* scripted source for sound_read_samples          */
static u32  g_src_avail;
u32  sound_read_samples(s16 *out, u32 frames);
void sound_flush_ring(void);

u32 sound_read_samples(s16 *out, u32 frames) {
    u32 i, n = (g_src_avail < frames) ? g_src_avail : frames;
    for (i = 0; i < n * 2u; i++) out[i] = g_src ? g_src[i] : 0;
    g_src_avail -= n;
    return n;
}
void sound_flush_ring(void) { g_src_avail = 0; }

/* The one platform call gba_audio.c makes. Captures grains so channel order can
   be checked all the way through the staging copy. */
#define CAP_GRAINS 64
static s16 cap[CAP_GRAINS][256 * 2];
static int cap_n;
int plat_audio_submit(const short *grain);
int plat_audio_submit(const short *grain) {
    if (cap_n < CAP_GRAINS) memcpy(cap[cap_n], grain, 256 * 2 * sizeof(s16));
    cap_n++;
    return 0;
}

/* ---- THE PRODUCTION TRANSLATION UNIT, VERBATIM --------------------------- */
#include "gba_audio.c"

/* ------------------------------------------------------------------ utils -- */

static int failures;

static void fail(const char *what, long long got, long long want) {
    printf("  FAIL %-46s got %lld want %lld\n", what, got, want);
    failures++;
}
static void ok(const char *what) {
    printf("  ok   %s\n", what);
}

/* Reads output frame i (0 == oldest unconsumed) straight out of the production
   ring, so short sequences that never fill a grain are still inspectable. */
static s16 ring_at(u32 i, u32 ch) {
    return ring[((ring_tail + i) & RING_MASK) * 2u + ch];
}

/* Writes one input frame into the production staging buffer. This is exactly
   what gba_audio_pull() and gba_audio_tone() do. */
static void put_in(u32 i, s32 l, s32 r) {
    pull_buf[i * 2 + 0] = (s16)l;
    pull_buf[i * 2 + 1] = (s16)r;
}

/* ============================== THE TESTS ================================= */

/* 1 + 2 + 3 + 4: ratio, boundedness, periodicity and group size, checked one
   input frame at a time over 8192 full blocks = 4,194,304 input frames. */
static void test_ratio_and_drift(void) {
    const u32 BLOCKS = 8192;
    u32 b, i;
    u64 total_out = 0;
    u32 bad_acc = 0, bad_group = 0, bad_block = 0;
    u32 group_max = 0, group_min = 0xFFFFFFFFu;
    u32 since_emit = 0;

    printf("[1] ratio, accumulator bound, periodicity, group size\n");

    gba_audio_reset();
    cap_n = 0;

    for (b = 0; b < BLOCKS; b++) {
        u64 before = S.produced;

        for (i = 0; i < GBA_AUDIO_DEC_IN; i++) {
            u64 p_before = S.produced;

            /* A varying signal, so the test is not silence-on-silence. */
            put_in(0, (s32)((i * 37u + b) & 0x3FFFu) - 8192,
                      (s32)((i * 53u + b) & 0x3FFFu) - 8192);
            gba_audio_resample(1);

            /* 2. THE INVARIANT, CHECKED AFTER EVERY SINGLE INPUT FRAME. */
            if (R.acc >= GBA_AUDIO_DEC_IN) bad_acc++;

            /* 4. GROUP SIZE, MEASURED AT EMISSION TIME.
                  R.n cannot be read after the call for this: emission resets it
                  to 0, so the post-state is only ever 0 or 1 and a test against
                  it would be vacuous. The real group size is the number of
                  input frames consumed since the previous emission. */
            since_emit++;
            if (S.produced != p_before) {
                if (since_emit > group_max) group_max = since_emit;
                if (since_emit < group_min) group_min = since_emit;
                if (since_emit < 1u || since_emit > 2u) bad_group++;
                since_emit = 0;
            }
        }

        /* 1. EXACTLY 375 OUTPUT FRAMES FROM THIS BLOCK OF 512. */
        if (S.produced - before != GBA_AUDIO_DEC_OUT) bad_block++;
        total_out += S.produced - before;

        /* 3. THE ACCUMULATOR IS BACK WHERE IT STARTED. This is the property
              that makes long-term converter drift exactly zero. */
        if (R.acc != 0u) bad_acc++;

        /* Keep the ring shallow so nothing overruns; 375 frames per block is
           1.46 grains, and draining generously holds fill below one grain. */
        gba_audio_drain(64);
        cap_n = 0;
    }

    if (bad_block) fail("blocks yielding exactly 375 output frames",
                        (long long)(BLOCKS - bad_block), (long long)BLOCKS);
    else           ok("every one of 8192 blocks yielded exactly 375 frames");

    if (total_out != (u64)BLOCKS * GBA_AUDIO_DEC_OUT)
        fail("total output frames", (long long)total_out,
             (long long)((u64)BLOCKS * GBA_AUDIO_DEC_OUT));
    else
        ok("4194304 input frames -> 3072000 output frames, exactly");

    if (bad_acc) fail("accumulator bound / periodicity violations",
                      (long long)bad_acc, 0);
    else         ok("acc < 512 after every input, and == 0 every 512 inputs");

    if (group_max != 2u) fail("max box-group size", (long long)group_max, 2);
    else                 ok("box group size never exceeded 2 (mean is a shift)");

    if (group_min != 1u) fail("min box-group size", (long long)group_min, 1);
    else                 ok("box group size was never 0 (no empty emission)");

    if (bad_group) fail("groups outside {1,2}", (long long)bad_group, 0);
    else           ok("every emission consumed exactly 1 or 2 input frames");

    if (S.overruns) fail("overruns during the drift run",
                         (long long)S.overruns, 0);
    else            ok("zero overruns across the whole drift run");
}

/* 6: the hand-computed vector.
 *
 * With acc starting at 0 and stepping by 375 against 512, the first thirteen
 * input frames group as
 *     [1,2] [3] [4,5] [6] [7] [8,9] [10] [11] [12,13]
 * which is nine output frames. Input frame i carries L = i*100, R = -i*100, so
 * every expected value below is exact with no rounding ambiguity -- each sum is
 * even, so the arithmetic shift and a division would agree. */
static void test_vector(void) {
    static const s16 want_l[9] = { 150, 300, 450, 600, 700, 850, 1000, 1100, 1250 };
    u32 i;
    int bad = 0;

    printf("[2] deterministic 13-frame vector -> 9 exact output frames\n");

    gba_audio_reset();
    for (i = 0; i < 13u; i++)
        put_in(i, (s32)(i + 1u) * 100, -(s32)(i + 1u) * 100);
    gba_audio_resample(13);

    if (S.produced != 9u) {
        fail("output frames from 13 inputs", (long long)S.produced, 9);
        return;
    }
    ok("13 input frames produced exactly 9 output frames");

    for (i = 0; i < 9u; i++) {
        if (ring_at(i, 0) != want_l[i]) {
            fail("left value", ring_at(i, 0), want_l[i]);
            bad = 1;
        }
        if (ring_at(i, 1) != (s16)-want_l[i]) {
            fail("right value", ring_at(i, 1), -want_l[i]);
            bad = 1;
        }
    }
    if (!bad) ok("all nine L and R values match the hand-computed expectation");
}

/* 5: channel order and isolation, through the full path including the grain
 * staging copy inside gba_audio_drain(). A constant input is the sharpest test
 * available here: whether a group holds one frame or two, the box mean of a
 * constant is that same constant, so EVERY output frame must equal it exactly. */
static void test_channel_order(void) {
    u32 i, k;
    int bad_l = 0, bad_r = 0;

    printf("[3] channel order and isolation\n");

    /* (a) constant, distinct per channel, all the way into submitted grains.
     *
     * SIX BLOCKS, NOT FOUR, AND THE COUNT IS LOAD-BEARING.
     * gba_audio_drain() enforces a ONE-SHOT PREFILL: it submits nothing until
     * the ring has once reached GBA_AUDIO_TARGET_FILL (1536 frames). Four
     * blocks of 512 inputs yield 4*512*375/512 = 1500 output frames, which is
     * 36 frames SHORT of that threshold -- so the production code would
     * correctly submit nothing and this test would fail against working code.
     * Six blocks give 2250 frames, clearing the gate with margin.
     *
     * The threshold is not hardcoded here twice: it is asserted below in
     * test_prefill(), so if TARGET_FILL ever changes, the failure names the
     * cause instead of appearing as a mysterious "no grains captured". */
    gba_audio_reset();
    cap_n = 0;
    for (k = 0; k < 6u; k++) {
        for (i = 0; i < 512u; i++) put_in(i, 1000, -2000);
        gba_audio_resample(512);
    }
    gba_audio_drain(GBA_AUDIO_MAX_GRAINS);

    if (cap_n < 1) {
        fail("grains captured from 3072 input frames", cap_n, 1);
    } else {
        for (i = 0; i < 256u; i++) {
            if (cap[0][i * 2 + 0] != 1000)  bad_l = 1;
            if (cap[0][i * 2 + 1] != -2000) bad_r = 1;
        }
        if (bad_l) fail("submitted grain left channel", cap[0][0], 1000);
        if (bad_r) fail("submitted grain right channel", cap[0][1], -2000);
        if (!bad_l && !bad_r)
            ok("L=+1000 R=-2000 survived resample+grain staging exactly");
    }

    /* (b) LEFT-only tone must leave every right sample at zero, and must put a
           real signal in the left. This is the offline half of the M10B
           hardware pan test.

           NOTE ON WHY "left is never zero" WOULD BE THE WRONG ASSERTION: the
           tone is a square wave, and a two-frame box group that straddles an
           edge averages +8000 and -8000 to exactly 0. Zeros in the left channel
           are therefore EXPECTED at transitions. The correct test is that SOME
           left sample is non-zero while EVERY right sample is zero. */
    {
        int saw_l, leak_r;

        gba_audio_reset();
        cap_n = 0;
        for (k = 0; k < 6u; k++) {   /* six, to clear the prefill gate -- see (a) */
            u32 n = gba_audio_tone(512, GBA_AUDIO_TONE_LEFT);
            gba_audio_resample(n);
        }
        gba_audio_drain(GBA_AUDIO_MAX_GRAINS);

        saw_l = 0; leak_r = 0;
        if (cap_n < 1) {
            fail("grains captured from the LEFT-only tone", cap_n, 1);
        } else {
            for (i = 0; i < 256u; i++) {
                if (cap[0][i * 2 + 0] != 0) saw_l = 1;
                if (cap[0][i * 2 + 1] != 0) leak_r = 1;
            }
            if (leak_r)     fail("LEFT-only tone leaked into RIGHT", 1, 0);
            else if (!saw_l) fail("LEFT-only tone produced no left signal", 0, 1);
            else ok("LEFT-only tone: left carries signal, right identically zero");
        }

        /* (c) RIGHT-only, the mirror image */
        gba_audio_reset();
        cap_n = 0;
        for (k = 0; k < 6u; k++) {   /* six, to clear the prefill gate -- see (a) */
            u32 n = gba_audio_tone(512, GBA_AUDIO_TONE_RIGHT);
            gba_audio_resample(n);
        }
        gba_audio_drain(GBA_AUDIO_MAX_GRAINS);

        {
            int saw_r = 0, leak_l = 0;
            if (cap_n < 1) {
                fail("grains captured from the RIGHT-only tone", cap_n, 1);
            } else {
                for (i = 0; i < 256u; i++) {
                    if (cap[0][i * 2 + 1] != 0) saw_r = 1;
                    if (cap[0][i * 2 + 0] != 0) leak_l = 1;
                }
                if (leak_l)      fail("RIGHT-only tone leaked into LEFT", 1, 0);
                else if (!saw_r) fail("RIGHT-only tone produced no right signal", 0, 1);
                else ok("RIGHT-only tone: right carries signal, left identically zero");
            }
        }
    }
}

/* 7: nothing is invented and nothing is lost. */
static void test_conservation(void) {
    u32 k;
    struct gba_audio_stats st;

    printf("[4] frame conservation and the pull path\n");

    gba_audio_reset();
    cap_n = 0;

    for (k = 0; k < 200u; k++) {
        u32 i, n;
        /* Drive the REAL pull path, not just resample: this is the call the
           verifier requires gba_audio.o to make. */
        static s16 src[1098 * 2];
        for (i = 0; i < 1098u; i++) {
            src[i * 2 + 0] = (s16)(((i * 11u + k) & 0x1FFFu) - 4096);
            src[i * 2 + 1] = (s16)(((i * 13u + k) & 0x1FFFu) - 4096);
        }
        g_src = src;
        g_src_avail = 1098;

        n = gba_audio_pull();
        if (n != 1098u) { fail("frames pulled", n, 1098); return; }
        gba_audio_resample(n);
        gba_audio_drain(GBA_AUDIO_MAX_GRAINS);
        cap_n = 0;
    }

    gba_audio_stats(&st);

    if (st.pulled != 200ull * 1098ull)
        fail("total input frames pulled", (long long)st.pulled, 200 * 1098);
    else
        ok("gba_audio_pull() drove sound_read_samples 200 times");

    if (st.consumed + (u64)st.fill != st.produced)
        fail("produced vs consumed+fill", (long long)(st.consumed + st.fill),
             (long long)st.produced);
    else
        ok("produced == consumed + fill (no frame invented or lost)");

    if (st.overruns) fail("overruns", (long long)st.overruns, 0);
    else             ok("zero overruns");

    if (st.peak == 0 || gba_audio_is_silent())
        fail("peak amplitude on a non-silent source", (long long)st.peak, 1);
    else
        ok("level detection reports a non-silent source correctly");

    /* The predicted ratio, as an independent cross-check of the whole path:
       219600 input frames should yield 219600 * 375 / 512 = 160839.8 -> the
       converter emits floor-consistent counts, so allow the one-frame boundary. */
    {
        u64 expect = (200ull * 1098ull * GBA_AUDIO_DEC_OUT) / GBA_AUDIO_DEC_IN;
        long long diff = (long long)st.produced - (long long)expect;
        if (diff < 0) diff = -diff;
        if (diff > 1)
            fail("produced vs 375/512 of pulled", (long long)st.produced,
                 (long long)expect);
        else
            printf("  ok   %llu input -> %llu output (predicted %llu, within 1)\n",
                   (unsigned long long)st.pulled,
                   (unsigned long long)st.produced,
                   (unsigned long long)expect);
    }
}

/* Silence must stay silent AND must still convert at the right rate -- the
   failure mode where a converter "works" only because nothing is playing. */
static void test_silence(void) {
    u32 i;
    printf("[5] silence converts at the correct rate and reports SILENT\n");

    gba_audio_reset();
    for (i = 0; i < 512u; i++) put_in(i, 0, 0);
    gba_audio_resample(512);

    if (S.produced != GBA_AUDIO_DEC_OUT)
        fail("silent block output frames", (long long)S.produced,
             GBA_AUDIO_DEC_OUT);
    else
        ok("a silent 512-frame block still produced exactly 375 frames");

    if (!gba_audio_is_silent())
        fail("is_silent on an all-zero source", 0, 1);
    else
        ok("gba_audio_is_silent() reports SILENT, as M10C requires");
}

/* 8: THE ONE-SHOT PREFILL GATE.
 *
 * gba_audio_drain() must submit NOTHING until the ring has once reached
 * GBA_AUDIO_TARGET_FILL, and must then submit freely for the rest of the run
 * WITHOUT ever waiting again. Both halves are tested, because getting only the
 * first half right would produce a converter that stutters at every dip in the
 * ring -- a servo by accident, which is exactly what M10C must not contain.
 *
 * THE DELIBERATE RETURN OF ZERO MUST NOT COUNT AS AN UNDERRUN. A prefill that
 * inflated the underrun counter would bury the real underruns this milestone
 * exists to detect, so that is asserted explicitly. */
static void test_prefill(void) {
    struct gba_audio_stats st;
    u32 i, k;
    unsigned int sent;

    printf("[6] the one-shot prefill gate\n");

    gba_audio_reset();
    cap_n = 0;

    /* Four blocks -> 1500 output frames, deliberately just BELOW the 1536
       threshold. Nothing may be submitted yet. */
    for (k = 0; k < 4u; k++) {
        for (i = 0; i < 512u; i++) put_in(i, 1000, -2000);
        gba_audio_resample(512);
    }
    sent = gba_audio_drain(GBA_AUDIO_MAX_GRAINS);

    gba_audio_stats(&st);

    if (st.fill >= GBA_AUDIO_TARGET_FILL) {
        /* The arithmetic behind this test drifted; say so rather than pass. */
        fail("test setup: fill must start below TARGET_FILL",
             (long long)st.fill, (long long)GBA_AUDIO_TARGET_FILL - 1);
    } else if (sent != 0u) {
        fail("grains submitted below the prefill threshold", (long long)sent, 0);
    } else if (st.grains != 0ull) {
        fail("grains reaching the backend below threshold",
             (long long)st.grains, 0);
    } else {
        printf("  ok   %u frames < %u threshold -> nothing submitted\n",
               st.fill, (unsigned)GBA_AUDIO_TARGET_FILL);
    }

    if (st.underruns != 0ull)
        fail("prefill wrongly counted as an underrun",
             (long long)st.underruns, 0);
    else
        ok("the deliberate wait is NOT counted as an underrun");

    /* Two more blocks -> 2250 frames, over the threshold. It must now submit. */
    for (k = 0; k < 2u; k++) {
        for (i = 0; i < 512u; i++) put_in(i, 1000, -2000);
        gba_audio_resample(512);
    }
    sent = gba_audio_drain(GBA_AUDIO_MAX_GRAINS);

    if (sent == 0u)
        fail("grains submitted once the threshold was crossed", 0, 1);
    else
        printf("  ok   threshold crossed -> %u grain(s) submitted\n", sent);

    /* AND IT MUST NEVER WAIT AGAIN. Drain the ring far below TARGET_FILL, then
       add just over one grain: a gate that re-armed would submit nothing here,
       which would be a rate correction rather than a start condition. */
    while (gba_audio_drain(GBA_AUDIO_MAX_GRAINS) > 0u) { }

    for (i = 0; i < 512u; i++) put_in(i, 1000, -2000);
    gba_audio_resample(512);      /* +375 frames */
    for (i = 0; i < 512u; i++) put_in(i, 1000, -2000);
    gba_audio_resample(512);      /* +375 -> 750ish, well under 1536 */

    {
        /* Sampled BEFORE the drain. Reading it afterwards would compare the
           threshold against a fill the drain had already reduced, which could
           let a genuinely-too-full setup slip through as though it had proved
           something. */
        unsigned int fill_before = gba_audio_fill();

        sent = gba_audio_drain(GBA_AUDIO_MAX_GRAINS);
        gba_audio_stats(&st);

        if (fill_before >= GBA_AUDIO_TARGET_FILL) {
            fail("test setup: fill must be below TARGET_FILL to prove no re-arm",
                 (long long)fill_before, (long long)GBA_AUDIO_TARGET_FILL - 1);
        } else if (sent == 0u) {
            fail("gate RE-ARMED below threshold (it must latch once, forever)",
                 0, 1);
        } else {
            printf("  ok   gate latched: %u frames < %u still submitted %u grain(s)\n",
                   fill_before, (unsigned)GBA_AUDIO_TARGET_FILL, sent);
        }
    }

    if (st.overruns) fail("overruns during the prefill test",
                          (long long)st.overruns, 0);
    else             ok("no overrun at any point in the prefill sequence");
}

int main(void) {
    printf("LUAport M10 -- 512:375 decimator equivalence harness\n");
    printf("source %u Hz, destination %u Hz, reduced ratio %u:%u\n\n",
           GBA_AUDIO_SRC_HZ, GBA_AUDIO_DST_HZ,
           GBA_AUDIO_DEC_IN, GBA_AUDIO_DEC_OUT);

    /* The ratio itself, restated as an arithmetic identity rather than trusted:
       65536 * 375 must equal 48000 * 512. */
    if ((u64)GBA_AUDIO_SRC_HZ * GBA_AUDIO_DEC_OUT !=
        (u64)GBA_AUDIO_DST_HZ * GBA_AUDIO_DEC_IN) {
        printf("FATAL: 65536:48000 does not reduce to 512:375\n");
        return 2;
    }
    printf("  ok   65536 * 375 == 48000 * 512 (the reduction is exact)\n\n");

    test_ratio_and_drift();
    printf("\n");
    test_vector();
    printf("\n");
    test_channel_order();
    printf("\n");
    test_conservation();
    printf("\n");
    test_silence();
    printf("\n");
    test_prefill();

    printf("\n");
    if (failures) {
        printf("RESULT: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("RESULT: all properties hold\n");
    return 0;
}
