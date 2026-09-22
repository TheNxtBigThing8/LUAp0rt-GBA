#ifndef CORE_H
#define CORE_H

/* -------------------------------------------------------------------------
 * LUAport -- generic PS5 native runtime.
 *
 * Adapted from LuaPSX/src/core.h. That file in turn carried its header over
 * from LuaMD, which carried it from LuaGB: this runtime has now survived three
 * emulator cores unchanged in its essentials, which is the evidence behind the
 * report's claim that the platform layer is separable from the emulator.
 *
 * PS5 runtime pattern: EmuC0re by egycnq / EgyDevTeam.
 * Delivery substrate: Luac0re by Gezine.
 *
 * WHAT WAS REMOVED relative to LuaPSX/src/core.h, and why:
 *   PSX_FB_W/H              PlayStation framebuffer bounds -- emulator specific
 *   MD_FB_W/H, SCALE        Mega Drive scaler geometry -- emulator specific
 *   MD_FM_RATE, MD_PSG_RATE Mega Drive sound chip rates -- emulator specific
 *   SAMPLE_RATE, SAMPLES_PER_BUF, AUDIO_S16_STEREO
 *                           audio was deferred to M10. RESTORED AT M10 -- see
 *                           the block below. They are the ONLY three names this
 *                           file has regained since M0.
 *   ROM_DIR, SAVE_DIR       ROM/save paths -- belong to a core, arrive at M4/M12
 *   struct rom_entry, MAX_ROMS, MAX_NAME
 *                           ROM picker -- arrives at M13
 *
 * Nothing was added. Everything retained is byte-identical in behaviour to the
 * LuaPSX original so that a divergence between LUAport and the known-working
 * reference is always a deliberate edit rather than drift.
 * ------------------------------------------------------------------------- */

typedef unsigned long  u64;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef long           s64;
typedef int            s32;
typedef short          s16;
typedef signed char    s8;

/* Offsets into the host game's eboot (Star Wars Racer Revenge). Properties of
   the host title, not of the emulator, so they carry over unchanged.

   These are the single most host-specific numbers in the runtime; see the
   report's risk 4.

   VERIFICATION STATUS, updated after the M0 Stage 1 hardware run:
     GADGET_OFFSET    PROVEN on firmware 7.61 with CUSA03474. Stage 1 routed
                      every dlsym and every syscall through this gadget, so a
                      wrong value could not have produced a clean return.
     EBOOT_GS_THREAD  Carried from LuaPSX, NOT yet exercised. First used at
     EBOOT_VIDOUT     Stage 2.

   Both of the unproven two are offsets into the SAME host eboot that
   GADGET_OFFSET indexes, and an eboot is a property of the title, not of the
   system firmware -- the file is byte-identical on 4.51 and 7.61. Since
   GADGET_OFFSET resolved correctly on 7.61, the image is laid out as expected
   and these two should be equally valid. That is a strong inference, not a
   measurement, which is why it is written down as one. */
#define GADGET_OFFSET    0x31AA9     /* the native_call trampoline gadget    */
#define LIBKERNEL_HANDLE 0x2001
#define EBOOT_GS_THREAD  0x057F89B0  /* pthread handle of the ps2emu GS thread */
#define EBOOT_VIDOUT     0x02d695d0  /* ps2emu's own sceVideoOut handle        */

/* Output geometry. */
#define SCR_W       1920
#define SCR_H       1080
#define FB_SIZE     (SCR_W * SCR_H * 4)
#define FB_ALIGNED  ((FB_SIZE + 0x1FFFFF) & ~0x1FFFFF)
#define FB_TOTAL    (FB_ALIGNED * 2)

/* ---------------------------------------------------------------- audio ----
 * RESTORED AT M10, byte-identical to LuaPSX/src/core.h:78-80. These are the
 * three names the header note above records as removed at M0; nothing else is
 * restored and nothing is added.
 *
 * THEY ARE PROPERTIES OF THE PS5 AUDIO PORT, NOT OF ANY EMULATOR.
 *   SAMPLE_RATE      48000 is the ONLY legal rate for a MAIN port.
 *                    sceAudioOutOpen returns 0x80260008
 *                    INVALID_SAMPLE_FREQ for anything else, which is why the
 *                    GBA's 65536 Hz must be converted rather than requested.
 *   SAMPLES_PER_BUF  the grain. sceAudioOutOutput takes whole grains only and
 *                    returns 0x80260006 INVALID_SIZE otherwise.
 *   AUDIO_S16_STEREO the format selector: signed 16-bit, 2 channels,
 *                    interleaved L,R.
 *
 * THREE PURE #defines. No code, no data, no string, and no translation unit
 * changes behaviour by their presence -- an image that does not open an audio
 * port is byte-identical with and without them, which is what keeps M0-M9
 * unaffected by this edit. */
#define SAMPLE_RATE      48000
#define SAMPLES_PER_BUF  256
#define AUDIO_S16_STEREO 1

/* Diagnostic/UI resolution.

   480x270 at an integer 4x is exactly 1920x1080, so the surface fills the frame
   with no pillarbox and no fractional scaling. The 8x8 glyph lands 32 screen
   pixels tall and the usable text area is 60 columns by 33 rows. LuaGB, LuaMD
   and LuaPSX all use these same numbers. */
#define UI_W      480
#define UI_H      270
#define UI_SCALE  4                       /* 480*4 = 1920, 270*4 = 1080 */

/* Calls a native function through the host eboot's argument-shuffling gadget.
   rdi = gadget, rsi = target fn, then the six real arguments. The gadget
   expects the callee in rbx and the first argument already in rdi. */
__attribute__((naked))
static u64 native_call(void *gadget, void *fn,
                       u64 a1, u64 a2, u64 a3,
                       u64 a4, u64 a5, u64 a6)
{
    __asm__ volatile (
        "pushq %%rbx\n\t"
        "movq %%rsi, %%rbx\n\t"
        "movq %%rdi, %%rax\n\t"
        "movq %%rdx, %%rdi\n\t"
        "movq %%rcx, %%rsi\n\t"
        "movq %%r8,  %%rdx\n\t"
        "movq %%r9,  %%rcx\n\t"
        "movq 16(%%rsp), %%r8\n\t"
        "movq 24(%%rsp), %%r9\n\t"
        "callq *%%rax\n\t"
        "popq %%rbx\n\t"
        "retq" ::: "memory"
    );
}

static void *resolve_sym(void *gadget, void *dlsym_fn, s32 handle, const char *name) {
    void *addr = 0;
    native_call(gadget, dlsym_fn, (u64)handle, (u64)name, (u64)&addr, 0, 0, 0);
    return addr;
}

/* Overridable so the shim can be driven by a host test harness, which calls
   targets directly instead of through the eboot's argument-shuffling gadget.
   The host ABI is the same System V one, so only the trampoline differs. */
#ifndef NC
#define NC  native_call
#endif
#define SYM resolve_sym

/* Arguments handed in by the Lua launcher. Layout is mirrored by the Lua
 * template -- keep the two in sync.
 *
 * dbg[3] carries the real userId from sceUserServiceGetInitialUser on the Lua
 * side. scePadGetHandle needs that real id; sceAudioOutOpen needs 0xFF instead.
 * The two calls look alike and are not -- this was a real logged bug in LuaPSX
 * and the distinction must survive into LUAport. */
struct ext_args {
    s64 status;
    s64 step;
    u32 frame_count;
    u32 _pad;
    s32 log_fd;
    s32 pad_fd;
    u8  log_addr[16];
    u64 dbg[8];
};

#endif
