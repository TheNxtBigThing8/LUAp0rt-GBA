#ifndef GBA_AUDIO_H
#define GBA_AUDIO_H

/* =============================================================================
 * LUAport M10 -- adapters/gba/gba_audio.h
 *
 * THE GBA -> PS5 AUDIO ADAPTER: 65536 Hz -> 48000 Hz, exact integer 512:375.
 * =============================================================================
 *
 * THIS HEADER DELIBERATELY INCLUDES NOTHING AND USES ONLY PLAIN C TYPES.
 * The rule is M5's, unchanged (adapters/gba/gba_exec.h:10-16): gpsp/common.h:100
 * typedefs u64 as `unsigned long long` while runtime/core.h:30 typedefs it as
 * `unsigned long`. They are DISTINCT TYPES, so a translation unit including both
 * fails on the duplicate typedef.
 *
 *   apps/m10gpsp/main.c includes runtime/core.h and NEVER a gpSP header.
 *   adapters/gba/gba_audio.c includes gpsp/common.h and NEVER runtime/core.h.
 *
 * This header is included by BOTH, so every type in it is a builtin
 * (`unsigned int`, `int`, `unsigned long long`) and never a typedef from either
 * side. That is why the counters below are spelled `unsigned long long` rather
 * than u64.
 *
 * =============================================================================
 * WHY M10 NEEDS NO CHANGE TO gpsp/ AT ALL
 * =============================================================================
 * gpsp/sound.c is ALREADY COMPILED AND ALREADY LINKED into M9 -- Makefile:3549
 * lists $(M9BUILD)/gpsp_sound.o in M9_GPSP_OBJS. render_gbc_sound() has been
 * running on hardware every frame since M5, mixing all six channels into
 * gpsp/sound.c:36's `static s32 sound_buffer[65536]`.
 *
 * WHAT WAS MISSING WAS A CONSUMER, NOT A PRODUCER. gpSP already publishes a
 * clean pull API:
 *
 *     gpsp/sound.h:154   u32 sound_read_samples(s16 *out, u32 frames);
 *     gpsp/sound.h:64    void sound_flush_ring(void);
 *
 * The ONLY caller of sound_read_samples() anywhere in the tree is
 * gpsp/libretro/libretro.c:437, and libretro is excluded from every LUAport link
 * (Makefile:3558). So M10 is a DRAIN, not a port. gpsp/ is untouched.
 *
 * A REAL CONSEQUENCE OF HAVING HAD NO CONSUMER, AND M10 HANDLES IT.
 * render_gbc_sound() accumulates with `+=` (gpsp/sound.c:334,338) and ONLY
 * sound_read_samples() zeroes a slot after reading it (gpsp/sound.c:852). With
 * no consumer, M9's runs re-accumulated over a wrapping 65536-entry ring. The
 * result is BOUNDED and did not invalidate M9 -- worst case |sum| per slot is
 * 98304 per pass against an s32 -- but stale values are resident at M10 entry.
 * gba_audio_reset() therefore calls sound_flush_ring() before the measured run.
 * That is a gpSP function CALL, not a gpSP EDIT.
 *
 * =============================================================================
 * THE TWO RATES, PROVEN FROM CONSTANTS RATHER THAN ASSUMED
 * =============================================================================
 * SOURCE 65536 Hz, and it is IMMUTABLE in this build:
 *   gpsp/sound.c:26-27   sound_frequency = GBA_SOUND_FREQUENCY, freq_bits = 16
 *   gpsp/sound.h:66      #define GBA_SOUND_FREQUENCY (64 * 1024)   == 65536
 *   gpsp/sound.c:603-604 init_sound() hardcodes gbc_sound_tick_step = 256,
 *                        which is only valid for 2^16.
 *   The only code that ever changes the rate is gpsp_apply_sound_rate() in
 *   libretro.c:66-87 -- NOT LINKED. So 65536 is a constant here, not a default.
 *
 * DESTINATION 48000 Hz, and it is the ONLY legal choice:
 *   runtime/core.h SAMPLE_RATE 48000. sceAudioOutOpen returns 0x80260008
 *   INVALID_SAMPLE_FREQ for a MAIN port at any other rate.
 *
 * SAMPLES PER GBA FRAME -- exact, and it is NOT a constant:
 *   GBC_BASE_RATE_INT = 16 * 1024 * 1024 = 16777216 cycles/s (gpsp/sound.h:76)
 *   cycles per frame  = 308 * 4 * 228    = 280896
 *   frames of audio   = 65536 * 280896 / 16777216 = 280896 / 256 = 1097.25
 *
 * gpsp/sound.c:441-448 carries the fraction in gbc_sound_partial_ticks, so a
 * frame yields 1097 OR 1098. sound_read_samples() additionally holds back the
 * last 512 SAMPLES = 256 FRAMES (gpsp/sound.c:838-840) and rounds down to an
 * even sample count. NOTHING HERE MAY ASSUME A FIXED COUNT PER FRAME, and
 * nothing does: every entry point below takes or returns an actual count.
 *
 * =============================================================================
 * THE RATIO IS EXACT. THIS IS THE WHOLE REASON THE CONVERTER CANNOT DRIFT.
 * =============================================================================
 *     65536 = 2^16                 48000 = 2^7 * 375
 *     gcd(65536, 48000) = 128
 *     65536 : 48000  ==  512 : 375     EXACTLY
 *
 * 512 input frames produce EXACTLY 375 output frames, forever, with an integer
 * accumulator that returns to its starting value every 512 inputs. There is no
 * irrational ratio, no floating point and no accumulated phase error ANYWHERE in
 * the converter.
 *
 * THIS IS DOWNSAMPLING, WHICH FLIPS THE LuaPSX PRECEDENT.
 * LuaPSX/src/psxaudio.c:15-22 explains that the PSX SPU runs SLOWER than the
 * output (44100 -> 48000), so it must INTERPOLATE, and that LuaMD's chips run
 * FASTER, so LuaMD uses BOX-FILTER DECIMATION. The GBA at 65536 Hz is in
 * LuaMD's case, not the PSX's: averaging the input frames that map to one output
 * frame both converts the rate and anti-aliases in the correct direction.
 * Copying psxaudio.c's linear interpolator here would be the wrong algorithm.
 *
 * =============================================================================
 * THE CONVERTER -- BRESENHAM BOX DECIMATOR, 6 INTEGER OPS PER INPUT FRAME
 * =============================================================================
 *     acc += 375;                          per input frame
 *     sum_l += l;  sum_r += r;  n++;
 *     if (acc >= 512) { acc -= 512; emit(n == 2 ? sum >> 1 : sum);
 *                       sum_l = sum_r = 0; n = 0; }
 *
 * WHY n IS ONLY EVER 1 OR 2, WHICH IS WHAT TURNS THE DIVIDE INTO A SHIFT:
 *   acc is always in [0, 512). Adding 375 gives at most 886; if that is >= 512
 *   one subtraction returns it to [0, 375). So after an emission acc < 375, and
 *   TWO further additions reach at least 750 > 512 -- an emission is therefore
 *   forced by the second input at the latest. n in {1, 2}, proven, so the mean
 *   is `sum >> 1` or `sum` and there is no division.
 *
 * WHY IT CANNOT DRIFT:
 *   over 512 inputs acc gains 512*375 = 192000 and loses 375*512 = 192000, so it
 *   returns EXACTLY to its entry value and the pattern is periodic with period
 *   512. tools/gba_audio_equiv.c proves both properties over millions of frames.
 *
 * COST: ~6 integer ops * ~1097 input frames = ~6.6k ops per video frame, against
 * a 16743 us budget. STATE: 16 bytes. No libm, no float, no division.
 *
 * =============================================================================
 * A DRIFT THAT IS *NOT* THE CONVERTER'S, AND IS PREDICTED RATHER THAN DISCOVERED
 * =============================================================================
 * The converter is exact, but the SYSTEM still has a rate mismatch, because
 * emulation is paced by the HOST DISPLAY (59.94 Hz) while a GBA frame is
 * 59.7275 Hz of audio:
 *
 *     production  = 803.65 output frames/frame * 59.94 = 48170.8 frames/s
 *     consumption =                                      48000.0 frames/s
 *     surplus     =                                       +170.8 frames/s
 *                                                        (+0.356 %)
 *
 * The ring therefore GROWS about 171 frames per second. From the 1536-frame
 * target fill, an 8192-frame ring saturates in (8192-1536)/170.8 ~= 39 SECONDS.
 * That lands inside the planned M10D 30-60 s run, which is exactly why M10C is
 * ten seconds and MEASURES THE SLOPE instead of assuming it.
 *
 * M10C RUNS THE FIXED 512:375 CONVERTER WITH NO SERVO. That is a deliberate
 * instruction: the prediction above is a HYPOTHESIS, and M10C is the experiment
 * that tests it. No correction is implemented on the strength of a prediction.
 * If and only if M10C's measured slope confirms it, M10D may enable a bounded
 * integer servo (drop or repeat ONE output frame when the fill leaves a dead
 * band), which at 0.356 % is ~3 corrections/second and inaudible.
 * ========================================================================= */

/* ---- proven rate constants ------------------------------------------------
   Restated here so the fixture can print them and so tools/gba_audio_equiv.c
   can assert against the same numbers the production code uses. */
#define GBA_AUDIO_SRC_HZ        65536u   /* gpsp/sound.h:66, and immutable    */
#define GBA_AUDIO_DST_HZ        48000u   /* runtime/core.h SAMPLE_RATE        */

/* The reduced ratio. 65536:48000 / 128 == 512:375. */
#define GBA_AUDIO_DEC_IN          512u
#define GBA_AUDIO_DEC_OUT         375u

/* ---- buffer geometry ------------------------------------------------------
   ALL STATIC. Nothing on this path allocates, at any time, by design: M9's
   arena tripwire (apps/m9gpsp/main.c:1704-1727) terminates a run on ONE byte of
   steady-state growth, and M10 keeps that gate.

     ring   8192 frames stereo s16 = 32768 bytes  (~170 ms of slack at 48 kHz)
     pull   2048 frames stereo s16 =  8192 bytes  (>= 1098 + margin)
     grain   256 frames stereo s16 =   1024 bytes (the sceAudioOut quantum)   */
#define GBA_AUDIO_RING_FRAMES    8192u
#define GBA_AUDIO_PULL_FRAMES    2048u
#define GBA_AUDIO_GRAIN_FRAMES    256u   /* MUST equal SAMPLES_PER_BUF        */

/* THE PREFILL TARGET, AND IT IS ENFORCED RATHER THAN ADVISORY.
   1536 frames == 32 ms == exactly 6 grains. gba_audio_drain() submits NOTHING
   until the ring has reached this level ONCE; after that it submits whole
   grains normally and never waits again.

   A grain handed over on a nearly empty ring leaves nothing behind it, so the
   next scheduling hiccup starves the port. Building the cushion once costs
   about two video frames of latency and buys six grains of slack for the whole
   run. The floor under this is gpSP's own 256-frame readout holdback (3.9 ms).

   IT IS A START CONDITION, NOT A SERVO: it fires once, latches, and never acts
   again. The 512:375 converter runs FIXED with no rate correction anywhere,
   which is precisely what M10C measures. */
#define GBA_AUDIO_TARGET_FILL    1536u

/* Grains submitted per video frame, hard cap. Steady-state need is
   803.65/256 = 3.14, so 4 absorbs jitter while bounding the worst case a
   BLOCKING sceAudioOutOutput can cost a single frame. */
#define GBA_AUDIO_MAX_GRAINS        4u

/* Channel selectors for the M10B synthetic tone. */
#define GBA_AUDIO_TONE_LEFT         1u
#define GBA_AUDIO_TONE_RIGHT        2u
#define GBA_AUDIO_TONE_BOTH         3u

/* ---- instrumentation ------------------------------------------------------
   Plain builtin types only -- see the type-boundary note at the top. */
struct gba_audio_stats {
    unsigned long long produced;      /* output frames written into the ring  */
    unsigned long long consumed;      /* output frames handed to the backend  */
    unsigned long long pulled;        /* input frames read from gpSP          */
    unsigned long long grains;        /* whole grains submitted               */
    unsigned long long underruns;     /* frames where a grain was wanted and
                                         the ring could not supply one        */
    unsigned long long overruns;      /* output frames DROPPED, ring full     */
    unsigned long long nonzero;       /* non-zero samples seen entering ring  */
    unsigned int       peak;          /* loudest |sample| entering the ring   */
    unsigned int       fill;          /* current ring occupancy, frames       */
    unsigned int       fill_min;      /* min observed after first submission  */
    unsigned int       fill_max;      /* max observed                         */
    unsigned int       acc;           /* live resampler accumulator, < 512    */
};

/* Clears the ring, the resampler and every counter, AND calls gpSP's
 * sound_flush_ring() so the measured run does not begin on samples that
 * accumulated while no consumer existed. Call once, immediately before the
 * measured loop. */
void gba_audio_reset(void);

/* SPAN F1. Reads whatever gpSP currently has available into this adapter's
 * private pull buffer, capped at GBA_AUDIO_PULL_FRAMES. Returns the number of
 * INPUT frames actually read, which is 0 when gpSP is holding everything back.
 *
 * THIS IS THE ONLY FUNCTION IN LUAport THAT CALLS sound_read_samples(), and
 * m10-verify asserts both halves of that: gba_audio.o MUST reference it and
 * m10main.o MUST NOT. */
unsigned int gba_audio_pull(void);

/* SPAN F1, M10B only. Synthesises `in_frames` of a deterministic 512 Hz square
 * wave at the SOURCE rate into the same private pull buffer, so the tone travels
 * the identical resampler -> ring -> backend path the emulator's audio does.
 * chan_mask is GBA_AUDIO_TONE_LEFT / _RIGHT / _BOTH. Returns in_frames, clamped
 * to GBA_AUDIO_PULL_FRAMES. Touches no gpSP state and needs no ROM. */
unsigned int gba_audio_tone(unsigned int in_frames, unsigned int chan_mask);

/* SPAN F2. Runs `in_frames` from the pull buffer through the 512:375 decimator
 * and into the ring. `in_frames` must be the value the matching pull/tone call
 * returned. */
void gba_audio_resample(unsigned int in_frames);

/* SPAN F3. Submits at most `max_grains` WHOLE grains to the PS5 backend, and
 * never a partial one. Returns the number submitted.
 *
 * A frame that cannot fill a grain leaves the remainder in the ring and counts
 * ONE underrun -- the leftover is never discarded and never padded with
 * silence, which is what makes the residue self-correcting rather than
 * cumulative (the same property LuaPSX/src/psxaudio.c:159-165 relies on). */
unsigned int gba_audio_drain(unsigned int max_grains);

/* Current ring occupancy in output frames. Cheap; safe to call per frame. */
unsigned int gba_audio_fill(void);

/* Copies the counters out. `out` must be non-NULL. */
void gba_audio_stats(struct gba_audio_stats *out);

/* 1 when the ring has never received a non-zero sample -- the explicit SILENT
 * answer M10C is required to report rather than leave to inference. */
int gba_audio_is_silent(void);

#endif
