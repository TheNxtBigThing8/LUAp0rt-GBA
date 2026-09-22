/* LUAport M7 -- adapters/gba/gba_present.c
 *
 * The GBA -> PS5 presentation adapter. See gba_present.h for the full
 * derivation of the source format, the destination format, the five-bit green
 * and the bit-replication expansion; this file is the implementation only.
 *
 * NO PS5 API IS CALLED FROM THIS FILE, and none may ever be. It converts pixels
 * into a buffer it is handed. Display ownership, direct memory and flips are
 * runtime/platform.c's business, and keeping the two apart is what lets
 * `make m7-verify` prove that every sceVideoOut* string in the M7 image comes
 * from the one translation unit that is supposed to contain them.
 *
 * THIS FILE INCLUDES NO gpSP HEADER, which is why it is compiled with M7FLAGS
 * rather than M7_GPSPFLAGS: gpsp/common.h:100 typedefs u64 as
 * `unsigned long long` and runtime/core.h:30 uses `unsigned long`, so a TU that
 * pulled in both would fail on the duplicate typedef. The geometry constants
 * are DERIVED FROM gpSP's source and RESTATED here rather than imported, which
 * is the same type-boundary discipline every adapter since M1 has followed.
 */

#include "gba_present.h"

/* LuaPSX/src/ui.c:142, unchanged.
 *
 * Bit replication, not a shift. 31 -> (31<<3)|(31>>2) = 248|7 = 255. A plain
 * <<3 would map 31 to 248 and leave every white pixel visibly grey -- the
 * reference records that exact symptom at ui.c:140-141. */
static inline u32 x5to8(u32 v) {
    return (v << 3) | (v >> 2);
}

u32 gba_present_argb(u16 px) {
    u32 p  = (u32)px;
    u32 r5 = (p >> 11) & 0x1Fu;
    u32 g5 = (p >>  6) & 0x1Fu;   /* FIVE bits from bit 6. Bit 5 is discarded */
    u32 b5 =  p        & 0x1Fu;

    return 0xFF000000u
         | (x5to8(r5) << 16)
         | (x5to8(g5) <<  8)
         |  x5to8(b5);
}

/* ------------------------------------------------------- the staging row --
 *
 * M9-E. ONE SCALED DESTINATION ROW, IN ORDINARY CACHED MEMORY.
 *
 * This is the entire cost of making the blit write-only, and it is deliberate
 * rather than incidental, so it is stated here in full.
 *
 * The framebuffer handed to gba_present_blit() is PS5 direct memory of type 3
 * (WC_GARLIC). That memory is optimised for GPU reads and for write-combined
 * CPU WRITES; a CPU READ of it is uncached, uncombined and serialising. The
 * M9-D diagnostic measured both directions on real hardware and the asymmetry
 * is not marginal:
 *
 *     P2 framebuffer read      ~24-25 MB/s
 *     P2 .bss read             ~8,594 MB/s        ratio ~347.9x
 *     P3 framebuffer write     ~5,890 MB/s
 *
 * So the mapping is not the defect. WRITES ARE ALREADY FAST. Only the read
 * direction is pathological, and the previous form of this blit read 1,152,000
 * u32 back out of the framebuffer every frame purely to replicate rows it had
 * just written. M9-D attributed ~297,864 us of a ~298,091 us blit to exactly
 * that loop -- 99.9% of the whole presentation cost.
 *
 * The fix is to build the scaled row ONCE in cached memory and then write it
 * out six times. The destination is then only ever assigned to, never read.
 *
 * WHY A CACHED STAGING ROW AND NOT A DIRECT 6-STREAM EMIT. The obvious
 * alternative -- convert each pixel and immediately store all 36 of its
 * destination pixels -- also performs no framebuffer reads, and needs no
 * scratch at all. It was rejected on the hardware evidence: it writes SIX
 * concurrent streams 7,680 bytes apart, 24 bytes per stream per pixel. The
 * write-combining buffers are few and are evicted when the stream switches, so
 * six interleaved partial-line streams risk collapsing write-combining into
 * partial-line uncached writes and forfeiting the 5,890 MB/s above. The form
 * below writes each destination row FULLY SEQUENTIALLY, 5,760 bytes at a time,
 * one stream at a time -- which is precisely the access pattern M9-D's P1
 * probe measured at ~704 us for five rows. Its cost is measured, not modelled.
 *
 * 1440 u32 = 5,760 bytes of .bss, against the 2 MB tripwire in
 * tools/check_forbidden.py which M9 was using 1,415,200 bytes of. The size is
 * pinned below so it cannot drift with the geometry.
 *
 * THIS MAKES gba_present_blit() NON-REENTRANT. It is a single static buffer
 * shared by every call, so two concurrent blits would interleave into it and
 * tear. Every caller in the tree (M7, M8, M9, M9-D) blits from its own single
 * fixture main loop, and no threading is introduced by M9-E, so the constraint
 * is satisfied today -- but it is a REAL new constraint and is documented in
 * the header rather than left to be discovered. */
static u32 gba_present_row[GBA_PRESENT_DST_W];

_Static_assert(sizeof(gba_present_row) == 5760,
               "the staging row must be exactly 1440 u32 = 5760 bytes");

/* ---------------------------------------------------------------- the blit --
 *
 * CONVERT ONCE PER SOURCE PIXEL, REPLICATE THE REST -- AND NEVER READ BACK.
 *
 * The naive form calls gba_present_argb() once per DESTINATION pixel: 1,382,400
 * conversions instead of 38,400, i.e. thirty-six times the work for an
 * identical result. Here each source row is converted ONCE into the cached
 * staging row above, and that row is then stored to all six rows of the
 * destination band. Every destination pixel in a band is therefore, by
 * construction, bit-identical to its neighbours -- which is the property
 * apps/m7gpsp/main.c's 6x6 block check verifies independently rather than
 * assumes.
 *
 * PER-FRAME BUDGET, UNCHANGED WHERE IT MATTERS AND ZEROED WHERE IT HURT:
 *
 *   conversions            38,400        (unchanged)
 *   framebuffer stores  1,382,400        (unchanged, and to the SAME addresses)
 *   framebuffer loads           0        (was 1,152,000)
 *   cached staging stores 230,400        (new, ordinary write-back memory)
 *
 * The write SET is identical to the previous implementation's, which is why
 * the bounds proof below carries over verbatim and why the output is expected
 * to be byte-for-byte what M7 already validated.
 *
 * BOUNDS, COMPUTED RATHER THAN TRUSTED (and re-asserted at runtime by the
 * fixture, which walks the whole surface):
 *
 *   first pixel  src (0,0)     -> dst (240, 60)   .. (245, 65)     index 115440
 *   last pixel   src (239,159) -> dst (1674,1014) .. (1679,1019)   index 1958159
 *
 *   1,958,159 < SCR_W*SCR_H = 2,073,600, so no write can leave the surface.
 *   240 + 1440 = 1680 <= 1920 and 60 + 960 = 1020 <= 1080.
 *
 * The scratch row (source row 160) is NEVER READ: the loop bound is SRC_H = 160
 * and the source index never exceeds 159*240+239 = 38,399. */
void gba_present_blit(u32 *dst, const u16 *src) {
    unsigned y, x, j;

    /* A null framebuffer means "write nowhere" (runtime/platform.h:73-76), so
       it is ignored rather than faulted. The caller is separately required to
       treat a null as a hard failure; doing it in both places means a missed
       check in one cannot corrupt memory through the other. */
    if (!dst || !src) return;

    for (y = 0; y < GBA_PRESENT_SRC_H; y++) {
        const u16 *srow = src + (unsigned)y * GBA_PRESENT_SRC_W;

        /* Top row of this source pixel's 6-row destination band. */
        u32 *drow = dst
                  + ((unsigned)GBA_PRESENT_OFF_Y + y * GBA_PRESENT_SCALE)
                        * (unsigned)SCR_W
                  + (unsigned)GBA_PRESENT_OFF_X;

        /* Convert each of the 240 source pixels EXACTLY ONCE and expand it
           horizontally into the cached staging row. 240 * 6 = 1440 stores, all
           of them to ordinary write-back memory -- the framebuffer is not
           touched at all by this loop. */
        for (x = 0; x < GBA_PRESENT_SRC_W; x++) {
            u32  c = gba_present_argb(srow[x]);
            u32 *p = gba_present_row + x * GBA_PRESENT_SCALE;

            /* Unrolled to exactly GBA_PRESENT_SCALE stores. The static assert
               in the header pins SCALE at 6, so this cannot silently fall out
               of step with the geometry. */
            p[0] = c; p[1] = c; p[2] = c;
            p[3] = c; p[4] = c; p[5] = c;
        }

        /* Store the finished row to ALL SIX destination rows. j starts at 0,
           not 1: the first row is no longer a special case that the other five
           are recovered from, so there is nothing to read back. Each pass is a
           single sequential 5,760-byte write-combined run, and every load in
           it comes from gba_present_row (cached), never from d (framebuffer). */
        for (j = 0; j < GBA_PRESENT_SCALE; j++) {
            u32 *d = drow + j * (unsigned)SCR_W;
            for (x = 0; x < GBA_PRESENT_DST_W; x++)
                d[x] = gba_present_row[x];
        }
    }
}

/* ------------------------------------------------------------- self-test --
 *
 * RUN BEFORE THE DISPLAY IS TAKEN OVER. plat_video_init() cancels the host
 * game's GS thread irreversibly, so a conversion defect must be caught while
 * the host game is still on screen and a clean refusal is still possible.
 *
 * Each row is computed by hand in gba_present.h's comment block; none of the
 * expected values below is copied from a run of this code. */
unsigned gba_present_selftest(void) {
    unsigned f = 0;

    /* The four values this cartridge can actually produce. */
    if (gba_present_argb(0x0000u) != 0xFF000000u) f |= GBA_PRESENT_KV_BLACK;
    if (gba_present_argb(0x07C0u) != 0xFF00FF00u) f |= GBA_PRESENT_KV_GREEN;
    if (gba_present_argb(0xFFDFu) != 0xFFFFFFFFu) f |= GBA_PRESENT_KV_WHITE;
    if (gba_present_argb(0xF800u) != 0xFFFF0000u) f |= GBA_PRESENT_KV_RED;

    /* Channel order. Without this a red/blue swap passes every other row. */
    if (gba_present_argb(0x001Fu) != 0xFF0000FFu) f |= GBA_PRESENT_KV_BLUE;

    /* The expansion. g5 = 16 -> (16<<3)|(16>>2) = 128|4 = 132 = 0x84. A plain
       <<3 would give 128 = 0x80 and pass every saturated row above. */
    if (gba_present_argb(0x0400u) != 0xFF008400u) f |= GBA_PRESENT_KV_MIDG;

    /* Green's LSB. Only bit 5 is set, which convert_palette() can never
       produce, so this must be black. A six-bit green reading returns 4. */
    if (gba_present_argb(0x0020u) != 0xFF000000u) f |= GBA_PRESENT_KV_BIT5;

    /* Forced blank and rendered white COLLIDE after conversion. Asserted, not
       merely noted, so the fixture can never be rewritten to look for a
       0xFFFF marker in the ARGB output. */
    if (gba_present_argb(0xFFFFu) != 0xFFFFFFFFu) f |= GBA_PRESENT_KV_BLANK;

    return f;
}
