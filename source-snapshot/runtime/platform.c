#include "platform.h"
#include "shim.h"

#include <stdio.h>

/* =========================================================================
 * PS5 VideoOut bring-up.
 *
 * PROVENANCE, line by line, against the known-working reference:
 *
 *   this file                       LuaPSX/src/main.c
 *   ---------------------------     -----------------------------------------
 *   symbol resolution               306-325
 *   GS thread cancel + 300ms        327-331
 *   close ps2emu's VideoOut + 100ms 333-335
 *   sceVideoOutOpen(0xFF, ...)      337-342
 *   equeue + AddFlipEvent           344-346
 *   AllocateDirectMemory            348-350
 *   MapDirectMemory                 351-356
 *   attribute block                 358-364
 *   two buffer pointers             366-368
 *   RegisterBuffers                 370-373
 *   SetFlipRate                     374
 *   clear both buffers              375-376
 *   SubmitFlip + WaitEqueue         237-241
 *   teardown                        801-809
 *
 * THE TWO SLEEPS ARE NOT PADDING. 300ms after cancelling the GS thread and
 * 100ms after closing ps2emu's VideoOut handle. They look removable and are
 * not: scePthreadCancel is asynchronous, so it requests cancellation and
 * returns immediately -- the thread is still inside the display driver when the
 * call comes back. Opening our own output while the old owner is mid-teardown
 * is what the delay exists to prevent. Stage 2's brief is fidelity over
 * cleanup, and these are the exact lines that brief is about.
 * ========================================================================= */

/* All of this lands in .bss, so nothing here is legal until boot_data_region()
   has run. plat_video_init() is documented as requiring that. */
static struct {
    void *G;

    /* libSceVideoOut */
    void *vid_open, *vid_close, *vid_reg, *vid_flip, *vid_rate, *vid_evt;

    /* libkernel */
    void *usleep, *create_eq, *wait_eq, *delete_eq;
    void *alloc_dm, *map_dm, *dm_size;

    u64   eq;          /* flip event queue, 0 if it could not be created  */
    s32   video;       /* sceVideoOut handle, -1 when not open            */
    void *vmem;        /* base of the mapped direct memory                */
    u32  *fbs[2];      /* the two registered framebuffers                 */

    u32   frames;      /* flips submitted, for the teardown flip argument */
    int   up;          /* 1 once RegisterBuffers has succeeded            */
    int   paced;       /* 1 when flip events are pacing the loop          */
} V;

/* ----------------------------------------------------------------- timing --
   Separate state from V on purpose: the clock is usable whether or not video
   came up, and Stage 3 reports "timing init" and "video init" as two distinct
   milestones. Sharing one struct would tie their lifetimes together for no
   reason.

   tvbuf is a static rather than a local because that is how LuaPSX/src/main.c
   does it (line 94) and the call is on the per-frame path; keeping it identical
   avoids introducing a stack-address argument where the reference passes a
   .bss one. */
static struct {
    void *G;
    void *gettod;
} T;

static u64 tvbuf[2];

int plat_time_init(void *G, void *D) {
    T.G      = G;
    T.gettod = SYM(G, D, LIBKERNEL_HANDLE, "gettimeofday");

    if (!T.gettod) {
        klog("TIME: FAIL gettimeofday unresolved\n");
        return PLAT_TIME_ESYMBOL;
    }
    printf("TIME: gettimeofday -> %p\n", T.gettod);
    return PLAT_TIME_OK;
}

/* LuaPSX/src/main.c:96-100, unchanged. */
u64 plat_time_us(void) {
    if (!T.gettod) return 0;
    NC(T.G, T.gettod, (u64)tvbuf, 0, 0, 0, 0, 0);
    return (u64)tvbuf[0] * 1000000ULL + (u64)tvbuf[1];
}

/* ------------------------------------------------------------------------- */

u32 *plat_video_get_framebuffer(int index) {
    if (!V.up || index < 0 || index > 1) return 0;
    return V.fbs[index];
}

int plat_video_is_vsync_paced(void) {
    return V.paced;
}

void plat_video_fill(int index, u32 color) {
    u32 *fb = plat_video_get_framebuffer(index);
    if (!fb) return;
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = color;
}

int plat_video_present(int index, u32 frame_id) {
    if (!V.up || index < 0 || index > 1) return PLAT_VID_EREGISTER;

    s32 r = (s32)NC(V.G, V.vid_flip, (u64)V.video, (u64)index, 1,
                    (u64)frame_id, 0, 0);
    if (r != 0) return (int)r;

    V.frames++;

    if (V.eq && V.wait_eq) {
        /* 64 bytes is LuaPSX's own event buffer size (main.c:239). One event
           is requested and the returned count is deliberately ignored: the
           call returning at all is the vsync edge we are waiting for. */
        u8  evt[64];
        s32 cnt = 0;
        NC(V.G, V.wait_eq, V.eq, (u64)evt, 1, (u64)&cnt, 0, 0);
    } else if (V.usleep) {
        /* FALLBACK, ADDITIVE -- this branch does not exist in LuaPSX.
         *
         * LuaPSX always has its event queue, so it never had to consider what
         * happens without one: the flip loop would spin as fast as the CPU
         * allows and 300 "frames" would elapse in microseconds. The test colour
         * would never appear, and the failure would look identical to video
         * never coming up at all -- the one confusion Stage 2 exists to avoid.
         *
         * 16ms is approximately one 60Hz frame. It is NOT a claim that the
         * display is 60Hz; it is a floor that keeps the colour on screen long
         * enough to be seen and photographed. plat_video_is_vsync_paced()
         * reports which path was taken so the log never leaves it ambiguous. */
        NC(V.G, V.usleep, 16000, 0, 0, 0, 0, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

int plat_video_init(void *G, void *D, u64 eboot_base) {
    V.G     = G;
    V.video = -1;
    V.eq    = 0;
    V.up    = 0;
    V.paced = 0;

    klog("VIDEO: resolving symbols\n");

    void *load_mod = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLoadStartModule");
    void *cancel   = SYM(G, D, LIBKERNEL_HANDLE, "scePthreadCancel");

    V.usleep    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelUsleep");
    V.alloc_dm  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelAllocateDirectMemory");
    V.map_dm    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMapDirectMemory");
    V.dm_size   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelGetDirectMemorySize");
    V.create_eq = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelCreateEqueue");
    V.wait_eq   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelWaitEqueue");
    V.delete_eq = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelDeleteEqueue");

    if (!load_mod) {
        klog("VIDEO: FAIL sceKernelLoadStartModule unresolved\n");
        return PLAT_VID_ESYMBOL;
    }
    if (!V.alloc_dm || !V.map_dm) {
        klog("VIDEO: FAIL direct memory symbols unresolved\n");
        return PLAT_VID_ESYMBOL;
    }

    s32 vid_mod = (s32)NC(G, load_mod, (u64)"libSceVideoOut.sprx", 0, 0, 0, 0, 0);
    printf("VIDEO: libSceVideoOut.sprx -> handle 0x%08x\n", (unsigned)vid_mod);

    V.vid_open  = SYM(G, D, vid_mod, "sceVideoOutOpen");
    V.vid_close = SYM(G, D, vid_mod, "sceVideoOutClose");
    V.vid_reg   = SYM(G, D, vid_mod, "sceVideoOutRegisterBuffers");
    V.vid_flip  = SYM(G, D, vid_mod, "sceVideoOutSubmitFlip");
    V.vid_rate  = SYM(G, D, vid_mod, "sceVideoOutSetFlipRate");
    V.vid_evt   = SYM(G, D, vid_mod, "sceVideoOutAddFlipEvent");

    /* Named individually rather than as one combined check: on firmware 7.61
       this is the first time these exports have been resolved by LUAport, and
       "which symbol" is a far more actionable log line than "a symbol". */
    if (!V.vid_open || !V.vid_reg || !V.vid_flip) {
        printf("VIDEO: FAIL symbols open=%p reg=%p flip=%p close=%p "
               "rate=%p evt=%p\n",
               V.vid_open, V.vid_reg, V.vid_flip,
               V.vid_close, V.vid_rate, V.vid_evt);
        return PLAT_VID_ESYMBOL;
    }

    /* ---- take the display away from ps2emu ------------------------------
       Done FIRST, before anything is allocated, so a fault here leaves a black
       screen rather than a frozen host-game frame (LuaPSX main.c:302-305). */
    if (cancel) {
        u64 gs = *(u64 *)(eboot_base + EBOOT_GS_THREAD);
        printf("VIDEO: ps2emu GS thread handle 0x%lx\n", (unsigned long)gs);
        if (gs) NC(G, cancel, gs, 0, 0, 0, 0, 0);
    } else {
        klog("VIDEO: scePthreadCancel unresolved, GS thread left running\n");
    }
    if (V.usleep) NC(G, V.usleep, 300000, 0, 0, 0, 0, 0);

    s32 emu_vid = *(s32 *)(eboot_base + EBOOT_VIDOUT);
    printf("VIDEO: ps2emu VideoOut handle %d\n", (int)emu_vid);
    if (V.vid_close && emu_vid >= 0) NC(G, V.vid_close, (u64)emu_vid, 0, 0, 0, 0, 0);
    if (V.usleep) NC(G, V.usleep, 100000, 0, 0, 0, 0, 0);
    klog("VIDEO: old output closed\n");

    /* ---- open our own output ------------------------------------------
       0xFF is SCE_USER_SERVICE_USER_ID_SYSTEM. It is the sceVideoOutOpen
       convention and is NOT a user id -- LuaPSX main.c:380-384 records the bug
       that came from confusing the two. Stage 2 loads no user service at all,
       so 0xFF is the only value available and is also the correct one. */
    s32 video = (s32)NC(G, V.vid_open, 0xFF, 0, 0, 0, 0, 0);
    if (video < 0) {
        printf("VIDEO: FAIL sceVideoOutOpen -> %d (0x%08x)\n",
               (int)video, (unsigned)video);
        return PLAT_VID_EOPEN;
    }
    V.video = video;
    printf("VIDEO: open OK, handle %d\n", (int)video);

    /* ---- flip event queue ---------------------------------------------- */
    if (V.create_eq) NC(G, V.create_eq, (u64)&V.eq, (u64)"lupq", 0, 0, 0, 0);
    if (V.vid_evt && V.eq) NC(G, V.vid_evt, V.eq, (u64)V.video, 0, 0, 0, 0);

    if (V.eq && V.wait_eq) {
        V.paced = 1;
        klog("VIDEO: event queue OK\n");
    } else {
        /* Not fatal. A flip still happens; only the pacing changes. Saying so
           explicitly beats a silent downgrade that later shows up as "the
           colour flashed past too fast to see". */
        klog("VIDEO: event queue UNAVAILABLE -- pacing by sleep\n");
    }

    /* ---- direct memory --------------------------------------------------
       FB_TOTAL is two 2MB-aligned 1920x1080x4 buffers = 16MB. The search range
       is the whole of direct memory; alignment 0x200000 and memory type 3 are
       LuaPSX's values unchanged. */
    u64 mem_total = V.dm_size ? NC(G, V.dm_size, 0, 0, 0, 0, 0, 0)
                              : 0x300000000ULL;
    u64 phys = 0;
    NC(G, V.alloc_dm, 0, mem_total, FB_TOTAL, 0x200000, 3, (u64)&phys);
    if (!phys) {
        printf("VIDEO: FAIL AllocateDirectMemory (total 0x%lx, want 0x%x)\n",
               (unsigned long)mem_total, (unsigned)FB_TOTAL);
        plat_video_shutdown();
        return PLAT_VID_EALLOC;
    }
    printf("VIDEO: direct memory OK, phys 0x%lx size 0x%x\n",
           (unsigned long)phys, (unsigned)FB_TOTAL);

    void *vmem = 0;
    NC(G, V.map_dm, (u64)&vmem, FB_TOTAL, 0x33, 0, phys, 0x200000);
    if (!vmem) {
        klog("VIDEO: FAIL MapDirectMemory returned no address\n");
        plat_video_shutdown();
        return PLAT_VID_EMAP;
    }
    V.vmem = vmem;
    printf("VIDEO: mmap OK at %p\n", vmem);

    /* ---- buffer attributes ---------------------------------------------
       Byte-for-byte LuaPSX main.c:358-364. Offsets in the attribute block:
         +0  pixel format 0x80000000 (ARGB8888, BGRA order on the wire)
         +4  tiling mode 1 (linear)
         +8  aspect ratio, left 0
         +12 width, +16 height, +20 pitch in pixels
       The remaining 40 bytes stay zero. */
    u8 attr[64];
    for (int i = 0; i < 64; i++) attr[i] = 0;
    *(u32 *)(attr + 0)  = 0x80000000;
    *(u32 *)(attr + 4)  = 1;
    *(u32 *)(attr + 12) = SCR_W;
    *(u32 *)(attr + 16) = SCR_H;
    *(u32 *)(attr + 20) = SCR_W;

    V.fbs[0] = (u32 *)vmem;
    V.fbs[1] = (u32 *)((u8 *)vmem + FB_ALIGNED);

    s32 rr = (s32)NC(G, V.vid_reg, (u64)V.video, 0, (u64)V.fbs, 2, (u64)attr, 0);
    if (rr != 0) {
        printf("VIDEO: FAIL RegisterBuffers -> %d (0x%08x)\n",
               (int)rr, (unsigned)rr);
        plat_video_shutdown();
        return PLAT_VID_EREGISTER;
    }
    V.up = 1;
    printf("VIDEO: buffers registered (%dx%d, fb0 %p, fb1 %p)\n",
           SCR_W, SCR_H, (void *)V.fbs[0], (void *)V.fbs[1]);

    /* Rate 0 = every vsync. */
    if (V.vid_rate) NC(G, V.vid_rate, (u64)V.video, 0, 0, 0, 0, 0);
    klog("VIDEO: flip rate set\n");

    plat_video_fill(0, 0xFF000000u);
    plat_video_fill(1, 0xFF000000u);
    klog("VIDEO: framebuffer cleared\n");

    return PLAT_VID_OK;
}

/* ------------------------------------------------------------------------- */

void plat_video_shutdown(void) {
    klog("VIDEO: shutdown start\n");

    /* Blank before releasing, so the display is black at the moment the handle
       goes away rather than holding our last frame (LuaPSX main.c:803-806). */
    if (V.up) {
        plat_video_fill(0, 0xFF000000u);
        plat_video_fill(1, 0xFF000000u);
        NC(V.G, V.vid_flip, (u64)V.video, 0, 1, (u64)V.frames, 0, 0);
        if (V.usleep) NC(V.G, V.usleep, 50000, 0, 0, 0, 0, 0);
    }

    if (V.vid_close && V.video >= 0) NC(V.G, V.vid_close, (u64)V.video, 0, 0, 0, 0, 0);
    if (V.delete_eq && V.eq)         NC(V.G, V.delete_eq, V.eq, 0, 0, 0, 0, 0);

    /* The direct memory is NOT released.
     *
     * This matches LuaPSX exactly (main.c:801-809 closes the output and the
     * queue and stops there), and Stage 2's brief is fidelity over cleanup. It
     * is a real leak of FB_TOTAL = 16MB per run, and it is disclosed rather
     * than silently fixed: sceKernelReleaseDirectMemory is not in the proven
     * sequence, and introducing an untested teardown call is exactly the kind
     * of change that turns a clean Stage 2 result into an ambiguous one.
     *
     * OPERATIONAL CONSEQUENCE: relaunch the host game between Stage 2 runs.
     * Repeated runs in one session will eventually exhaust direct memory, and
     * that shows up as AllocateDirectMemory failing (status -24) on a build
     * that worked minutes earlier. */

    V.up    = 0;
    V.video = -1;
    V.eq    = 0;

    klog("VIDEO: shutdown OK\n");
}

/* =========================================================================
 * DualSense input.  Added at Stage 4.
 *
 * PROVENANCE against the known-working reference:
 *
 *   this file                  LuaPSX/src/main.c
 *   ------------------------   ---------------------------------------------
 *   module load + 3 symbols    387-390
 *   scePadInit                 391
 *   scePadGetHandle(userId)    393
 *   read: zero, call, screen   173-181  (read_pad_raw, reproduced exactly)
 *   analog byte offsets 4..7   tools/patch_analog.py:65-67
 *   button bit values          157-171
 * ========================================================================= */

static struct {
    void *G;
    void *pad_init, *pad_geth, *pad_read;
    s32   user_id;
    s32   handle;
    int   up;
    u8    lx, ly, rx, ry;   /* last good sticks -- see plat_pad_read() */
} P;

/* LuaPSX declares its equivalent as `u8 pad_buf[128]` on _start's stack
   (main.c:394). Here it is .bss for the same reason tvbuf is: the address is
   handed to a system call on the per-frame path, and a .bss address has a
   lifetime that obviously outlives the call. 128 bytes is the reference's own
   size and is not a guess about sizeof(ScePadData). */
static u8 pad_buf[128];

s32 plat_pad_user_id(void) { return P.up ? P.user_id : -1; }
s32 plat_pad_handle(void)  { return P.up ? P.handle  : -1; }

int plat_pad_init(void *G, void *D, s32 user_id) {
    P.G       = G;
    P.handle  = -1;
    P.up      = 0;
    P.user_id = user_id;

    /* Neutral until the first successful read, so a display that renders before
       any read shows centred sticks rather than a hard corner. */
    P.lx = P.ly = P.rx = P.ry = 128;

    klog("PAD: resolving symbols\n");

    void *load_mod = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLoadStartModule");
    if (!load_mod) {
        klog("PAD: FAIL sceKernelLoadStartModule unresolved\n");
        return PLAT_PAD_ESYMBOL;
    }

    s32 pad_mod = (s32)NC(G, load_mod, (u64)"libScePad.sprx", 0, 0, 0, 0, 0);
    printf("PAD: libScePad.sprx -> handle 0x%08x\n", (unsigned)pad_mod);
    if (pad_mod < 0) {
        printf("PAD: FAIL libScePad.sprx load -> %d\n", (int)pad_mod);
        return PLAT_PAD_EINIT;
    }

    P.pad_init = SYM(G, D, pad_mod, "scePadInit");
    P.pad_geth = SYM(G, D, pad_mod, "scePadGetHandle");
    P.pad_read = SYM(G, D, pad_mod, "scePadRead");

    /* Named individually rather than as one combined check, for the same reason
       plat_video_init() does it: "which symbol" is actionable, "a symbol" is
       not, and this is the first time these exports have been resolved by
       LUAport on firmware 7.61. */
    if (!P.pad_init || !P.pad_geth || !P.pad_read) {
        printf("PAD: FAIL symbols init=%p geth=%p read=%p\n",
               P.pad_init, P.pad_geth, P.pad_read);
        return PLAT_PAD_ESYMBOL;
    }

    /* scePadInit's RETURN IS LOGGED BUT NOT TREATED AS FATAL, deliberately.
     *
     * LuaPSX ignores it outright (main.c:391 is a bare call). The reason that
     * is right, rather than merely convenient, is that the host title is a
     * running PS2 emulator which has already initialised scePad for its own
     * input -- so "already initialised" is the EXPECTED outcome here, not a
     * failure. Rejecting a non-zero return would fail on exactly the hardware
     * configuration this payload is built for, while the reference succeeds.
     *
     * The value is printed because Stage 4's brief asks for the exact return of
     * every pad call; the real gate on usability is the handle, below. */
    s32 ir = (s32)NC(G, P.pad_init, 0, 0, 0, 0, 0, 0);
    printf("PAD: scePadInit -> %d (0x%08x)%s\n", (int)ir, (unsigned)ir,
           ir == 0 ? "" : "  [non-zero is expected if the host already init'd]");

    /* THE REAL userId, NOT 0xFF. See the contract note in platform.h. */
    printf("PAD: userId %d (0x%08x)\n", (int)user_id, (unsigned)user_id);
    if (user_id == 0xFF) {
        /* Not rejected -- 0xFF is a legal integer and the caller may genuinely
           have been handed it -- but called out loudly, because this is the
           precise value that produced "pad: N/A" in the reference and it would
           otherwise look like an ordinary id in the log. */
        klog("PAD: WARNING userId is 0xFF, which is the SYSTEM id, not a user "
             "id -- scePadGetHandle is expected to fail\n");
    }

    s32 h = (s32)NC(G, P.pad_geth, (u64)user_id, 0, 0, 0, 0, 0);
    printf("PAD: scePadGetHandle(userId %d) -> %d (0x%08x)\n",
           (int)user_id, (int)h, (unsigned)h);
    if (h < 0) {
        printf("PAD: FAIL handle %d for userId %d\n", (int)h, (int)user_id);
        return PLAT_PAD_EHANDLE;
    }

    P.handle = h;
    P.up     = 1;
    printf("PAD: init OK (userId %d, handle %d)\n", (int)user_id, (int)h);
    return PLAT_PAD_OK;
}

/* LuaPSX/src/main.c:173-181 (read_pad_raw), reproduced call for call:
 *   zero the buffer, scePadRead(handle, buf, 1), reject a non-positive or
 *   error-shaped count, take the first u32, reject it if bit 31 is set, and
 *   mask to the low 21 bits.
 *
 * A rejected read is NOT an error. scePadRead legitimately returns nothing when
 * no new sample is ready, and the reference treats that as "no buttons this
 * frame" and carries on -- so this returns 0 and the caller keeps running.
 *
 * ONE DELIBERATE DEVIATION, and it is the only one in this file.
 *   LuaPSX reads pad_buf[4..7] unconditionally after a failed read
 *   (patch_analog.py:65-67), and since the buffer was just zeroed, a transient
 *   failure feeds the sticks 0,0 -- which is not "centre", it is hard
 *   left-and-up. Inside a game that is a single invisible frame. On a
 *   MEASUREMENT screen it would read as the sticks slamming into a corner, and
 *   the operator would be looking at a hardware fault that did not happen.
 *   So the last good stick values are held across a dropped read, and
 *   `connected` reports that the frame was dropped. Buttons still go to 0,
 *   exactly as the reference does. */
int plat_pad_read(struct plat_pad_state *st) {
    if (!st) return 0;

    st->buttons   = 0;
    st->lx        = P.lx;
    st->ly        = P.ly;
    st->rx        = P.rx;
    st->ry        = P.ry;
    st->connected = 0;

    if (!P.up || P.handle < 0 || !P.pad_read) return 0;

    for (int i = 0; i < 128; i++) pad_buf[i] = 0;

    s32 n = (s32)NC(P.G, P.pad_read, (u64)P.handle, (u64)pad_buf, 1, 0, 0, 0);
    if (n <= 0 || (u32)n >= 0x80000000u) return 0;

    u32 r = *(u32 *)pad_buf;
    if (r & 0x80000000u) return 0;

    st->buttons = r & 0x001FFFFFu;

    /* ScePadData: leftStick at byte 4, rightStick at byte 6, each a pair of
       uint8_t. Unscaled 0..255 on both sides -- patch_analog.py:65-67. Stage 4
       applies NO deadzone and NO normalisation: this stage measures the pad, and
       a policy applied here would be indistinguishable from the hardware's own
       behaviour on the screen that is meant to characterise it. */
    P.lx = st->lx = pad_buf[4];
    P.ly = st->ly = pad_buf[5];
    P.rx = st->rx = pad_buf[6];
    P.ry = st->ry = pad_buf[7];

    st->connected = 1;
    return 1;
}

void plat_pad_shutdown(void) {
    if (!P.up) {
        klog("PAD: shutdown (nothing to release)\n");
        return;
    }

    /* NO scePadClose.
     *
     * The reference never closes the pad -- LuaPSX's teardown (main.c:801-809)
     * releases the video output and the event queue and stops. The handle is
     * owned by a process that continues running after this payload returns, and
     * introducing an untested close into the one path whose job is a clean
     * return is exactly the kind of change that turns a clean Stage 4 result
     * into an ambiguous one. This is the same fidelity-over-cleanup call the
     * direct-memory note above documents, and it is disclosed for the same
     * reason.
     *
     * The state is dropped so a later plat_pad_read() cannot use a stale
     * handle. */
    P.up     = 0;
    P.handle = -1;
    klog("PAD: shutdown OK\n");
}
