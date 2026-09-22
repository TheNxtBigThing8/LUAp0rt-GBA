#ifndef GBA_PRESENT_H
#define GBA_PRESENT_H

#include "core.h"

/* =============================================================================
 * LUAport M7 -- adapters/gba/gba_present.h
 *
 * THE GBA -> PS5 PRESENTATION ADAPTER: 240x160 RGB565 -> 1920x1080 ARGB8888.
 * =============================================================================
 *
 * WHY THIS LIVES IN adapters/gba/ AND NOT IN runtime/
 * ---------------------------------------------------
 * The codebase already decided this twice, in writing:
 *
 *   runtime/gfx.h:12-14 removed blit_scale() with the note "The GBA adapter will
 *   want its own 240x160 equivalent at M6, and IT BELONGS IN adapters/gba/, NOT
 *   HERE."
 *
 *   runtime/platform.h:21-26 "No emulator concepts of any kind. No scaling, no
 *   ROM geometry."
 *
 * A 240x160 RGB565 scaler is an emulator concept. Putting it in runtime/ would
 * violate both notes; leaving it inside apps/m7gpsp/main.c would work but would
 * mix the production conversion with the fixture's assertions and block reuse by
 * M8/M9. So it gets its own translation unit, with the narrowest surface that
 * still does the job.
 *
 * =============================================================================
 * THE SOURCE FORMAT -- RGB565 WITH A FIVE-BIT GREEN. VERIFIED, NOT ASSUMED.
 * =============================================================================
 * gpsp/common.h:104-110 selects convert_palette() on USE_XBGR1555_FORMAT. That
 * macro is NOT defined by the M6 or M7 build (the only gpSP defines are
 * -DINLINE=inline -DNDEBUG and -DROM_BUFFER_SIZE=2; the sole definition anywhere
 * in the tree is gpsp/Makefile:451, a PS2 target that is never invoked), so the
 * live branch is:
 *
 *     (((v & 0x1F) << 11) | ((v & 0x03E0) << 1) | ((v >> 10) & 0x1F))
 *
 * Input is GBA-native BGR555. Output bit positions:
 *
 *     R  bits 15..11     5 meaningful bits
 *     G  bits 10..6      5 meaningful bits   <-- NOT SIX
 *     B  bits  4..0      5 meaningful bits
 *     bit 5              STRUCTURALLY ALWAYS ZERO
 *
 * `(v & 0x03E0) << 1` shifts a FIVE-bit field left by one. Nothing anywhere ever
 * sets bit 5. So the green channel is a 5-bit value sitting in a 6-bit field.
 *
 * THE TRAP THIS AVOIDS, AND IT IS A SILENT ONE.
 * Reading green as a genuine 6-bit field -- g6 = (p >> 5) & 0x3F, expanded with
 * (v << 2) | (v >> 4) -- gives, for pure green 0x07C0: g6 = 62, and
 * 62 -> (62<<2)|(62>>4) = 248 | 3 = 251. Green would saturate at 251 AND NEVER
 * REACH 255. Every white pixel would render (255, 251, 255): a faint magenta
 * cast that looks entirely plausible on a photograph and is wrong. The five-bit
 * treatment is mandatory, and it is correct precisely BECAUSE bit 5 is provably
 * always zero.
 *
 * =============================================================================
 * THE DESTINATION FORMAT -- 0xAARRGGBB. THREE AGREEING SOURCES.
 * =============================================================================
 *   1. runtime/platform.c:304-305 paints BLACK with 0xFF000000 -- alpha in bits
 *      31..24, RGB zero.
 *   2. LuaPSX/src/ui.c:151, the hardware-proven reference blit, assembles
 *      0xFF000000u | (r << 16) | (g << 8) | b.
 *   3. runtime/gfx.h:22 defines COL_BRAND 0xFF9BBC0F -- 9B BC 0F is the
 *      canonical DMG green #9BBC0F, i.e. R at 23..16, G at 15..8, B at 7..0.
 *
 * platform.c:274's "BGRA order on the wire" comment is consistent: little-endian
 * storage of 0xAARRGGBB yields the bytes B,G,R,A. Writing the u32 is correct and
 * no byte swapping is required in C.
 *
 * =============================================================================
 * FIVE-BIT EXPANSION IS BIT REPLICATION, NOT A SHIFT
 * =============================================================================
 * (v << 3) | (v >> 2), which is LuaPSX/src/ui.c:142's x5to8() unchanged. The
 * reference states the reason at ui.c:140-141: "31 maps to 255 rather than 248;
 * a plain <<3 leaves whites visibly grey."
 *
 * =============================================================================
 * WHAT THIS ADAPTER DELIBERATELY DOES NOT DO
 * =============================================================================
 *   - does NOT allocate anything AT RUN TIME, and does not stage a whole frame.
 *     gba_present_blit() writes DIRECTLY into the caller's PS5 direct-memory
 *     framebuffer. A 1920x1080 u32 staging surface would be 8,294,400 bytes and
 *     would breach the 2 MB .bss tripwire in tools/check_forbidden.py by more
 *     than 4x.
 *
 *     IT DOES, SINCE M9-E, OWN ONE STATIC SCALED ROW: 1440 u32 = 5,760 bytes of
 *     .bss, three orders of magnitude below a full staging surface. That row is
 *     what lets the blit be WRITE-ONLY with respect to the framebuffer. The
 *     previous implementation read 1,152,000 u32 back out of write-combined
 *     GARLIC memory every frame to replicate rows, and M9-D measured that on
 *     hardware at ~297,864 us of a ~298,091 us blit -- 99.9% of presentation
 *     cost, against a framebuffer read bandwidth of ~24-25 MB/s versus
 *     ~8,594 MB/s from .bss. There is no per-frame and no dynamic allocation:
 *     the row is reserved once, in .bss, at link time.
 *   - does NOT modify the source. The source parameter is `const u16 *`, so an
 *     in-place mutation is a COMPILE ERROR rather than a runtime surprise.
 *   - does NOT call any PS5 API. It knows nothing about sceVideoOut, direct
 *     memory or flips; it converts pixels into a buffer it is handed.
 *   - does NOT filter, interpolate, stretch or correct aspect. Integer nearest
 *     neighbour only.
 *   - does NOT read the 161st scratch row. The loop bound is SRC_H = 160.
 * ========================================================================== */

/* gpsp/common.h:112-119 -- GBA_SCREEN_PITCH 240, GBA_SCREEN_HEIGHT 160. The
   source PITCH equals the source WIDTH; there is no padding between rows in the
   visible region. */
#define GBA_PRESENT_SRC_W   240
#define GBA_PRESENT_SRC_H   160

/* 1920/240 = 8 exactly, 1080/160 = 6.75 -> 6. min(8, 6) = 6. */
#define GBA_PRESENT_SCALE   6

#define GBA_PRESENT_DST_W   (GBA_PRESENT_SRC_W * GBA_PRESENT_SCALE)  /* 1440 */
#define GBA_PRESENT_DST_H   (GBA_PRESENT_SRC_H * GBA_PRESENT_SCALE)  /*  960 */

/* Centred. (1920-1440)/2 = 240 left/right, (1080-960)/2 = 60 top/bottom. */
#define GBA_PRESENT_OFF_X   ((SCR_W - GBA_PRESENT_DST_W) / 2)        /*  240 */
#define GBA_PRESENT_OFF_Y   ((SCR_H - GBA_PRESENT_DST_H) / 2)        /*   60 */

/* Opaque black, matching runtime/platform.c:304's own blanking colour exactly.
   Using a different "black" here would make the border check compare against a
   value the platform layer never writes. */
#define GBA_PRESENT_BORDER  0xFF000000u

/* The geometry constants state one fact several ways, so they are tied together
   at compile time and cannot drift apart in a later edit. */
_Static_assert(GBA_PRESENT_DST_W == 1440, "scaled width must be 1440");
_Static_assert(GBA_PRESENT_DST_H == 960,  "scaled height must be 960");
_Static_assert(GBA_PRESENT_OFF_X == 240,  "horizontal centring drifted");
_Static_assert(GBA_PRESENT_OFF_Y == 60,   "vertical centring drifted");
/* CENTRED, SO THE BORDER IS COUNTED TWICE -- ONCE ON EACH SIDE.
   The content rect does NOT run to the screen edge: OFF_X+DST_W = 1680, which
   is 240 short of 1920, and that shortfall is the RIGHT border. The earlier
   form of these two asserts was `OFF_X + DST_W == SCR_W`, which silently
   assumed the rect was flush to the right/bottom edge -- i.e. that only a
   left/top border existed. That contradicts both the centring above and
   apps/m7gpsp/main.c:1270, whose right-border walk would be an empty loop if
   it were true. The real invariant is that the two equal borders plus the
   content account for the whole axis. */
_Static_assert(GBA_PRESENT_OFF_X * 2 + GBA_PRESENT_DST_W == SCR_W,
               "left border + content + right border must span the screen width");
_Static_assert(GBA_PRESENT_OFF_Y * 2 + GBA_PRESENT_DST_H == SCR_H,
               "top border + content + bottom border must span the screen height");
/* The rect must also STAY on the surface -- weaker than the two above, stated
   separately because it is the property the bounds proof in gba_present.c
   actually depends on. */
_Static_assert(GBA_PRESENT_OFF_X + GBA_PRESENT_DST_W <= SCR_W,
               "content rect overruns the right screen edge");
_Static_assert(GBA_PRESENT_OFF_Y + GBA_PRESENT_DST_H <= SCR_H,
               "content rect overruns the bottom screen edge");
_Static_assert(GBA_PRESENT_SCALE * GBA_PRESENT_SCALE == 36,
               "each source pixel must occupy exactly 36 destination pixels");
/* 1920/240 = 8 and 1080/160 = 6, so 6 is min(8,6) and 7 would not fit. */
_Static_assert(GBA_PRESENT_DST_H + GBA_PRESENT_SRC_H > SCR_H,
               "SCALE is not maximal -- SCALE+1 would still fit vertically");

/* ---- the known-value self-test ------------------------------------------
 *
 * Every value this cartridge can produce lands on FULLY SATURATED channels,
 * which makes the table below unusually strong evidence: any conversion defect
 * -- a swapped channel, a mis-shifted field, a plain <<3 expansion -- produces a
 * NON-saturated channel and is caught here rather than on a photograph.
 *
 * KV_BLUE and KV_BIT5 are not reachable from this cartridge and are tested
 * anyway, because they are the two discriminators that matter most:
 *   KV_BLUE  distinguishes 0xAARRGGBB from 0xAABBGGRR. Without it a red/blue
 *            swap passes every other row in the table (black, white and the
 *            mid-green are all symmetric under it).
 *   KV_BIT5  proves green is read from bit 6 and not bit 5. Source 0x0020 sets
 *            ONLY bit 5, which convert_palette() can never produce, so it must
 *            convert to pure black. A six-bit reading would return green 4.
 */
#define GBA_PRESENT_KV_BLACK  (1u << 0)   /* 0x0000 -> 0xFF000000  backdrop   */
#define GBA_PRESENT_KV_GREEN  (1u << 1)   /* 0x07C0 -> 0xFF00FF00  palette    */
#define GBA_PRESENT_KV_WHITE  (1u << 2)   /* 0xFFDF -> 0xFFFFFFFF  palette    */
#define GBA_PRESENT_KV_RED    (1u << 3)   /* 0xF800 -> 0xFFFF0000  palette    */
#define GBA_PRESENT_KV_BLUE   (1u << 4)   /* 0x001F -> 0xFF0000FF  R/B swap   */
#define GBA_PRESENT_KV_MIDG   (1u << 5)   /* 0x0400 -> 0xFF008400  expansion  */
#define GBA_PRESENT_KV_BIT5   (1u << 6)   /* 0x0020 -> 0xFF000000  green LSB  */
#define GBA_PRESENT_KV_BLANK  (1u << 7)   /* 0xFFFF -> 0xFFFFFFFF  see below  */

/* One source pixel to one PS5 pixel. Pure function: no state, no allocation, no
   I/O, and safe to call before video is up -- which is exactly what the
   self-test does.

   NOTE ON 0xFFFF. Forced-blank residue (0xFFFF, written by video.cc:2369's raw
   memset, which bypasses convert_palette) and rendered white (0xFFDF) BOTH
   convert to 0xFFFFFFFF. The discriminator does not survive conversion, so the
   forced-blank assertion is only meaningful on the SOURCE u16 buffer and must
   never be re-derived from the ARGB output. GBA_PRESENT_KV_BLANK asserts that
   collision explicitly rather than leaving it to be rediscovered. */
u32 gba_present_argb(u16 px);

/* Converts and 6x-scales the whole 240x160 visible frame into a 1920x1080
   ARGB8888 destination whose pitch is SCR_W pixels.

   WRITES ONLY THE CONTENT RECT. The borders are NOT painted here -- the caller
   is expected to have blanked the whole surface first (plat_video_fill), which
   is both cheaper and keeps this function's write set exactly equal to the rect
   its bounds proof covers.

   WRITE-ONLY WITH RESPECT TO `dst`. Since M9-E the destination is only ever
   assigned to and is never read back: each source row is converted once into a
   private 5,760-byte static staging row (see gba_present.c) and that row is
   then stored to all six rows of the band. Per frame: 38,400 conversions,
   1,382,400 framebuffer stores to exactly the same addresses as before, and
   ZERO framebuffer loads, down from 1,152,000.

   NOT REENTRANT, AND NOT THREAD-SAFE. The staging row is a single static buffer
   shared by every call, so two overlapping calls -- on two threads, or from a
   signal handler -- would interleave into it and tear the output. One blit at a
   time. Every caller in the tree drives it from a single fixture main loop, and
   the pure conversion helper gba_present_argb() below remains stateless and
   safe to call from anywhere.

   `src` is const and is never written. `dst` must be non-NULL and must be a real
   SCR_W x SCR_H surface; a NULL is ignored rather than faulted, because a null
   framebuffer means "write nowhere" (runtime/platform.h:73-76). */
void gba_present_blit(u32 *dst, const u16 *src);

/* Runs the known-value table. Returns 0 when every conversion is exact, or a
   mask of GBA_PRESENT_KV_* bits. Costs eight function calls and touches no
   hardware, so it runs BEFORE the display is taken over -- a conversion defect
   must never be discovered after the host game has already been killed. */
unsigned gba_present_selftest(void);

#endif
