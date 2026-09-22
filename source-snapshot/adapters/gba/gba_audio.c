/* LUAport M10 -- adapters/gba/gba_audio.c
 *
 * =============================================================================
 * THE GBA -> PS5 AUDIO BRIDGE. THE ONLY CALLER OF gpSP's sound_read_samples().
 * =============================================================================
 *
 * The full derivation of the rates, the ratio, the converter and the predicted
 * system drift lives in gba_audio.h and is not repeated here. This file is the
 * implementation of what that header specifies.
 *
 * THE TYPE BOUNDARY, HANDLED THE WAY M5 HANDLED IT.
 * This translation unit includes gpsp/common.h and therefore CANNOT include
 * runtime/core.h or runtime/audio.h: gpsp/common.h:100 typedefs u64 as
 * `unsigned long long` and runtime/core.h:30 typedefs it as `unsigned long`,
 * which are distinct types and collide.
 *
 * So plat_audio_submit() is HAND-DECLARED below rather than included. That is
 * safe here and would not be safe for an arbitrary function: the declaration
 * mentions only `const short *` and `int`, and BOTH headers agree on those two
 * builtins exactly (gpsp/common.h:97 `signed short int s16`, runtime/core.h:36
 * `short s16`). u64 -- the one type the two sides disagree about -- does not
 * appear in the signature at all. adapters/gba/gba_exec.c:70-72 establishes the
 * same hand-extern precedent.
 *
 * WHAT THIS FILE MUST NEVER CONTAIN, AND m10-verify PROVES ALL FOUR:
 *   - no sceAudioOut* name. Hardware is reached ONLY through plat_audio_submit.
 *   - no malloc/free. Every buffer below is static.
 *   - no floating point and no libm.
 *   - no PS5 concept of any kind beyond that single call.
 */

#include "common.h"        /* gpSP: pulls sound.h -> sound_read_samples,
                              sound_flush_ring (gpsp/common.h:193) */
#include "gba_audio.h"

/* THE ONE CALL OUT TO THE PLATFORM. See the type-boundary note above. Returns 0
   on success, negative otherwise; this adapter treats the value as a flag and
   never interprets the code, which is runtime/audio.c's job. */
extern int plat_audio_submit(const short *grain);

/* ============================ STATIC STORAGE =============================
 *
 * TOTAL: 32768 + 8192 + 1024 = 41,984 bytes of .bss, plus ~80 bytes of state.
 * NOTHING HERE IS ALLOCATED AT RUNTIME. M9's arena tripwire terminates a run on
 * one byte of steady-state growth and M10 keeps that gate, so a malloc on this
 * path would end the run rather than be absorbed. */

/* The output ring, at 48000 Hz. Power-of-two length so the wrap is a mask. */
static s16 ring[GBA_AUDIO_RING_FRAMES * 2];
static u32 ring_head;      /* free-running; fill == head - tail             */
static u32 ring_tail;      /* free-running; unsigned wrap is intentional    */

/* The input staging buffer, at 65536 Hz. Filled by gba_audio_pull() from gpSP
   or by gba_audio_tone() synthetically, then consumed by gba_audio_resample().
   Sized well above the 1098-frame worst case a single GBA frame can yield. */
static s16 pull_buf[GBA_AUDIO_PULL_FRAMES * 2];

/* The submission staging grain. Static rather than a local so that a 1 KB
   buffer never lands on the stack of the measured loop. */
static s16 grain_buf[GBA_AUDIO_GRAIN_FRAMES * 2];

/* The 512:375 Bresenham decimator. 16 bytes of state, exactly as specified. */
static struct {
    u32 acc;               /* phase accumulator, invariant: acc < 512       */
    s32 sum_l;             /* box-filter accumulator, left                  */
    s32 sum_r;             /* box-filter accumulator, right                 */
    u32 n;                 /* inputs in the current group, provably 1 or 2  */
} R;

static struct gba_audio_stats S;

/* M10B's tone phase, kept across calls so the square wave is continuous
   between video frames rather than restarting (which would click). */
static u32 tone_phase;

/* Latched once the first grain has been submitted, so fill_min is not dragged
   to zero by the prefill period before audio has actually started. */
static u32 started;

/* Latched once the ring has ONCE reached GBA_AUDIO_TARGET_FILL. Until then
   gba_audio_drain() submits nothing, deliberately.

   WHY THE CUSHION IS BUILT BEFORE THE FIRST GRAIN GOES OUT, rather than
   submitting as soon as 256 frames exist: a grain handed over on a nearly empty
   ring leaves NOTHING behind it, so the very next scheduling hiccup starves the
   port. Waiting for 1536 frames (32 ms) once, at the start, buys back six
   grains of slack for the whole run at a cost of about two video frames of
   latency.

   THIS IS NOT A SERVO AND IS NOT A CORRECTION. It is a one-shot start
   condition: it fires once, latches, and never acts again. The 512:375
   converter still runs FIXED with no rate adjustment of any kind, which is what
   M10C is there to measure. */
static u32 prefilled;

/* THE RING LENGTH MUST BE A POWER OF TWO -- the wrap below is a mask, not a
   modulo. Checked here rather than assumed. */
#define RING_MASK  (GBA_AUDIO_RING_FRAMES - 1u)

/* ============================== THE RING ================================= */

static u32 ring_fill(void) {
    return (u32)(ring_head - ring_tail);
}

/* One finished OUTPUT frame into the ring.
 *
 * NO CLAMPING IS PERFORMED, AND THAT IS DELIBERATE AND PROVABLE.
 * gpSP has already saturated every sample to [-32768, 32767] before handing it
 * over (gpsp/sound.c:854-857). This decimator either passes such a value
 * through unchanged (n == 1) or averages two of them (n == 2), and the mean of
 * two values in that interval is itself in that interval. So `l` and `r` are
 * always representable as s16 and a second clamp here could only ever be a
 * no-op that costs two branches per output frame. Clamping twice is also how a
 * saturation bug hides. */
static void ring_push(s32 l, s32 r) {
    u32 i;
    s32 al, ar;

    if (ring_fill() >= GBA_AUDIO_RING_FRAMES) {
        /* OVERFLOW POLICY: drop the NEWEST frame and count it. Dropping the
           oldest would mean advancing the tail past samples already committed
           to a grain boundary, which desynchronises L/R if it ever lands on an
           odd frame. Dropping the newest cannot. */
        S.overruns++;
        return;
    }

    i = (ring_head & RING_MASK) * 2u;
    ring[i + 0] = (s16)l;          /* EVEN == LEFT  -- gpsp/sound.c:338 */
    ring[i + 1] = (s16)r;          /* ODD  == RIGHT -- gpsp/sound.c:334 */
    ring_head++;
    S.produced++;

    /* LEVEL IS MEASURED SEPARATELY FROM RATE. A converter that faithfully
       converts silence passes every ratio check there is, so M10C reports peak
       amplitude and a non-zero count and is required to say SILENT explicitly
       when they are zero. */
    al = (l < 0) ? -l : l;
    ar = (r < 0) ? -r : r;
    if ((u32)al > S.peak) S.peak = (u32)al;
    if ((u32)ar > S.peak) S.peak = (u32)ar;
    if (l) S.nonzero++;
    if (r) S.nonzero++;
}

/* ============================ THE CONVERTER ==============================
 *
 * 512 input frames in, EXACTLY 375 output frames out, forever.
 *
 *   acc += 375 per input; emit and subtract 512 on carry. Over 512 inputs acc
 *   gains 512*375 = 192000 and loses 375*512 = 192000, returning to its entry
 *   value: the pattern is periodic with period 512 and the converter's own
 *   drift is EXACTLY ZERO, not merely small.
 *
 *   acc invariant: acc < 512 always. Adding 375 gives at most 886; one
 *   subtraction returns it below 375. Hence at most two inputs per output, so
 *   R.n is provably 1 or 2 and the mean is a shift, never a division.
 *
 * ON `>> 1` FOR A SIGNED SUM: GCC and Clang both define signed right shift as
 * an ARITHMETIC shift, and they are the only compilers this tree is built with
 * ($(CC)/$(CXX) in the Makefile). The shift rounds toward negative infinity
 * where a `/ 2` would round toward zero; either is correct for a box filter,
 * and the shift is chosen so the operation is a single instruction and so the
 * offline harness has one exact expected answer to check against. */
void gba_audio_resample(unsigned int in_frames) {
    u32 i;
    u32 n = (u32)in_frames;

    if (n > GBA_AUDIO_PULL_FRAMES) n = GBA_AUDIO_PULL_FRAMES;

    for (i = 0; i < n; i++) {
        R.sum_l += (s32)pull_buf[i * 2 + 0];
        R.sum_r += (s32)pull_buf[i * 2 + 1];
        R.n++;

        R.acc += GBA_AUDIO_DEC_OUT;
        if (R.acc >= GBA_AUDIO_DEC_IN) {
            R.acc -= GBA_AUDIO_DEC_IN;
            if (R.n == 2u)
                ring_push(R.sum_l >> 1, R.sum_r >> 1);
            else
                ring_push(R.sum_l, R.sum_r);
            R.sum_l = 0;
            R.sum_r = 0;
            R.n     = 0;
        }
    }
    S.acc = R.acc;
}

/* ============================== THE SOURCE =============================== */

/* SPAN F1. THE ONLY sound_read_samples() CALL SITE IN LUAport.
 *
 * gpSP decides how much is available: it holds back the last 512 samples as
 * "in use" and rounds down to an even sample count (gpsp/sound.c:837-845), so
 * a return of 0 is NORMAL on an early frame and is not an error. */
unsigned int gba_audio_pull(void) {
    u32 got = sound_read_samples(pull_buf, GBA_AUDIO_PULL_FRAMES);
    S.pulled += got;
    return (unsigned int)got;
}

/* SPAN F1, M10B. A deterministic 512 Hz square wave at the 65536 Hz SOURCE
 * rate -- 65536 / 128 = 512 exactly, so the period is a whole 128 frames and
 * the tone needs no phase fraction.
 *
 * It is written into the SAME pull buffer the emulator's samples land in, so it
 * travels the identical decimator -> ring -> grain -> sceAudioOut path. That is
 * the point of M10B: it validates channel order, signedness, width and the
 * backend WITHOUT depending on a ROM making a sound. */
unsigned int gba_audio_tone(unsigned int in_frames, unsigned int chan_mask) {
    u32 i;
    u32 n = (u32)in_frames;

    if (n > GBA_AUDIO_PULL_FRAMES) n = GBA_AUDIO_PULL_FRAMES;

    for (i = 0; i < n; i++) {
        /* Half the 128-frame period high, half low. Amplitude 8000 of 32767:
           unmistakably audible, comfortably short of clipping, and small enough
           that a doubled-amplitude bug would still not distort. */
        s16 v = (tone_phase & 64u) ? (s16)-8000 : (s16)8000;
        tone_phase = (tone_phase + 1u) & 127u;

        pull_buf[i * 2 + 0] = (chan_mask & GBA_AUDIO_TONE_LEFT)  ? v : (s16)0;
        pull_buf[i * 2 + 1] = (chan_mask & GBA_AUDIO_TONE_RIGHT) ? v : (s16)0;
    }
    return (unsigned int)n;
}

/* ============================== THE SINK ================================= */

/* SPAN F3. WHOLE GRAINS ONLY, AND AT MOST max_grains OF THEM.
 *
 * Steady-state need is 803.65 / 256 = 3.14 grains per video frame, so stopping
 * with fewer than 256 frames left is the NORMAL case and is not an underrun.
 * An underrun is counted only when the ring could not supply even ONE grain,
 * which is the condition that actually starves the port.
 *
 * plat_audio_submit() BLOCKS. The cap is what stops a blocking backend from
 * turning into the frame pacer -- video/vsync stays the clock, and the caller
 * times this span separately so a blocking regression appears as a number. */
unsigned int gba_audio_drain(unsigned int max_grains) {
    unsigned int sent = 0;
    u32 i;

    /* ---- THE ONE-SHOT PREFILL GATE ----------------------------------------
       Returning 0 here is NOT an underrun and must not be counted as one: the
       ring is being filled ON PURPOSE and the port has not been asked for
       anything yet. Counting these frames would report tens of false underruns
       at the head of every submitting run and make the real ones unfindable. */
    if (!prefilled) {
        if (ring_fill() < GBA_AUDIO_TARGET_FILL) {
            S.fill = ring_fill();
            if (S.fill > S.fill_max) S.fill_max = S.fill;
            return 0;
        }
        prefilled = 1u;
    }

    while (sent < max_grains) {
        if (ring_fill() < GBA_AUDIO_GRAIN_FRAMES)
            break;

        for (i = 0; i < GBA_AUDIO_GRAIN_FRAMES; i++) {
            u32 s = ((ring_tail + i) & RING_MASK) * 2u;
            grain_buf[i * 2 + 0] = ring[s + 0];
            grain_buf[i * 2 + 1] = ring[s + 1];
        }
        ring_tail += GBA_AUDIO_GRAIN_FRAMES;
        S.consumed += GBA_AUDIO_GRAIN_FRAMES;

        /* The frames leave the ring whether or not the backend accepts them --
           re-submitting a rejected grain would only deepen a backlog. Successes
           are counted here; failures are counted inside runtime/audio.c. */
        if (plat_audio_submit(grain_buf) == 0) {
            S.grains++;
            started = 1u;
        }
        sent++;
    }

    if (sent == 0u)
        S.underruns++;

    /* ONE fill sample per drain call, i.e. once per video frame. */
    S.fill = ring_fill();
    if (S.fill > S.fill_max) S.fill_max = S.fill;
    if (started && S.fill < S.fill_min) S.fill_min = S.fill;

    return sent;
}

/* ============================ INTROSPECTION ============================== */

unsigned int gba_audio_fill(void) {
    return (unsigned int)ring_fill();
}

void gba_audio_stats(struct gba_audio_stats *out) {
    if (!out) return;
    S.fill = ring_fill();
    S.acc  = R.acc;
    *out   = S;

    /* fill_min is seeded to ~0 so the first sample always wins. Report it as 0
       when no sample was ever taken, rather than handing the caller 4294967295
       and letting it print that as a measurement. Same convention as
       apps/m9gpsp/main.c:568's m9_span_min(). */
    if (out->fill_min == 0xFFFFFFFFu)
        out->fill_min = 0;
}

int gba_audio_is_silent(void) {
    return (S.nonzero == 0) ? 1 : 0;
}

/* ================================ RESET ================================== */

/* Clears everything this adapter owns AND drops whatever accumulated inside
 * gpSP while no consumer existed.
 *
 * THE sound_flush_ring() CALL IS THE POINT. render_gbc_sound() accumulates with
 * `+=` and only sound_read_samples() zeroes a slot, so with no consumer -- the
 * situation in every milestone up to M9 -- gpSP's 65536-entry mix bus has been
 * re-accumulating over itself. gpsp/sound.c:873-879 is gpSP's own supported way
 * to discard that and realign both direct-sound producers with the consumer.
 * Calling it is not a modification of gpsp/. */
void gba_audio_reset(void) {
    u32 i;

    for (i = 0; i < GBA_AUDIO_RING_FRAMES * 2u; i++) ring[i] = 0;
    for (i = 0; i < GBA_AUDIO_PULL_FRAMES * 2u; i++) pull_buf[i] = 0;
    for (i = 0; i < GBA_AUDIO_GRAIN_FRAMES * 2u; i++) grain_buf[i] = 0;

    ring_head = 0;
    ring_tail = 0;

    R.acc   = 0;
    R.sum_l = 0;
    R.sum_r = 0;
    R.n     = 0;

    tone_phase = 0;
    started    = 0;
    prefilled  = 0;

    S.produced  = 0;
    S.consumed  = 0;
    S.pulled    = 0;
    S.grains    = 0;
    S.underruns = 0;
    S.overruns  = 0;
    S.nonzero   = 0;
    S.peak      = 0;
    S.fill      = 0;
    S.fill_min  = 0xFFFFFFFFu;   /* so the first sample always wins */
    S.fill_max  = 0;
    S.acc       = 0;

    sound_flush_ring();
}
