#ifndef LUAPORT_AUDIO_H
#define LUAPORT_AUDIO_H

#include "core.h"

/* =============================================================================
 * LUAport M10 -- runtime/audio.h
 *
 * THE PS5 AUDIO OUTPUT LAYER: sceAudioOut bring-up, grain submission, teardown.
 * =============================================================================
 *
 * WHY THIS IS A SEPARATE TRANSLATION UNIT AND NOT PART OF runtime/platform.c
 * -------------------------------------------------------------------------
 * THIS IS THE SINGLE MOST IMPORTANT DESIGN DECISION IN M10, and it is a
 * VERIFICATION constraint, not a matter of taste.
 *
 * Makefile m7-verify, m8-verify and m9-verify each carve .rodata out of the
 * linked image and FAIL if the string `sceAudioOut` appears anywhere in it:
 *
 *     Makefile:3842-3848 (m9-verify)   "no sceGnm* and no sceAudioOut* name is
 *                                       present"
 *
 * Every LUAport image links runtime/platform.o IN ITS ENTIRETY -- the link line
 * names each object explicitly and NO target passes --gc-sections (Makefile
 * :3680-3683). So a `sceAudioOutOpen` string literal added to platform.c would
 * be carried into the M7, M8 AND M9 images even though none of those fixtures
 * ever calls an audio function, and ALL THREE FROZEN VERIFIERS WOULD FAIL.
 *
 * The fix is structural rather than a weakened gate: audio gets its own object,
 * runtime/audio.o, which appears ONLY in the M10 object list. M7/M8/M9 link
 * exactly the objects they linked before, their .rodata is unchanged, and their
 * sceAudioOut prohibition keeps its full force. M10 replaces that prohibition
 * with a STRICT FIVE-NAME ALLOWLIST -- the same M6 -> M7 pattern by which
 * sceVideoOut went from forbidden to allowlisted (Makefile:2634-2639).
 *
 * WHAT DOES *NOT* BELONG HERE
 *   No emulator concepts. No GBA. No sample-rate conversion, no ring buffer, no
 *   mixing. This file knows about exactly one thing: handing a finished
 *   SAMPLES_PER_BUF-frame grain of signed 16-bit stereo at SAMPLE_RATE to the
 *   console and taking the port away again cleanly. The GBA-facing half lives in
 *   adapters/gba/gba_audio.c and reaches hardware ONLY through this interface --
 *   m10-verify proves that gba_audio.o carries no sceAudioOut* relocation.
 *
 * =============================================================================
 * THE 0xFF CONTRACT -- THE SAME TRAP platform.h:141-157 DOCUMENTS FOR THE PAD
 * =============================================================================
 * sceAudioOutOpen takes 0xFF (SCE_USER_SERVICE_USER_ID_SYSTEM) as its user id.
 * It is NOT the real user id. Passing the real id to a MAIN port returns
 * 0x8026000A INVALID_PORT_TYPE -- a real, logged failure in the reference
 * implementation, recorded at LuaPSX/src/main.c:419-423 and :435-439.
 *
 * scePadGetHandle wants the OPPOSITE: the real id from
 * sceUserServiceGetInitialUser. The two calls look alike and want different
 * values.
 *
 * plat_audio_init() therefore takes NO user id parameter at all. There is
 * nothing for a caller to get wrong, because 0xFF is not a choice this layer
 * offers -- it is the only legal value and it is supplied internally.
 * ========================================================================= */

/* Return codes. The -40.. band is this layer's alone: platform.h owns
   -20..-24, -26, -28, -29 and -31, and apps/m0diag/main.c owns -30, so no code
   a caller can observe from the runtime layer is ambiguous. */
#define PLAT_AUD_OK           0
#define PLAT_AUD_EMODULE    -40   /* libSceAudioOut.sprx would not load        */
#define PLAT_AUD_ESYMBOL    -41   /* a required entry point did not resolve    */
#define PLAT_AUD_EOPEN      -42   /* sceAudioOutOpen returned a negative handle*/
#define PLAT_AUD_ENOTOPEN   -43   /* submit called before a successful init    */

/* Brings the audio port up.
 *
 * Must be called AFTER boot_data_region() and AFTER shim_init(), because the
 * resolved pointers and the handle live in .bss and the failure paths log
 * through printf()/klog().
 *
 *   G   the eboot's argument-shuffling gadget
 *   D   sceKernelDlsym, as handed in by the Lua loader
 *
 * Performs the proven LuaPSX bring-up ORDER, which is load-bearing:
 *   1. load libSceAudioOut.sprx
 *   2. resolve Init / Open / Output / Close
 *   3. CLOSE HANDLES 0..7 FIRST. The host title's PS2 emulator may still hold
 *      audio ports; LuaMD's open failed until the stale handles were released
 *      (LuaPSX/src/main.c:396-405). Closing a handle that was never open is
 *      harmless and its return value is deliberately ignored.
 *   4. sceAudioOutInit, treating 0x8026000E ALREADY_INIT as SUCCESS -- the host
 *      process got there first, which is the normal case.
 *   5. sceAudioOutOpen(0xFF, 0, 0, SAMPLES_PER_BUF, SAMPLE_RATE,
 *                      AUDIO_S16_STEREO)
 *
 * Returns PLAT_AUD_OK or one of the negative codes above. Every failure logs the
 * exact call, its raw return value and the decoded SDK error name, so a console
 * run never has to be repeated just to find out WHICH call failed. */
int plat_audio_init(void *G, void *D);

/* Submits exactly ONE grain: SAMPLES_PER_BUF frames of signed 16-bit stereo,
 * interleaved L,R -- that is SAMPLES_PER_BUF * 2 s16 values, and the caller must
 * supply exactly that many. A short buffer is a caller bug this layer cannot
 * detect; sceAudioOutOutput reads the full grain regardless.
 *
 * THIS CALL BLOCKS. sceAudioOutOutput does not return until the previously
 * queued buffer has been consumed by the audio hardware, which is precisely why
 * LuaPSX uses it as its frame pacer (LuaPSX/src/main.c:756-761). LUAport does
 * NOT: video/vsync remains the primary clock, so the caller is responsible for
 * bounding how many grains it submits per frame and for measuring the time this
 * call costs. See adapters/gba/gba_audio.h.
 *
 * Returns PLAT_AUD_OK, PLAT_AUD_ENOTOPEN when the port never opened, or the
 * raw negative return of sceAudioOutOutput. */
int plat_audio_submit(const s16 *grain);

/* The open port handle, or -1. Reported so "audio up" is a fact on the screen
   rather than an inference. */
s32 plat_audio_handle(void);

/* Total grains this layer has handed to sceAudioOutOutput, and the number of
   those calls that returned negative. Both are layer-owned so that a submission
   failure is visible even when the caller ignores the return value. */
u64 plat_audio_submitted(void);
u64 plat_audio_errors(void);

/* Decodes an sceAudioOut return code to its SDK name. Carried verbatim from
   LuaPSX/src/main.c:102-128 for the reason stated there: a bare 0x8026000A
   means nothing at 2am and every console run costs a game relaunch.

   NOTE FOR THE VERIFIER: none of the strings this returns contains the
   substring "sceAudioOut", so the M10 allowlist check sees exactly the five
   permitted names and nothing else. */
const char *plat_audio_err_name(s32 e);

/* Closes the port. Safe to call when init failed or was never called. */
void plat_audio_shutdown(void);

#endif
