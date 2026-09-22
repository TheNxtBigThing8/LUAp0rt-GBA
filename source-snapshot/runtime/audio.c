/* LUAport M10 -- runtime/audio.c
 *
 * =============================================================================
 * THE PS5 AUDIO OUTPUT LAYER. THE ONLY FILE IN THE TREE THAT NAMES sceAudioOut.
 * =============================================================================
 *
 * Adapted from LuaPSX/src/main.c:396-440 (bring-up), :756-761 (submission
 * placement) and :102-128 (the error decoder). Those lines are proven on retail
 * hardware; the ORDER of the bring-up is load-bearing and is reproduced here
 * rather than rationalised.
 *
 * WHY THIS IS A SEPARATE OBJECT: see the long note in runtime/audio.h. In one
 * line -- m7-verify, m8-verify and m9-verify FAIL if the string `sceAudioOut`
 * appears in their image, every image links platform.o whole with no
 * --gc-sections, so audio cannot live in platform.c without breaking three
 * frozen verifiers.
 *
 * THIS FILE CONTAINS NO EMULATOR CONCEPT. No GBA, no sample rate conversion, no
 * ring buffer, no mixing, no timing model. Rate conversion and buffering are the
 * GBA adapter's job (adapters/gba/gba_audio.c) precisely so that this layer
 * stays reusable by a future core, exactly as platform.c was reusable across
 * LuaGB -> LuaMD -> LuaPSX -> LUAport.
 *
 * runtime/platform.c IS NOT MODIFIED BY M10.
 */

#include "core.h"
#include "shim.h"
#include "audio.h"

/* Resolves to runtime/libc/stdio.h -- LUAport's freestanding stand-in, NOT the
   system libc -- through the -Iruntime/libc already carried by $(M10_INCLUDES)
   (Makefile:4716). printf is declared there at line 55, outside the file's
   #ifndef NULL block, so it is visible unconditionally. This is the same one
   line, in the same position, that runtime/platform.c:4 and runtime/shim.c:5
   already use; no target in this tree links a system libc. */
#include <stdio.h>

/* Resolved entry points and the open handle. All in .bss, which is why
   plat_audio_init() must run after boot_data_region(). Same shape as
   platform.c's V and T structures. */
static struct {
    void *gadget;
    void *aud_open;
    void *aud_out;
    void *aud_close;
    void *aud_init;
    s32   handle;
    u64   submitted;
    u64   errors;
} A;

/* sceAudioOut return codes, from the SDK reference. Carried verbatim from
   LuaPSX/src/main.c:105-128.

   NOTE THE ABSENCE OF THE SUBSTRING "sceAudioOut" IN EVERY RETURN VALUE. That
   is deliberate and it is checked: m10-verify allows exactly five sceAudioOut*
   names in .rodata and fails on a sixth, so an error string that happened to
   spell one would break the build. */
const char *plat_audio_err_name(s32 e) {
    switch ((u32)e) {
        case 0x80260001u: return "NOT_OPENED";
        case 0x80260002u: return "BUSY";
        case 0x80260003u: return "INVALID_PORT";
        case 0x80260004u: return "INVALID_POINTER";
        case 0x80260005u: return "PORT_FULL";
        case 0x80260006u: return "INVALID_SIZE (len must be 256/512/768/1024)";
        case 0x80260007u: return "INVALID_FORMAT";
        case 0x80260008u: return "INVALID_SAMPLE_FREQ (only 48000 is legal)";
        case 0x80260009u: return "INVALID_VOLUME";
        case 0x8026000au: return "INVALID_PORT_TYPE (userId/type combination)";
        case 0x8026000cu: return "INVALID_CONF_TYPE";
        case 0x8026000du: return "OUT_OF_MEMORY";
        case 0x8026000eu: return "ALREADY_INIT";
        case 0x8026000fu: return "NOT_INIT";
        case 0x80260010u: return "MEMORY";
        case 0x80260011u: return "SYSTEM_RESOURCE";
        case 0x80260012u: return "TRANS_EVENT";
        case 0x80260013u: return "INVALID_FLAG";
        case 0x80260014u: return "INVALID_MIXLEVEL";
        case 0x80260015u: return "INVALID_ARG";
        default:          return "unknown";
    }
}

int plat_audio_init(void *G, void *D) {
    int h;

    A.gadget    = G;
    A.handle    = -1;
    A.submitted = 0;
    A.errors    = 0;

    klog("AUDIO: resolving symbols\n");

    void *load_mod = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLoadStartModule");
    if (!load_mod) {
        klog("AUDIO: FAIL sceKernelLoadStartModule unresolved\n");
        return PLAT_AUD_ESYMBOL;
    }

    s32 aud_mod = (s32)NC(G, load_mod, (u64)"libSceAudioOut.sprx", 0, 0, 0, 0, 0);
    printf("AUDIO: libSceAudioOut.sprx -> handle 0x%08x\n", (unsigned)aud_mod);
    if (aud_mod < 0) {
        printf("AUDIO: FAIL module load -> 0x%08x\n", (unsigned)aud_mod);
        return PLAT_AUD_EMODULE;
    }

    A.aud_open  = SYM(G, D, aud_mod, "sceAudioOutOpen");
    A.aud_out   = SYM(G, D, aud_mod, "sceAudioOutOutput");
    A.aud_close = SYM(G, D, aud_mod, "sceAudioOutClose");
    A.aud_init  = SYM(G, D, aud_mod, "sceAudioOutInit");

    if (!A.aud_open || !A.aud_out || !A.aud_close) {
        printf("AUDIO: FAIL symbols open=%p out=%p close=%p init=%p\n",
               A.aud_open, A.aud_out, A.aud_close, A.aud_init);
        return PLAT_AUD_ESYMBOL;
    }

    /* STALE HANDLES FIRST. The host title's PS2 emulator may still own audio
       ports. LuaMD's open failed until these were released, and the reference
       carries the same loop (LuaPSX/src/main.c:404-405). Closing a handle that
       was never open is harmless, so the return value is ignored ON PURPOSE. */
    for (h = 0; h < 8; h++)
        NC(G, A.aud_close, (u64)h, 0, 0, 0, 0, 0);
    klog("AUDIO: stale handles 0..7 released\n");

    /* ALREADY_INIT IS SUCCESS. The library is usually already initialised in
       this process because the host game got there first; treating 0x8026000E
       as an error would fail every normal run. */
    if (A.aud_init) {
        s32 ir = (s32)NC(G, A.aud_init, 0, 0, 0, 0, 0, 0);
        if (ir != 0 && (u32)ir != 0x8026000eu)
            printf("AUDIO: sceAudioOutInit -> 0x%08x %s\n",
                   (unsigned)ir, plat_audio_err_name(ir));
        else
            printf("AUDIO: sceAudioOutInit -> 0x%08x %s\n",
                   (unsigned)ir, (ir == 0) ? "OK" : "ALREADY_INIT (fine)");
    }

    /* 0xFF, NOT a user id. See the contract note in audio.h. A MAIN port
       REQUIRES SCE_USER_SERVICE_USER_ID_SYSTEM; a real user id returns
       0x8026000A INVALID_PORT_TYPE and is the logged reference bug. */
    A.handle = (s32)NC(G, A.aud_open, 0xFF, 0, 0,
                       SAMPLES_PER_BUF, SAMPLE_RATE, AUDIO_S16_STEREO);

    if (A.handle < 0) {
        printf("AUDIO: FAIL open -> 0x%08x %s\n",
               (unsigned)A.handle, plat_audio_err_name(A.handle));
        klog("AUDIO: a MAIN port requires user id 0xFF. If this returned "
             "INVALID_PORT_TYPE the id is wrong, not the format\n");
        return PLAT_AUD_EOPEN;
    }

    printf("AUDIO: up (handle %d, %d Hz, grain %d frames, S16 stereo)\n",
           (int)A.handle, SAMPLE_RATE, SAMPLES_PER_BUF);
    return PLAT_AUD_OK;
}

int plat_audio_submit(const s16 *grain) {
    s32 rc;

    if (A.handle < 0 || !A.aud_out || !grain)
        return PLAT_AUD_ENOTOPEN;

    /* BLOCKS until the previously queued grain has been consumed. The caller
       bounds how many of these it issues per video frame and times the span --
       LUAport is paced by vsync, not by audio. */
    rc = (s32)NC(A.gadget, A.aud_out, (u64)A.handle, (u64)grain, 0, 0, 0, 0);

    A.submitted++;
    if (rc < 0) {
        A.errors++;
        /* Logged only for the first few, so a systematic failure is visible
           without a per-frame flood destroying the timing it would explain. */
        if (A.errors <= 4)
            printf("AUDIO: submit -> 0x%08x %s\n",
                   (unsigned)rc, plat_audio_err_name(rc));
        return (int)rc;
    }
    return PLAT_AUD_OK;
}

s32 plat_audio_handle(void)    { return A.handle; }
u64 plat_audio_submitted(void) { return A.submitted; }
u64 plat_audio_errors(void)    { return A.errors; }

void plat_audio_shutdown(void) {
    if (A.handle >= 0 && A.aud_close) {
        NC(A.gadget, A.aud_close, (u64)A.handle, 0, 0, 0, 0, 0);
        printf("AUDIO: handle %d closed after %llu grains (%llu errors)\n",
               (int)A.handle,
               (unsigned long long)A.submitted,
               (unsigned long long)A.errors);
    }
    A.handle   = -1;
    A.aud_open = 0;
    A.aud_out  = 0;
    A.aud_init = 0;
    /* A.aud_close is deliberately retained: nothing calls it after this point,
       and clearing it would only matter if shutdown ran twice, which is safe
       either way because handle is now -1. */
}
