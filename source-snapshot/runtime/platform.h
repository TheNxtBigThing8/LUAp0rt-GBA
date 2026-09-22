#ifndef LUAPORT_PLATFORM_H
#define LUAPORT_PLATFORM_H

#include "core.h"

/* =========================================================================
 * LUAport -- PS5 platform layer: VideoOut bring-up, framebuffers, flip.
 * =========================================================================
 *
 * Adapted from LuaPSX/src/main.c lines 300-376 (bring-up) and 801-809
 * (teardown). Those lines are proven on retail hardware; the ORDER and the
 * TWO SLEEPS in them are load-bearing and are reproduced here verbatim.
 *
 * WHY THIS IS A SEPARATE TRANSLATION UNIT
 *   In LuaPSX the whole sequence is inlined into _start alongside pad, audio,
 *   disc scanning and the emulator loop, so there is no way to bring up video
 *   without dragging PlayStation-specific state along with it. Splitting it out
 *   is the single change that makes "video works" testable on its own, which is
 *   the entire purpose of Stage 2.
 *
 * WHAT DOES *NOT* BELONG HERE
 *   No emulator concepts of any kind. There is no emulator in M0. No scaling,
 *   no ROM geometry, no timing model, no gpSP. This file knows about exactly
 *   one thing: getting a 1920x1080 ARGB surface onto the television and taking
 *   it away again cleanly.
 *
 * OWNERSHIP OF THE DISPLAY
 *   The host title (Star Wars Racer Revenge, CUSA03474) runs a PS2 emulator
 *   that already owns the display. Its GS thread must be cancelled and its
 *   sceVideoOut handle closed before ours will open -- see plat_video_init().
 *   Both of those use offsets into the host eboot, from core.h.
 * ========================================================================= */

/* Return codes. -20/-21/-22 are DELIBERATELY the same numbers LuaPSX uses for
   the same three failures (main.c:341, :355, :372), so a status read back on
   the Lua side can be compared against the reference implementation directly
   rather than through a translation table. -23/-24 cover two failures LuaPSX
   never checked for because it could not proceed far enough to observe them. */
#define PLAT_VID_OK           0
#define PLAT_VID_EOPEN      -20   /* sceVideoOutOpen returned negative        */
#define PLAT_VID_EMAP       -21   /* sceKernelMapDirectMemory gave no address */
#define PLAT_VID_EREGISTER  -22   /* sceVideoOutRegisterBuffers non-zero      */
#define PLAT_VID_ESYMBOL    -23   /* libSceVideoOut / libkernel symbol missing */
#define PLAT_VID_EALLOC     -24   /* sceKernelAllocateDirectMemory gave no phys */

/* Stage 3. Kept in platform.h's numbering rather than the app's so that every
   code a caller can receive from this layer is defined in one place. */
#define PLAT_TIME_OK          0
#define PLAT_TIME_ESYMBOL   -26   /* gettimeofday unresolved                   */

/* Stage 4. -28/-29/-31 continue the same one-code-per-failure scheme. -30 is
   skipped because apps/m0diag/main.c already owns it as ST_SELFTEST; every code
   a caller can observe in ext->status is unique across BOTH files. */
#define PLAT_PAD_OK           0
#define PLAT_PAD_ESYMBOL    -28   /* scePadInit/GetHandle/Read unresolved      */
#define PLAT_PAD_EINIT      -29   /* libScePad.sprx would not load             */
#define PLAT_PAD_EHANDLE    -31   /* scePadGetHandle returned a negative handle */

/* Brings the display up. Must be called AFTER boot_data_region() and AFTER
 * shim_init(), because it logs through klog()/printf() and keeps its state in
 * .bss.
 *
 *   G           the eboot's argument-shuffling gadget
 *   D           sceKernelDlsym, as handed in by the Lua loader
 *   eboot_base  host eboot load address; EBOOT_GS_THREAD and EBOOT_VIDOUT are
 *               offsets into it
 *
 * Returns PLAT_VID_OK, or one of the negative codes above. On failure every
 * resource that was already acquired is released before returning, so a failed
 * init leaves nothing behind and the caller may simply return to Lua. */
int plat_video_init(void *G, void *D, u64 eboot_base);

/* The two registered framebuffers, index 0 or 1. SCR_W x SCR_H, ARGB8888,
   pitch == SCR_W. Returns 0 if video is not up or the index is out of range --
   callers must check, because a null here means "write nowhere", not "crash". */
u32 *plat_video_get_framebuffer(int index);

/* Fills one framebuffer with a single colour. Kept here rather than in gfx.c
   because it writes a HARDWARE framebuffer at SCR_W x SCR_H, whereas everything
   in gfx.c operates on the UI_W x UI_H software surface. Mixing the two is how
   a buffer-size confusion gets written. */
void plat_video_fill(int index, u32 color);

/* Submits the flip and waits for it to retire.
 *
 * frame_id is passed through to sceVideoOutSubmitFlip as its flip argument;
 * LuaPSX passes the running frame counter and so do we.
 *
 * The wait is a real vsync wait via sceKernelWaitEqueue when the flip event
 * queue came up. If it did NOT, this falls back to a fixed 16ms sleep -- see
 * the note in platform.c for why that fallback exists.
 *
 * Returns 0, or the negative return value of sceVideoOutSubmitFlip. */
int plat_video_present(int index, u32 frame_id);

/* True when the flip loop is paced by real flip events rather than by the
   sleep fallback. Reported once, so the log states which of the two the frame
   timing actually came from instead of leaving it to be assumed. */
int plat_video_is_vsync_paced(void);

/* Closes the display: blanks both buffers, flips the blank, then releases the
   VideoOut handle and the event queue. Safe to call when init failed or was
   never called. */
void plat_video_shutdown(void);

/* ----------------------------------------------------------------- timing --
 * Added at Stage 3.
 *
 * WHY THIS IS NOT shim.c's time().
 *   The shim already resolves gettimeofday and already calls it -- but
 *   now_seconds() (shim.c:491-497) reads tv.sec and THROWS AWAY tv.usec,
 *   because the only consumer it was written for is an emulated cartridge RTC
 *   that ticks in whole seconds. A frame-delta measured with it would be 0 for
 *   sixty consecutive frames and then 1000000, which produces an FPS reading of
 *   either infinity or one. It is not a source that can time a frame.
 *
 * WHY NOT SIMPLY WIDEN shim.c's HELPER.
 *   runtime/shim.c is byte-identical to LuaPSX/src/shim.c. That identity is a
 *   standing invariant: any difference between the two files is supposed to
 *   mean "a deliberate LUAport edit", and spending it on a helper that has a
 *   natural home elsewhere would weaken the check for no gain.
 *
 * The body is LuaPSX/src/main.c:96-100 unchanged -- same call form, same
 * two-u64 buffer, same multiply -- moved behind an API instead of being a file
 * static in the middle of the emulator's entry point. */

/* Resolves gettimeofday. Must be called AFTER boot_data_region(), since the
   resolved pointer is kept in .bss. Returns PLAT_TIME_OK or
   PLAT_TIME_ESYMBOL. */
int plat_time_init(void *G, void *D);

/* Microseconds since the Unix epoch. Returns 0 -- never a stale or garbage
   value -- if plat_time_init() was not called or failed, so a caller that
   ignores the init result gets a visibly dead clock rather than noise. */
u64 plat_time_us(void);

/* -------------------------------------------------------------------- pad --
 * Added at Stage 4. Every scePad call and every DualSense constant lives in
 * platform.c; the app above consumes struct plat_pad_state and nothing else.
 *
 * THE userId CONTRACT -- the single most important thing in this file.
 *
 *   scePadGetHandle needs the REAL user id, obtained on the Lua side from
 *   sceUserServiceGetInitialUser and handed across in ext->dbg[3].
 *
 *   0xFF (SCE_USER_SERVICE_USER_ID_SYSTEM) is NOT a user id. It is the
 *   sceVideoOutOpen convention -- plat_video_init() passes it, correctly -- and
 *   sceAudioOutOpen wants it too. Passing 0xFF to scePadGetHandle simply fails.
 *
 *   That is not a theoretical hazard: it is a LOGGED BUG in the reference.
 *   LuaPSX/tools/patch_video.py:112 passed 0xFF and produced "pad: N/A";
 *   patch_audio.py:57 is the commit that diagnosed it, and
 *   LuaPSX/src/main.c:380-384 carries the explanation in the final source. The
 *   two calls look alike and want opposite values. plat_pad_init() therefore
 *   takes the id as a parameter and REFUSES to invent one -- there is no
 *   default and no fallback, because a fallback here would silently reintroduce
 *   exactly the bug the reference already paid for.
 *
 * DualSense button bits, as used by LuaPSX/src/main.c:157-171. Those twelve are
 * confirmed by working reference code. See platform.c for the two that are not.
 */
#define PAD_L3        0x00000002u
#define PAD_R3        0x00000004u
#define PAD_OPTIONS   0x00000008u
#define PAD_UP        0x00000010u
#define PAD_RIGHT     0x00000020u
#define PAD_DOWN      0x00000040u
#define PAD_LEFT      0x00000080u
#define PAD_L2        0x00000100u
#define PAD_R2        0x00000200u
#define PAD_L1        0x00000400u
#define PAD_R1        0x00000800u
#define PAD_TRIANGLE  0x00001000u
#define PAD_CIRCLE    0x00002000u
#define PAD_CROSS     0x00004000u
#define PAD_SQUARE    0x00008000u
#define PAD_TOUCHPAD  0x00100000u

/* The four shoulders together. LuaPSX/src/main.c:161 uses this exact set as its
   menu chord, for a reason that carries over unchanged: every face and d-pad
   button belongs to the application, and nothing asks the player to hold all
   four shoulders at once. */
#define PAD_CHORD  (PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2)

/* Platform-neutral by intent: no GBA concepts, no emulator concepts, no key
   mapping. Stage 4 measures the pad; assigning meaning to a button is a later
   milestone's job and does not belong in the platform layer. */
struct plat_pad_state {
    u32 buttons;     /* masked to the 21 bits scePadRead reports              */
    u8  lx, ly;      /* left stick,  0..255, 128 nominal centre               */
    u8  rx, ry;      /* right stick, 0..255                                   */
    int connected;   /* 1 when THIS frame's read returned real data           */
};

/* Loads libScePad.sprx, resolves the three entry points, calls scePadInit and
 * acquires the handle for user_id.
 *
 * Must be called AFTER boot_data_region() -- state lives in .bss -- and takes
 * the id rather than reading ext itself, so the layer stays independent of the
 * launcher's argument block.
 *
 * Returns PLAT_PAD_OK, or PLAT_PAD_ESYMBOL / PLAT_PAD_EINIT / PLAT_PAD_EHANDLE.
 * Every failure logs the exact call, its return value, and the user id in play. */
int plat_pad_init(void *G, void *D, s32 user_id);

/* One frame's state. Returns 1 when fresh data was read, 0 on a transient
   failure -- see platform.c for what "transient" means here and why it is not
   an error. Safe to call when init failed: it fills a neutral state and returns
   0 rather than touching an unresolved pointer. */
int plat_pad_read(struct plat_pad_state *st);

/* For the diagnostic display and the log. Both return -1 when pad init did not
   succeed, so "no pad" is visible on screen rather than inferred. */
s32 plat_pad_user_id(void);
s32 plat_pad_handle(void);

/* Drops the layer's state. There is deliberately no scePadClose call -- see
   platform.c. Safe to call when init failed or never ran. */
void plat_pad_shutdown(void);

#endif
