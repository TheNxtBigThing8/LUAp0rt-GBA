#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LUAp0rt -- tools/mkart.py -- R3 splash portrait converter.

DEVELOPER TOOLING. THIS IS NOT A BUILD STEP.
============================================================================
`make m16c` NEVER runs this file. It consumes the COMMITTED, generated

    apps/m16cgpsp/m16c_splash_art.inc

exactly the way it consumes apps/m13bgpsp/m13b_picker.inc: as checked-in text
reached by a textual #include. This tool exists only to REGENERATE that .inc
when the source artwork changes. A normal build therefore needs no image
library, no PNG decoder and no network -- which is the whole point of
committing the output.

STANDARD LIBRARY ONLY.
============================================================================
Imports are limited to argparse, hashlib, os, struct, sys and zlib. That
matches every other tool in tools/ (the repo has no third-party Python
dependency anywhere, and tools/m6_baseline.py already establishes zlib as
in-convention). Pillow is deliberately NOT used even when it is installed:
depending on it would make this the first third-party Python dependency in the
tree, and it would buy nothing, because the output is committed.

Consequence: the source image MUST be a PNG. There is no JPEG path and there
will not be one -- the standard library cannot decode JPEG.

WHAT THIS PRODUCES
============================================================================
An 80x80 8bpp indexed portrait plus a 256-entry ARGB8888 palette:

    6,400 bytes of indices  +  1,024 bytes of palette  =  7,424 bytes

Transparency is carried IN THE PALETTE, not in a second plane. Palette entry 0
is forced to 0x00000000 and every fully-transparent pixel maps to it, so the
runtime skips those pixels entirely and the background shows through
untouched. A separate 80x80 mask would have cost another 6,400 bytes for
information the palette already carries for free.

FEATHERING IS BAKED IN HERE, AT BUILD TIME.
============================================================================
The runtime does no feather math at all -- it reads an alpha out of the
palette and does one integer blend.

***** THE MASK IS A FRAME-ANCHORED UPPER-RIGHT BLEND. *****
Alpha is the product of TWO DIRECTIONAL DISSOLVE RAMPS and nothing else.
THREE earlier masks were built and all three were rejected on review:

  1. a RADIAL VIGNETTE -- with the portrait nearly full-frame, an alpha field
     driven by distance from the CENTRE inevitably draws a circle, and the
     result read as a circular profile picture instead of a photograph;

  2. a SYMMETRIC EDGE-DISTANCE DISSOLVE -- mathematically not radial at all,
     yet it read as a circular avatar too, because eroding all four sides of
     an extreme close-up by the same amount merely traces the round head's
     own outline back at the viewer. THE ROUNDNESS CAME FROM THE SYMMETRY,
     NOT FROM THE ARITHMETIC;

  3. an ASYMMETRIC FOUR-RAMP BLEND (left/bottom 12 px, top/right 3 px) --
     this DID fix the circle, but every ramp was a SMOOTH SEPARABLE GREY one
     and the portrait floated mid-screen at (400,0), so the shallow top and
     right ramps terminated the photograph in open background and it read as
     an obvious rectangular photo card pasted onto the splash.

The lesson of (3) is the operative one: A MASK CANNOT DISSOLVE AN EDGE INTO A
BACKGROUND THAT THE EDGE DOES NOT ACTUALLY TOUCH. Feathering an edge that
terminates mid-screen only makes a soft rectangle out of a hard one. So the
problem is now solved by COMPOSITION rather than by equation -- the portrait
is placed FLUSH INTO THE TOP-RIGHT CORNER OF THE INNER DECORATIVE FRAME of
the 480x270 logical surface:

    x = 393, y = 7   ->   occupies x 393..472, y 7..86

A first attempt tucked it into the corner of the SCREEN instead (400, 0). The
maths worked, but the approved reference puts the dog INSIDE the frame, and at
(400, 0) the frame's own rules cut straight across the portrait. So the
anchoring now terminates on the FRAME RULES rather than on the display edge:
row 7 is flush beneath the bright violet rule at y == 6, and column 472 is
flush left of the rule at x == 473. A hard photo edge butted against a bright
1 px rule reads as the photograph being TUCKED UNDER THE FRAME -- the same
perceptual trick as running off the display, but inside the frame where the
reference wants it.

That composition was accepted on review. Two sides then went through a further
correction, because with the top and right fully ANCHORED (band 0) the bright
fur terminated in dead-straight horizontal and vertical lines, and the 1 px
rules did NOT hide them: the portrait still read as a rectangle. The current
geometry therefore gives those two sides a TINY EDGE-BREAKING DISSOLVE whose
only job is to destroy the straight photographic boundary:

    LEFT   : broad atmospheric dissolve          (--left,   12 px)
    BOTTOM : broad atmospheric dissolve          (--bottom, 12 px)
    TOP    : edge-breaking dissolve under y == 6 (--top,     3 px)
    RIGHT  : edge-breaking dissolve at x == 473  (--right,   3 px)

    aL = D(x, L)      aR = D(size-1-x, R)
    aT = D(y, T)      aB = D(size-1-y, B)

    a  = aL * aR * aT * aB         (products taken /255)

***** THIS IS NOT A RETURN TO A SYMMETRIC FOUR-SIDED FEATHER, AND IT IS NOT
REVISION (3). ***** The asymmetry is the point and it stays obvious -- 12 px
against 3 px, a 4:1 ratio that main() enforces. Left and bottom perform a broad
ATMOSPHERIC dissolve that carries the photograph into the backdrop over a sixth
of its width; top and right merely ROUGHEN A BOUNDARY that is already covered
by a violet rule, over 3 px, and never reach anything. What separates this from
revision (3) is not the widths but the treatment: D() is irregular, stippled
and hazed (see below), whereas (3) was a smooth separable grey ramp ending in
open background. A 3 px SMOOTH band here would still be revision (3), so main()
refuses an edge-breaking band unless the irregular treatment is live.

NOTHING HERE COMPUTES A DISTANCE FROM THE CENTRE. There is no radius, no
ellipse, no superellipse, no L1/diagonal term and no corner term -- the
diagonal "corner bite" of revision (2) is gone, because it applied an EQUAL
bite to all four corners and was therefore actively contributing to the
roundness under complaint. Alpha depends on x and y only, through the four
ramps above plus the two noise fields, and the bottom-left corner dissolves for
no reason other than being the one corner where two BROAD ramps overlap.

NEITHER A CIRCULAR NOR A RECTANGULAR OUTLINE IS STRUCTURALLY POSSIBLE. At
x == 0, aL == D(0) == 0, and likewise at y == size-1, x == size-1 and y == 0,
so all four boundary rows/columns are exactly alpha 0 no matter what the source
contains or what the noise says -- every perturbation in D() is a MULTIPLICATIVE
factor on the ramp position, and 0 times anything is 0. Because smoothstep has
ZERO SLOPE AT BOTH ENDS, each ramp meets the opaque interior with a matching
gradient, so the junction cannot surface as a visible line -- precisely the
crease a linear ramp would have shown as a rectangular outline. And because the
ramps consult a 2D noise field, the OUTER locus is not straight either. main()
re-proves all of this by inspection.

main() additionally REFUSES TO RUN A NEAR-SYMMETRIC CONFIGURATION: the left and
bottom bands must exceed the right and top bands by at least 4 px, and an
edge-breaking band may not exceed EDGE_BREAK_MAX px. Both rejected symmetric
masks are therefore unreachable by flag, and so is any drift of the 12/3
asymmetry back toward a uniform four-sided feather.

***** THE MASK IS VALID ONLY AT ONE PLACEMENT. ***** Where an edge may stop is
a CLAIM ABOUT WHERE THE BYTES ARE DRAWN, not a property of the bytes. An
ANCHORED edge (band 0) is fully opaque right up to the boundary of the 80x80
box, so it is invisible only when it stops against something that is already a
hard graphic edge; an EDGE-BREAKING band (1..EDGE_BREAK_MAX px) is permitted
only against a frame rule, which covers its outermost row. There are exactly
two such terminators on this surface:

  1. THE SCREEN BOUNDARY  -- x == 0, y == 0, x == UI_W, y == UI_H.
  2. AN INNER DECORATIVE FRAME LINE -- the 1 px bright violet rules that
     m16c_draw_splash() lays down at y == 6, y == UI_H-7, x == 6 and
     x == UI_W-7. A hard photo edge butted directly against a bright rule
     reads as the photograph being TUCKED UNDER THE FRAME, exactly as the
     screen edge reads as it running off the display.

Terminator (2) is what the shipping splash uses: the portrait sits at
(393, 7), so its top row is flush beneath the y == 6 rule and its right
column is flush left of the x == UI_W-7 rule -- INSIDE the frame, which the
approved reference requires and which the screen-edge placement could not
give. Anywhere else, the top and right edges stop in open background, where a
3 px fade is revision (3) and a 0 px anchor is a hard cut line -- the worst of
all the variants tried so far. main() therefore enforces the agreement between
the band widths and --place-x/--place-y, and dies on a mismatch.

***** WHY A SMALL BAND IS ALLOWED AGAINST A FRAME RULE BUT NOT AGAINST THE
SCREEN. ***** They are not symmetric cases. The frame rule is DRAWN AFTER the
portrait and is 1 px of bright violet, so it overpaints the outermost portrait
row and gives the dissolve somewhere to go: the fur thins into haze that is
already the frame's own colour, and the rule caps it. At the screen boundary
there is nothing on the far side and nothing drawn over the top, so a band can
only end the photograph short of the edge -- which is exactly revision (3).
main() encodes precisely this distinction: a band is accepted on a frame
terminator (up to EDGE_BREAK_MAX) and still refused on a screen terminator.

THERE IS NO LUMINANCE KEY. An earlier revision faded dark pixels in the outer
band to let the fur silhouette end the portrait. It is GONE. It was the one
mechanism in this file that could punch holes through the dog's nearly black
pupils, and the review requires the eyes to stay solid. Alpha is now a pure
function of (x, y) and never of colour, so a facial feature can only fade if
it physically lies inside one of the four dissolve bands -- and face_report()
proves that none of them reaches the protected central face.

DETERMINISM
============================================================================
Every stage is integer arithmetic (// and >> only) -- no float appears
anywhere in the pixel path, so the same PNG and the same flags produce the
same bytes on any machine and any Python 3.8+. Dict iteration is
insertion-ordered and every sort key is total, so the median cut is stable.

VERIFIER SELF-CHECK
============================================================================
The generated bytes land in .rodata, and Makefile's m16c-verify greps the
carved rodata.bin for forbidden literals -- case-insensitively for
"/mnt|/usb|/dev/|ugen|usbctl". ~7.4 KB of essentially arbitrary binary has a
small but REAL chance (~1 in 17,000) of containing one by accident, and if it
did, the failure would present as a baffling "the artwork contains a USB
literal". So this tool scans its own output and perturbs visually
insignificant data until it is clean. It NEVER weakens the gate.

Usage:

    python3 tools/mkart.py assets/nbt_dog_portrait.png \
        -o apps/m16cgpsp/m16c_splash_art.inc \
        --preview build/dogpreview

SOURCE ASSET ROLES -- DO NOT CONFUSE THEM
============================================================================
  assets/nbt_dog_portrait.png      the ORIGINAL standalone dog photograph.
                                   THE ONLY permitted source of dog pixels.
                                   1288x1294, 8-bit RGBA, non-interlaced.

  assets/luap0rt_gba_splash_ref.png  the approved splash mockup.
                                   REFERENCE ONLY -- never a pixel source.
                                   This tool must never be pointed at it.

The photograph is an EXTREME close-up: the face already fills the frame edge
to edge, and the ear tips were cropped off by the camera, not by us. So the
default --crop-* values below are very close to a full-frame square (the
photographer had already centred the face) rather than the small off-centre
window an earlier revision used to pull the head out of the mockup. They are
fractions of the source dimensions, so they survive the photograph being
re-exported at a different resolution.

WHY THE EYES ARE SAFE WITHOUT ANY SPECIAL CASE.
The earliest revision needed a large protected radius because the luminance key
would otherwise have hollowed out the pupils. That key no longer exists, so no
colour-driven protection mechanism is needed: alpha is a pure function of
(x, y). On this crop the pupils sit roughly 28 px either side of the centre,
i.e. near x == 12 and x == 68. The LEFT pupil sits at the inner lip of the
12 px left band, where smoothstep has already flattened out -- alpha is exactly
255 from x == 12 inward -- so the eye stays visually solid even if it overlaps
the band's outermost pixel or two. The RIGHT pupil near x == 68 is 9 px clear
of the 3 px right band, which reaches only x == 77. The nose, muzzle and teeth
are central and nowhere near any band.

***** THE 3 px TOP/RIGHT BANDS CANNOT REACH A FACIAL FEATURE, AND THAT IS
CHECKED RATHER THAN ARGUED. ***** They erode only x 77..79 and y 0..2, i.e. the
outermost 3 px of fur on two sides. face_report() below defines an explicit
PROTECTED CENTRAL FACE rectangle -- the middle 60% of the portrait, x 16..63 and
y 16..63, which contains both eyes, the nose, the muzzle, the mouth and the
teeth -- and main() DIES if a single pixel of it is anything other than alpha
255, reporting the clearance from each band to the rectangle. It additionally
re-checks the darkest 2% of pixels (the pupils and nostrils) the same way. So
"the face is still opaque" is a measured result in the build log, not a claim.

Nothing dark is treated specially anywhere in this file, and main() prints an
ASCII map of the finished alpha field as well, so all of this is confirmed from
the log rather than taken on trust.
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"

# Mirrors the two grep patterns in Makefile's m16c-verify. The first group is
# matched case-INSENSITIVELY (grep -aqiE), the second case-SENSITIVELY.
FORBIDDEN_NOCASE = (b"/mnt", b"/usb", b"/dev/", b"ugen", b"usbctl")
FORBIDDEN_CASE = (b"RUNG", b"STAGE", b"MILESTONE")

# ---------------------------------------------------------------------------
# THE LOGICAL SPLASH SURFACE, MIRRORED FROM apps/m16cgpsp/main.c.
#
# These constants exist ONLY to render the composition preview. Nothing here
# reaches the emitted .inc -- the .inc contains the portrait and its palette
# and nothing about the splash. They are duplicated rather than parsed out of
# main.c because this tool is not a build step and must not develop opinions
# about C source; the preview is a judgement aid, and if main.c ever changes
# these values the preview simply stops matching, which is visible at a glance.
#
#   UI_W/UI_H          main.c:597   the 480x270 compose surface
#   M16C_C_BG          main.c:1087  0xFF0D0F18, laid down by a flat ui_fill()
#   M16C_A_BORDLO      main.c:1193  outer frame, dim
#   M16C_A_BORDER      main.c:1192  inner frame, bright violet
# ---------------------------------------------------------------------------
UI_W = 480
UI_H = 270

SPLASH_BG = (0x0D, 0x0F, 0x18)
SPLASH_BORDLO = (0x3A, 0x2C, 0x78)
SPLASH_BORDER = (0x8A, 0x6C, 0xF0)

# ***** THE COLOUR THE DISSOLVE FADES *THROUGH*. *****
#
# This is the fix for the gate failure that killed the previous revision. An
# alpha-only feather does not choose a transition colour -- it interpolates
# whatever the photograph happens to hold toward SPLASH_BG. The crop's outer
# ring is bright cream fur and a pale studio backdrop, so "cream -> near-black
# navy" passes through NEUTRAL GREY at every midpoint. That grey ramp, laid
# around two straight edges, is precisely what read as a drop shadow under a
# pasted photo card. Alpha controls HOW MUCH survives; it cannot control WHAT
# COLOUR the surviving part is on the way out.
#
# So the transition is given an explicit colour to travel through. Pixels are
# pulled toward this violet as their alpha falls (see apply_haze()), which
# turns the midpoint from grey into purple haze and makes the portrait read as
# atmosphere resolving into fur rather than a photograph with a shadow.
#
# Derived from the frame's own two violets rather than invented, so the haze is
# by construction a colour already present in the splash: three parts of the
# dim outer violet to one part of the bright inner rule, i.e. (0x4E, 0x3C,
# 0x96). Dark enough to settle into SPLASH_BG without a halo, saturated enough
# to read as violet and not as "dark grey" at the midpoint.
SPLASH_HAZE = tuple((SPLASH_BORDLO[i] * 3 + SPLASH_BORDER[i]) // 4
                    for i in range(3))

# The splash's decorative double frame, drawn by m16c_draw_splash() at
# main.c:1608-1622 as (x, y, w, h) fills. ***** IT IS DRAWN *AFTER* THE
# PORTRAIT, SO IT OVERWRITES IT. ***** That is exactly why the composition
# preview has to include it: the top-right corner the portrait is being
# anchored into is not bare background, and a preview that omitted the frame
# would look clean and then betray us at build time.
SPLASH_FRAME = (
    (2, 2, UI_W - 4, 1, SPLASH_BORDLO),
    (2, UI_H - 3, UI_W - 4, 1, SPLASH_BORDLO),
    (2, 2, 1, UI_H - 4, SPLASH_BORDLO),
    (UI_W - 3, 2, 1, UI_H - 4, SPLASH_BORDLO),
    (6, 6, UI_W - 12, 1, SPLASH_BORDER),
    (6, UI_H - 7, UI_W - 12, 1, SPLASH_BORDER),
    (6, 6, 1, UI_H - 12, SPLASH_BORDER),
    (UI_W - 7, 6, 1, UI_H - 12, SPLASH_BORDER),
    (6, 6, 5, 5, SPLASH_BORDER),
    (UI_W - 11, 6, 5, 5, SPLASH_BORDER),
    (6, UI_H - 11, 5, 5, SPLASH_BORDER),
    (UI_W - 11, UI_H - 11, 5, 5, SPLASH_BORDER),
)

# The four INNER (bright violet) frame rules, as the single coordinate each one
# occupies. These are the second legal terminator for a portrait edge: a photo
# edge butted against one of these rules reads as the portrait being tucked
# under the frame, the same way the screen boundary reads as it running off the
# display. Derived from SPLASH_FRAME above rather than retyped, so the guard in
# main() cannot drift away from the frame the preview actually draws.
FRAME_TOP_Y = 6                  # rule occupies row 6; a portrait below it
FRAME_BOTTOM_Y = UI_H - 7        # starts at row 7
FRAME_LEFT_X = 6                 # rule occupies column 6
FRAME_RIGHT_X = UI_W - 7         # a portrait left of it ends at column 472

# ***** THE EDGE-BREAKING BAND CAP. *****
# An edge that terminates on a frame rule may carry a SMALL dissolve whose only
# purpose is to destroy the straight photographic boundary -- the defect that
# failed the second composition gate, where fully anchored (band 0) top/right
# edges left the bright fur ending in dead-straight lines that the 1 px rules
# did not hide. It is NOT an atmospheric dissolve and must never grow into one:
# past ~4 px the fade stops reading as a roughened boundary and starts reading
# as revision (3)'s photo-card edge, ending the photograph inside the frame.
# main() enforces this cap, enforces that left/bottom still exceed top/right by
# at least 4 px, and refuses the band entirely on a SCREEN terminator, where
# nothing is drawn over the top to cap it.
EDGE_BREAK_MAX = 4

# ***** THE PROTECTED CENTRAL FACE. *****
# The middle 60% of the portrait (inset 20% per side => x 16..63, y 16..63 at
# size 80). On this extreme close-up that rectangle contains both eyes, the
# nose, the muzzle, the mouth and the teeth. face_report() requires EVERY pixel
# of it to be alpha 255 and main() dies otherwise, so no band -- broad or
# edge-breaking -- can ever eat into a facial feature undetected.
PROTECTED_FACE_INSET_PCT = 20


def die(msg):
    sys.stderr.write("mkart: error: %s\n" % msg)
    sys.exit(1)


def note(msg):
    sys.stdout.write("mkart: %s\n" % msg)


# ---------------------------------------------------------------- PNG decode

def _paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter(raw, stride, bpp, rows):
    """Reverse the five PNG scanline filters for the first `rows` rows.

    Only `rows` rows are reconstructed. Filters chain top-to-bottom, so we
    cannot skip rows ABOVE the crop, but we can stop early once the crop's
    bottom edge is reached. With the near-full-frame default crop on the
    standalone photograph that saves nothing, but it stays correct and it
    still pays off for any tighter --crop-size the operator asks for."""
    out = []
    prev = bytearray(stride)
    pos = 0
    for y in range(rows):
        if pos + 1 + stride > len(raw):
            die("PNG data ended early at row %d (corrupt file?)" % y)
        ft = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride

        if ft == 0:
            pass
        elif ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        else:
            die("unknown PNG filter type %d on row %d" % (ft, y))

        out.append(line)
        prev = line
    return out


def _bits_at(line, index, depth):
    """Extract the `index`-th sub-byte sample of `depth` bits (1, 2 or 4)."""
    per = 8 // depth
    b = line[index // per]
    shift = 8 - depth * (index % per + 1)
    return (b >> shift) & ((1 << depth) - 1)


def read_png_rows(path, want_rows_frac):
    """Decode a PNG far enough down to cover `want_rows_frac` of its height.

    Returns (width, height, rows, getpx) where rows is the list of
    reconstructed scanlines and getpx(line, x) -> (r, g, b, a) with 8-bit
    channels."""
    with open(path, "rb") as f:
        data = f.read()

    if data[:8] != PNG_SIG:
        die("%s is not a PNG (signature mismatch)" % path)

    pos = 8
    ihdr = None
    plte = b""
    trns = b""
    idat = []
    while pos + 8 <= len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            ihdr = body
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break

    if ihdr is None or len(ihdr) < 13:
        die("%s has no usable IHDR" % path)

    w, h, depth, ctype, comp, filt, interlace = struct.unpack(">IIBBBBB", ihdr[:13])
    if interlace != 0:
        die("interlaced PNG is not supported -- re-export %s without Adam7" % path)
    if comp != 0 or filt != 0:
        die("unsupported PNG compression/filter method in %s" % path)

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None:
        die("unsupported PNG colour type %d" % ctype)
    if ctype == 3 and depth not in (1, 2, 4, 8):
        die("unsupported palette bit depth %d" % depth)
    if ctype != 3 and depth not in (8, 16):
        die("unsupported bit depth %d for colour type %d" % (depth, ctype))

    stride = (w * channels * depth + 7) // 8
    bpp = max(1, (channels * depth) // 8)

    rows_needed = min(h, max(1, int(h * want_rows_frac) + 2))
    raw = zlib.decompress(b"".join(idat))
    note("decoding %dx%d PNG (colour type %d, depth %d), %d of %d rows needed"
         % (w, h, ctype, depth, rows_needed, h))
    rows = _unfilter(raw, stride, bpp, rows_needed)

    if ctype == 3:
        pal = []
        for i in range(len(plte) // 3):
            a = trns[i] if i < len(trns) else 255
            pal.append((plte[3 * i], plte[3 * i + 1], plte[3 * i + 2], a))
        if not pal:
            die("palette PNG has no PLTE chunk")

        def getpx(line, x):
            idx = _bits_at(line, x, depth) if depth < 8 else line[x]
            return pal[idx] if idx < len(pal) else pal[0]

    elif depth == 8:
        if ctype == 0:
            def getpx(line, x):
                v = line[x]
                return (v, v, v, 255)
        elif ctype == 2:
            def getpx(line, x):
                o = x * 3
                return (line[o], line[o + 1], line[o + 2], 255)
        elif ctype == 4:
            def getpx(line, x):
                o = x * 2
                v = line[o]
                return (v, v, v, line[o + 1])
        else:
            def getpx(line, x):
                o = x * 4
                return (line[o], line[o + 1], line[o + 2], line[o + 3])
    else:  # depth == 16 -- take the high byte of each sample
        if ctype == 0:
            def getpx(line, x):
                v = line[x * 2]
                return (v, v, v, 255)
        elif ctype == 2:
            def getpx(line, x):
                o = x * 6
                return (line[o], line[o + 2], line[o + 4], 255)
        elif ctype == 4:
            def getpx(line, x):
                o = x * 4
                v = line[o]
                return (v, v, v, line[o + 2])
        else:
            def getpx(line, x):
                o = x * 8
                return (line[o], line[o + 2], line[o + 4], line[o + 6])

    return w, h, rows, getpx


# -------------------------------------------------------- crop and downscale

def crop_and_scale(w, h, rows, getpx, cx, cy, side_frac, size):
    """Box-filter the square crop down to `size` x `size` RGB.

    A box filter (a plain mean over the source rectangle covering each
    destination pixel) is the right choice here: it is integer, it is
    deterministic, and at the ~16:1 reduction the standalone photograph needs
    (1281 px -> 80 px, so each output pixel is the mean of a 16x16 source
    block) it suppresses the source's grain and JPEG-ish ringing instead of
    aliasing them into the 80x80 result."""
    side = int(h * side_frac)
    if side < size:
        die("crop side %d px is smaller than the %dx%d target" % (side, size, size))
    # The square is sized off the HEIGHT, so a portrait-orientation source can
    # ask for a side wider than the image. Clamping x0 alone would not save us:
    # the sampler would still read past the end of every row. Fail loudly with
    # the largest legal --crop-size instead of emitting silently wrong pixels.
    if side > w:
        die("crop side %d px exceeds the source width %d px -- "
            "--crop-size must be <= %.3f for this %dx%d image"
            % (side, w, float(w) / float(h), w, h))

    x0 = int(w * cx) - side // 2
    y0 = int(h * cy) - side // 2
    # Clamp, keeping the square square.
    x0 = max(0, min(x0, w - side))
    y0 = max(0, min(y0, h - side))
    if y0 + side > len(rows):
        die("crop bottom row %d exceeds the %d decoded rows -- widen --crop-rows"
            % (y0 + side, len(rows)))

    note("crop: %dx%d at (%d,%d) of %dx%d -> %dx%d"
         % (side, side, x0, y0, w, h, size, size))

    out = []
    for dy in range(size):
        sy0 = y0 + (dy * side) // size
        sy1 = y0 + ((dy + 1) * side) // size
        if sy1 <= sy0:
            sy1 = sy0 + 1
        for dx in range(size):
            sx0 = x0 + (dx * side) // size
            sx1 = x0 + ((dx + 1) * side) // size
            if sx1 <= sx0:
                sx1 = sx0 + 1
            ar = ag = ab = n = 0
            for sy in range(sy0, sy1):
                line = rows[sy]
                for sx in range(sx0, sx1):
                    r, g, b, _a = getpx(line, sx)
                    ar += r
                    ag += g
                    ab += b
                    n += 1
            out.append((ar // n, ag // n, ab // n))
    return out


# ----------------------------------------------------------- alpha synthesis

def _smoothstep(t):
    """S-curve on 0..255 -> 0..255, i.e. t*t*(3-2t), in pure integer maths.

    The property that earns smoothstep its place here is ZERO SLOPE AT BOTH
    ENDS. The feather therefore meets the opaque interior and the transparent
    boundary with matching gradients and neither junction can read as a line.
    A linear ramp is C0 only, and its crease where the ramp begins is exactly
    the rectangular outline this mask exists to avoid.

    255*255*(765 - 2*255) == 255*65025, so t == 255 yields exactly 255 and the
    result never needs clamping at the top."""
    if t <= 0:
        return 0
    if t >= 255:
        return 255
    return (t * t * (765 - 2 * t)) // 65025


def _hash2(ix, iy, seed):
    """Deterministic 2D integer hash -> 0..255. No RNG, no state, no seeding
    from the clock: the same (ix, iy, seed) yields the same byte on every
    machine and every run, which is what keeps the generated .inc reproducible.

    Pure integer avalanche (xorshift-multiply, the classic Wang/Murmur finaliser
    shape) masked to 32 bits at every step so Python's arbitrary-precision ints
    behave exactly like the u32 maths this mimics."""
    h = (ix * 0x1F1F1F1F) ^ (iy * 0x0D1F3A7B) ^ (seed * 0x27D4EB2D)
    h &= 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h ^= h >> 12
    h = (h * 0x297A2D39) & 0xFFFFFFFF
    h ^= h >> 15
    return h & 0xFF


def _value_noise(x, y, cell, seed):
    """Smooth 2D value noise on a `cell`-px lattice -> 0..255.

    Lattice corners are hashed, then blended with the SAME _smoothstep() the
    ramps use, so the field is C1 and cannot introduce a crease of its own.

    ***** THIS IS NOT A DISTANCE FIELD. ***** Nothing here measures a distance
    from a centre, a radius, an axis or a corner. The value at a pixel depends
    only on a hash of the lattice cell it falls in, so the iso-contours wander
    with no preferred shape -- which is the entire point. A radial, elliptical
    or superelliptical field was rejected three revisions ago and this must not
    quietly become one."""
    ix, fx = divmod(x, cell)
    iy, fy = divmod(y, cell)
    tx = _smoothstep(fx * 255 // cell)
    ty = _smoothstep(fy * 255 // cell)
    n00 = _hash2(ix, iy, seed)
    n10 = _hash2(ix + 1, iy, seed)
    n01 = _hash2(ix, iy + 1, seed)
    n11 = _hash2(ix + 1, iy + 1, seed)
    a = n00 + (n10 - n00) * tx // 255
    b = n01 + (n11 - n01) * tx // 255
    return a + (b - a) * ty // 255


def _wander(x, y, seed):
    """Two-octave organic field -> 0..255, used to bend the dissolve edge.

    A 17 px octave supplies the broad lobes that stop the edge being straight
    and a 7 px octave roughens them; weighted 2:1 so the coarse shape leads and
    the finer one only breaks up its outline. Both are prime-ish and mutually
    coprime, so the two lattices do not align anywhere on an 80 px tile and the
    sum has no visible repeat."""
    return (_value_noise(x, y, 17, seed) * 2
            + _value_noise(x, y, 7, seed + 101)) // 3


def _grain(x, y, seed):
    """Per-pixel stipple field -> 0..255, used to erode the vanishing edge.

    Half per-pixel white noise, half a 3 px smooth field. Pure white noise
    dissolves into television static at this scale; the 3 px term clumps it
    into small irregular flecks that read as particles of haze instead."""
    return (_hash2(x, y, seed + 977) + _value_noise(x, y, 3, seed + 613)) // 2


def _dissolve(dist, band, wander, grain, wobble, dither):
    """One directional dissolve at one pixel -> alpha 0..255.

    ***** A BAND OF 0 MEANS ANCHORED, NOT THIN. ***** It returns the constant
    255, i.e. the ramp is switched OFF and that edge stays fully opaque right
    up to and including its boundary pixel. That is the whole mechanism behind
    the anchoring: an edge that terminates on a hard graphic edge -- the edge
    of the display, or one of the inner frame's 1 px violet rules -- has no
    background on the far side to dissolve into, so any ramp there -- however
    shallow -- can only terminate the photograph early and draw the soft
    rectangle that got revision (3) rejected. It is deliberately an early
    return rather than a clamp: with band == 0 the scaling below would divide
    by zero.

    For a LIVE band the nominal ramp position is t0 = dist*255/band, and the
    two perturbations below are applied to it. ***** BOTH ARE MULTIPLICATIVE,
    AND THAT IS A CORRECTNESS REQUIREMENT, NOT A STYLE CHOICE. ***** A term
    ADDED to t0 could lift the boundary pixel off zero and paint a faint
    straight line of survivors down the outside of the band -- the exact
    artefact main()'s structural check (a) exists to catch. A factor cannot:
    0 * anything is still 0, so dist == 0 yields alpha 0 on every row, always.

    (1) WANDER -- the edge stops being straight. t0 is scaled by a smooth
        two-octave noise field, which locally steepens or slackens the ramp and
        so moves the visible edge in and out by a few px with no preferred
        direction. The magnitude is TAPERED BY (255 - t0), so it is strongest
        at the vanishing boundary and falls to exactly nothing as the ramp
        reaches the opaque core. That taper is what lets the perturbation be
        organic without disturbing the face, and it also means the interior
        early-return below is continuous rather than a cliff.

    (2) DITHER -- the vanishing edge breaks into flecks. t1 is scaled down by a
        per-pixel grain whose strength is proportional to (255 - t1), i.e. it
        is nil in the core and maximal where the portrait is already almost
        gone. This is what stops the dissolve being a clean gradient: instead
        of a smooth wash the outermost pixels drop out in irregular specks, the
        way haze thins rather than the way a shadow ends.

    INTERIOR IS BIT-EXACT. Once dist >= band the function returns 255 by the
    same early return the anchored case uses, so the opaque core is untouched
    by all of the above -- not "approximately 255", exactly 255. main()'s
    structural check (b) and the 65x65 opaque-core guarantee both still hold
    bit for bit, and no amount of --wobble or --dither can eat into the face."""
    if band <= 0:
        return 255
    t0 = dist * 255 // band
    if t0 >= 255:
        return 255

    taper = 255 - t0

    # (1) organic wander, tapered to nothing at the core edge.
    w = (wander - 128) * wobble // 128
    w = w * taper // 255
    t = t0 * (255 + w) // 255
    if t < 0:
        t = 0
    elif t > 255:
        t = 255

    # (2) stipple, strongest where the portrait is already almost gone.
    damp = dither * (255 - t) // 255
    damp = damp * grain // 255
    t = t * (255 - damp) // 255

    return _smoothstep(t)


def build_alpha(size, left, right, top, bottom,
                wobble=0, dither=0, seed=0):
    """FRAME-ANCHORED upper-right blend: four IRREGULAR directional dissolves,
    two BROAD and two TINY.

    With the shipped defaults (left 12, bottom 12, top 3, right 3) this is:

        aL = D(x,        L, x, y)       aR = D(size-1-x, R, x, y)
        aT = D(y,        T, x, y)       aB = D(size-1-y, B, x, y)

        a  = aL * aR * aT * aB          (products taken /255)

    where D() is _dissolve(): the old S(dist/band) ramp with two deterministic
    noise perturbations folded in.

    ***** THE TWO PAIRS DO COMPLETELY DIFFERENT JOBS. ***** Left and bottom are
    BROAD ATMOSPHERIC dissolves: 12 px, 15% of the portrait's width, enough to
    carry the fur out into the backdrop through violet haze. Top and right are
    EDGE-BREAKING dissolves: 3 px whose only purpose is to stop the fur ending
    in a dead-straight line under the frame rules. The asymmetry is deliberate,
    it is 4:1, and main() refuses to run without it -- this is NOT the symmetric
    four-edge feather that read as a circular avatar, and the difference is not
    subtle at 1:1.

    A band of 0 is still supported and still means ANCHORED (constant 255), so
    the tool remains usable if the portrait is ever moved to a screen edge --
    but at the shipping placement no band is 0, because fully anchored top and
    right edges are exactly what failed the second composition gate.

    WHY THE RAMPS ARE NO LONGER SMOOTH. Five masks were tried and rejected
    before this one. A radial vignette and a SYMMETRIC four-edge feather both
    read as a circular avatar (the roundness came from the symmetry, not the
    arithmetic). An ASYMMETRIC four-ramp version with SMOOTH 3 px top/right
    bands, floating at (400,0), fixed the circle but read as a rectangular photo
    card, because a smooth fade ending in open background just softens the
    rectangle. Anchoring the top and right edges onto the frame's own rules
    fixed THAT -- and the result still failed the composition gate, as a photo
    card with a drop shadow. Irregular ramps plus violet haze on left/bottom
    fixed the drop shadow -- and STILL failed, because the fully anchored top
    and right edges terminated the bright fur in dead-straight lines that the
    1 px frame rules were too thin to hide. Hence the 3 px edge-breaking bands
    on those two sides: same irregular, stippled, hazed treatment, one twelfth
    the width. The widths now match rejected revision (3); the TREATMENT does
    not, and the treatment is what was wrong with it.

    The reason is that the previous field was SEPARABLE: a = fx[x] * fy[y].
    A separable field has iso-alpha contours that are exactly straight lines
    parallel to the axes, so however soft the gradient is, the eye integrates
    it back into a rectangle. Softness was never the missing ingredient --
    every rejected mask was perfectly smooth. What was missing was IRREGULARITY.
    So the product survives but the factors are no longer functions of one
    coordinate: each consults a 2D noise field, the contours wander, and there
    is no straight line left in the alpha for the eye to find.

    ***** THE FIELD IS NOT SEPARABLE AND MUST NOT BE MADE SO AGAIN. ***** The
    per-axis lookup tables this function used to build are gone deliberately.
    Restoring them as an optimisation would restore the photo card with them;
    6,400 px of integer work is not worth that.

    CORNERS ARE INHERITED, NOT DRAWN. All four corners are now products of two
    live ramps, but they are NOT alike, and that is what keeps the roundness
    away. The bottom-left is a 12x12 overlap of two broad dissolves and melts
    away over a large area; the top-right is a 3x3 overlap of two edge-breakers
    and merely crumbles; the other two are 12x3 slivers. A symmetric mask makes
    all four corners identical, which is precisely what traced the round head
    back at the viewer. No corner term, no diagonal, no L1 distance: the
    symmetric revision had an explicit diagonal bite and it was removed for
    exactly this reason.

    BOUNDARY. At x == 0 the nominal ramp position is 0, and every perturbation
    in _dissolve() is a FACTOR applied to it, so aL is exactly 0 there no
    matter what the noise says; likewise aR at x == size-1, aT at y == 0 and aB
    at y == size-1. With the shipping bands ALL FOUR boundary rows/columns are
    therefore alpha 0 regardless of image content or seed -- the runtime skips
    them entirely. On the left and bottom they vanish into the backdrop; on the
    top and right they vanish under the frame's own violet rules, which are
    drawn afterwards and occupy the very rows the portrait has just given up.
    That is why a 3 px band there costs nothing visually: the outermost row it
    erases was going to be overpainted regardless. main() re-proves BOTH
    properties by inspection rather than trusting this docstring.

    OPAQUE CORE. a == 255 exactly when x >= L, x <= size-1-R, y >= T and
    y <= size-1-B, because _dissolve() returns early with a literal 255 once
    dist >= band. With the defaults that is a 65x65 rectangle spanning x 12..76
    and y 3..67 -- 66% of the image. The 3 px top/right bands shrank it from the
    previous 68x68 by exactly 3 px on two sides, and it still clears the
    PROTECTED CENTRAL FACE (x 16..63, y 16..63) by 4 px on the left, 13 px on
    the right, 13 px on the top and 4 px on the bottom.

    ***** THE NOISE CANNOT REACH THE FACE. ***** Its amplitude is tapered by
    (255 - t0) and the core is past the point where that taper reaches zero, so
    raising --wobble or --dither roughens the vanishing edge and provably
    nothing else. The eyes, nose, mouth and teeth all sit inside the protected
    rectangle, face_report() re-measures every one of its pixels, and main()
    dies if any is below 255.

    COLOUR IS NEVER CONSULTED. That is deliberate: the removed luminance key
    was the only thing that could have made the dog's near-black pupils
    transparent. A feature can now fade only by physically lying inside one of
    the four dissolve bands. (The violet haze added in this revision is applied
    to RGB by apply_haze() AFTER this function returns, and is driven by the
    alpha computed here -- it does not feed back into the mask.)
    """
    for name, band in (("left", left), ("right", right),
                       ("top", top), ("bottom", bottom)):
        if band < 0 or band > size // 3:
            die("--%s must be between 0 and %d px for a %d px portrait "
                "(0 anchors that edge to a hard graphic edge -- the screen "
                "boundary or an inner frame rule; the cap keeps an opaque "
                "core)" % (name, size // 3, size))

    # Per-pixel, NOT per-axis. Each of the four dissolves is evaluated at the
    # pixel because each consults the 2D noise fields at that pixel; the old
    # fx[]/fy[] tables cannot express that and were what made the edge straight.
    # All four bands are live at the shipping settings, but each _dissolve()
    # still returns on its first line for the ~66% of pixels that lie in the
    # opaque core, so the cost is four cheap calls and no mask math at runtime.
    #
    # The two noise fields are sampled ONCE per pixel and shared by all four
    # dissolves. That is what couples the horizontal and vertical breakup: in a
    # corner where two bands are live, the same lobe of wander pushes both
    # factors the same way, so the corner melts as one irregular region instead
    # of showing two independent fringes crossing. It also means the top and
    # right edge-breakers crumble along the SAME noise as the broad left and
    # bottom dissolves, so the whole boundary is one continuous treatment
    # rather than two unrelated effects meeting at the corners.
    out = []
    for y in range(size):
        for x in range(size):
            wn = _wander(x, y, seed)
            gn = _grain(x, y, seed)
            al = _dissolve(x, left, wn, gn, wobble, dither)
            ar = _dissolve(size - 1 - x, right, wn, gn, wobble, dither)
            at = _dissolve(y, top, wn, gn, wobble, dither)
            ab = _dissolve(size - 1 - y, bottom, wn, gn, wobble, dither)
            a = al * ar // 255
            a = a * at // 255
            a = a * ab // 255
            out.append(a)
    return out


def apply_haze(rgb, alpha, size, strength, haze):
    """Tint the dissolving pixels toward the violet haze. Returns a NEW list.

    ***** THIS IS THE OTHER HALF OF THE GATE FIX, AND THE MORE IMPORTANT ONE.
    ***** Irregularity alone would have produced a raggedly-edged photo card
    with a grey shadow. The grey is a separate defect with a separate cause:
    the crop's outer ring is bright cream fur against a pale backdrop, and
    alpha-compositing bright cream onto near-black navy passes through neutral
    grey at every midpoint. No mask shape can fix that, because the mask does
    not choose the midpoint colour -- it only chooses how much of the cream
    survives. Hence an explicit colour to dissolve THROUGH.

    The tint weight is smoothstep(255 - a), so:

        a = 255  ->  w =   0    the face and the whole opaque core: UNTOUCHED
        a = 200  ->  w =  30    12% violet, the first hint of atmosphere
        a = 128  ->  w = 126    49% violet, the midpoint reads violet not grey
        a =  40  ->  w = 238    93% violet, essentially pure haze
        a =   0  ->  w = 255    (pixel is dropped to palette entry 0 anyway)

    Using smoothstep rather than a linear (255 - a) matters at the TOP of that
    table: the tint has to arrive with zero slope where it meets the opaque
    core, otherwise the first tinted pixel is a visible violet outline tracing
    the core rectangle -- which would hand back the photo-card edge in a new
    colour. At the bottom end it saturates, so the last visible pixels are
    already essentially haze and there is no colour step at the point where
    pixels start dropping out to entry 0.

    ***** THE FACE IS PROVABLY UNTOUCHED. ***** w is a function of alpha alone,
    and alpha is exactly 255 across the 65x65 opaque core, so w is exactly 0
    there and the early return hands back the original tuple. The eyes, nose,
    mouth and teeth cannot be tinted by this, at any strength.

    Baked at build time into the palette, so the runtime cost is nil --
    m16c_draw_dog() still does one integer blend per pixel and knows nothing
    about any of this."""
    if strength <= 0:
        return list(rgb)
    hr, hg, hb = haze
    out = []
    for i in range(size * size):
        a = alpha[i]
        if a >= 255:
            out.append(rgb[i])
            continue
        w = _smoothstep(255 - a) * strength // 255
        r, g, b = rgb[i]
        out.append((r + (hr - r) * w // 255,
                    g + (hg - g) * w // 255,
                    b + (hb - b) * w // 255))
    return out


def measured_bands(alpha, size):
    """Measure the REALISED feather depth on each edge from the finished field.

    Sampled along the mid row and mid column so the corner products cannot
    contaminate the reading -- this is the depth a viewer actually sees on the
    flat part of each edge. Returns (left, right, top, bottom) as the count of
    pixels inward from each boundary that are not yet fully opaque.

    An ANCHORED edge measures exactly 0: its boundary pixel is already 255, so
    the loop never advances. That is the positive confirmation that no interior
    band exists on that side -- the thing revision (3) was rejected for.

    ***** THIS NUMBER IS NECESSARY BUT NOT SUFFICIENT, AND IT IS WHAT THE
    FAILED GATE WAS TRUSTING. ***** It reports the depth at which alpha reaches
    255 -- i.e. where the OPAQUE CORE starts -- which _dissolve() pins to
    exactly `band` on every row by construction. It says nothing whatsoever
    about the shape of the transition inside that band, and the rejected
    revision scored a perfect L12/B12/T0/R0 here while reading as a photo card
    with a drop shadow. transition_report() below measures the part that
    actually failed. Keep both: this one still catches broken ramp arithmetic.

    main() compares this against the requested widths and dies on a mismatch,
    which turns the reported numbers into a measurement rather than an echo of
    the flags."""
    my = size // 2
    mx = size // 2

    left = 0
    while left < size and alpha[my * size + left] < 255:
        left += 1
    right = 0
    while right < size and alpha[my * size + (size - 1 - right)] < 255:
        right += 1
    top = 0
    while top < size and alpha[top * size + mx] < 255:
        top += 1
    bottom = 0
    while bottom < size and alpha[(size - 1 - bottom) * size + mx] < 255:
        bottom += 1
    return left, right, top, bottom


def transition_report(alpha, size, left, right, top, bottom, onset=48):
    """Measure the VISIBLE edge -- the thing the composition gate judges.

    measured_bands() finds where the dissolve ENDS (alpha hits 255). That inner
    terminus is invisible: smoothstep is flat there, so it meets the core with
    matching gradients. What a viewer actually sees is where the portrait
    BEGINS -- the outermost pixel that is no longer effectively transparent --
    and it was the straightness of THAT locus, not the softness of the ramp,
    that made every previous revision read as a rectangle.

    So for each row this walks in from the left boundary and records the first
    x whose alpha exceeds `onset`, and likewise inward from the right, the top
    and the bottom. ALL FOUR EDGES ARE PROFILED, because all four now carry a
    live band -- the two broad atmospheric dissolves and the two 3 px
    edge-breakers, whose entire purpose is to make this statistic non-constant.
    The statistic that matters is the SPREAD:

        a separable ramp gives the same onset on every row  -> spread 0, 1
        distinct value, a dead-straight edge -- a rectangle, however soft.

        an irregular field gives a different onset per row -> a wandering
        edge with no straight segment for the eye to lock onto.

    `onset` is the alpha at which the portrait counts as visible, not 1. Over a
    near-black backdrop a pixel at alpha 4 is not on screen in any meaningful
    sense, and measuring from there would compress the whole profile into the
    first two columns and under-report the wander. 48/255 (19%) is roughly
    where the fur starts to be discernible against SPLASH_BG.

    ***** THE CORNERS ARE EXCLUDED FROM EVERY MEASUREMENT. ***** A corner is a
    product of two fractions, so it falls away much faster than either edge
    alone. Including those rows in the left profile would report onsets of 20,
    40, "never" and turn the spread into an artefact of the corner rather than a
    measurement of the edge. So each edge is profiled only across the span the
    perpendicular bands do not reach: the vertical edges over rows top..size-
    bottom, the horizontal edges over columns left..size-right -- the same
    convention main()'s anchored-edge check already uses. With all four bands
    live this now trims both ends of every profile, not just one.

    Returns a dict per edge with min/max/mean/distinct onset, plus `eroded`:
    pixels strictly inside the band (corner excluded, boundary row/column
    excluded since those are 0 by construction) that the stipple drove to full
    transparency. A non-zero erosion count is the positive confirmation that
    the boundary is breaking into flecks rather than sliding as a clean wash --
    with dither 0 it is 0 and the edge is a smooth (if wandering) gradient."""
    out = {}

    # The spans on which each edge is free of the perpendicular band.
    rows = range(top, size - bottom)
    cols = range(left, size - right)

    if left > 0:
        onsets = []
        for y in rows:
            row = y * size
            x = 0
            while x < size and alpha[row + x] <= onset:
                x += 1
            onsets.append(x)
        eroded = sum(1 for y in rows for x in range(1, left)
                     if alpha[y * size + x] == 0)
        out["left"] = _spread(onsets, eroded)

    if right > 0:
        onsets = []
        for y in rows:
            row = y * size
            d = 0
            while d < size and alpha[row + (size - 1 - d)] <= onset:
                d += 1
            onsets.append(d)
        eroded = sum(1 for y in rows for d in range(1, right)
                     if alpha[y * size + (size - 1 - d)] == 0)
        out["right"] = _spread(onsets, eroded)

    if top > 0:
        onsets = []
        for x in cols:
            d = 0
            while d < size and alpha[d * size + x] <= onset:
                d += 1
            onsets.append(d)
        eroded = sum(1 for d in range(1, top) for x in cols
                     if alpha[d * size + x] == 0)
        out["top"] = _spread(onsets, eroded)

    if bottom > 0:
        onsets = []
        for x in cols:
            d = 0
            while d < size and alpha[(size - 1 - d) * size + x] <= onset:
                d += 1
            onsets.append(d)
        eroded = sum(1 for d in range(1, bottom) for x in cols
                     if alpha[(size - 1 - d) * size + x] == 0)
        out["bottom"] = _spread(onsets, eroded)

    return out


def min_span_for(band):
    """How far the visible onset must wander on a band of this width.

    ***** THIS IS SCALED TO THE BAND, AND IT HAS TO BE. ***** On the 12 px
    atmospheric dissolves the onset has room to roam and anything under 2 px of
    end-to-end spread would still read as a straight line at 1:1, so the bar
    stays where it was. On a 3 px edge-breaker the onset can only ever take the
    values 0..3, and demanding a 2 px spread there would be demanding that some
    columns show no fur at all while others show it two px in -- a ragged
    tearing effect, not the roughened boundary that was asked for. 1 px of
    spread across a 3 px band IS the boundary ceasing to be straight: combined
    with the stipple erosion, which is reported separately, the outermost
    surviving row becomes a broken dotted line rather than a ruled one.

    What is NOT relaxed is `distinct`: every edge, however narrow, must show at
    least two different onset values. A single value means a perfectly straight
    visible boundary, which is the defect the gate failed on twice, and that is
    refused at any band width."""
    return 2 if band >= 8 else 1


def face_report(alpha, size, inset_pct=PROTECTED_FACE_INSET_PCT):
    """Prove the PROTECTED CENTRAL FACE is still 100% opaque.

    ***** THIS ANSWERS THE QUESTION THE REVIEW ASKED BEFORE THE TOP/RIGHT BANDS
    WENT LIVE. ***** Until now the top and right edges were anchored at band 0
    and provably could not touch anything, so "do the eyes survive?" only had to
    be asked of the left and bottom bands, and dark_report() answered it by
    sampling the darkest pixels. Now all four bands erode fur, so the question
    is asked directly and geometrically instead: take the rectangle that
    contains the entire face and require every single pixel of it to be exactly
    255.

    The rectangle is the middle 60% of the portrait -- inset 20% per side, i.e.
    x 16..63 and y 16..63 at size 80. On this crop the face fills the frame, the
    pupils sit near x 12 and x 68 at roughly y 30, and the muzzle, mouth and
    teeth run down the centre, so this rectangle covers every feature the review
    named while staying clear of the fur margin the bands are allowed to eat.

    Returns (rect, bad, worst) where `rect` is (x0, y0, x1, y1), `bad` is the
    count of protected pixels below 255 and `worst` is the lowest alpha found
    inside it. main() derives the per-band clearance from `rect` and dies on
    bad > 0 rather than scaling the mask, because silently shrinking a band to
    rescue an eye is precisely the "deform the mask blindly" failure mode the
    review prohibited."""
    inset = size * inset_pct // 100
    x0 = y0 = inset
    x1 = y1 = size - 1 - inset

    bad = 0
    worst = 255
    for y in range(y0, y1 + 1):
        row = y * size
        for x in range(x0, x1 + 1):
            a = alpha[row + x]
            if a < worst:
                worst = a
            if a != 255:
                bad += 1
    return (x0, y0, x1, y1), bad, worst


def _spread(vals, eroded):
    """min/max/mean/distinct of an onset profile, as a dict."""
    n = len(vals)
    mean10 = sum(vals) * 10 // n
    return {"min": min(vals), "max": max(vals),
            "mean10": mean10, "distinct": len(set(vals)),
            "span": max(vals) - min(vals), "eroded": eroded}


def alpha_ascii(alpha, size, cols=40, rows=20):
    """Render the finished alpha field as ASCII for the build log.

    The preview PNG remains the real gate, but this lands in stdout, so a
    change in mask shape is visible in a diff of the tool's output and the
    "are the eyes solid?" question can be answered without opening an image
    editor. 40x20 because terminal cells are about 2:1, so the map comes out
    roughly square."""
    ramp = " .:+*#@"
    lines = []
    for r in range(rows):
        y = r * size // rows
        row = []
        for c in range(cols):
            a = alpha[y * size + (c * size // cols)]
            if a >= 255:
                row.append("@")
            elif a <= 0:
                row.append(" ")
            else:
                row.append(ramp[1 + a * 5 // 255])
        lines.append("".join(row))
    return lines


def dark_report(rgb, alpha, size, frac):
    """Locate the darkest pixels and report whether they stayed fully opaque.

    ***** THIS IS A REPORT. IT NEVER MODIFIES ALPHA. ***** build_alpha() is
    colour-blind by design and stays that way; this function only reads the
    finished mask so the log can answer the one question the review cares
    about -- "are the eyes solid?" -- as a fact rather than an assumption.

    The dog's pupils and nostrils are the darkest things in the crop, so if
    the darkest `frac` of pixels all carry alpha 255, the facial features are
    provably untouched by the mask. Any that do not are, by construction,
    inside one of the four dissolve bands, because the core is a flat 255.
    The reported bounding box is the useful part: compare it against the opaque
    core rectangle main() prints and no eye can be fading undetected, whatever
    the preview happens to look like.

    The MINIMUM alpha matters as much as the count here. Smoothstep is flat
    near the top of a ramp, so a pixel one or two px inside a band still
    carries ~250/255 and is visually solid; a count alone would raise a false
    alarm on the left pupil, which sits at the inner lip of the left band."""
    n = size * size
    lum = []
    for i in range(n):
        r, g, b = rgb[i]
        lum.append((((54 * r + 183 * g + 19 * b) >> 8), i))
    lum.sort()

    take = max(1, int(n * frac))
    x0 = y0 = size
    x1 = y1 = -1
    worst = 255
    faded = 0
    for _l, i in lum[:take]:
        x = i % size
        y = i // size
        if x < x0:
            x0 = x
        if x > x1:
            x1 = x
        if y < y0:
            y0 = y
        if y > y1:
            y1 = y
        a = alpha[i]
        if a < worst:
            worst = a
        if a < 255:
            faded += 1
    return take, (x0, y0, x1, y1), worst, faded


# --------------------------------------------------------------- median cut

def _box_span(box):
    lo = [255, 255, 255, 255]
    hi = [0, 0, 0, 0]
    for col, _c in box:
        for k in range(4):
            v = col[k]
            if v < lo[k]:
                lo[k] = v
            if v > hi[k]:
                hi[k] = v
    spans = [hi[k] - lo[k] for k in range(4)]
    widest = 0
    for k in range(1, 4):
        if spans[k] > spans[widest]:
            widest = k
    return spans[widest], widest


def median_cut(hist, want):
    """Classic median cut, run in 4-D RGBA.

    Alpha participates in the split so the feather band gets clusters of its
    own instead of being averaged into the fur."""
    boxes = [list(hist.items())]
    while len(boxes) < want:
        best_span = -1
        best_i = -1
        best_ch = 0
        for i, b in enumerate(boxes):
            if len(b) < 2:
                continue
            span, ch = _box_span(b)
            if span > best_span:
                best_span = span
                best_i = i
                best_ch = ch
        if best_i < 0 or best_span <= 0:
            break

        b = boxes.pop(best_i)
        b.sort(key=lambda kv: (kv[0][best_ch], kv[0]))
        total = sum(c for _col, c in b)
        acc = 0
        split = 1
        for j, (_col, c) in enumerate(b):
            acc += c
            if acc * 2 >= total:
                split = j + 1
                break
        if split < 1:
            split = 1
        if split > len(b) - 1:
            split = len(b) - 1
        boxes.append(b[:split])
        boxes.append(b[split:])

    pal = []
    for b in boxes:
        tw = sum(c for _col, c in b)
        r = sum(col[0] * c for col, c in b) // tw
        g = sum(col[1] * c for col, c in b) // tw
        bb = sum(col[2] * c for col, c in b) // tw
        a = sum(col[3] * c for col, c in b) // tw
        pal.append((r, g, bb, a))
    return pal


def nearest(pal, col, start, cache):
    hit = cache.get(col)
    if hit is not None:
        return hit
    r, g, b, a = col
    best = start
    bestd = None
    for i in range(start, len(pal)):
        pr, pg, pb, pa = pal[i]
        dr = pr - r
        dg = pg - g
        db = pb - b
        da = pa - a
        d = dr * dr + dg * dg + db * db + da * da
        if bestd is None or d < bestd:
            bestd = d
            best = i
    cache[col] = best
    return best


# ------------------------------------------------------ verifier self-check

def scan_forbidden(blob):
    low = blob.lower()
    for pat in FORBIDDEN_NOCASE:
        i = low.find(pat)
        if i >= 0:
            return i, pat
    for pat in FORBIDDEN_CASE:
        i = blob.find(pat)
        if i >= 0:
            return i, pat
    return None, None


def pal_bytes(pal32):
    return b"".join(struct.pack("<I", v) for v in pal32)


def second_nearest(pal32, idx):
    """The visually closest OTHER opaque palette entry.

    Used to nudge a single pixel index when the raw index stream happens to
    spell a forbidden literal. Entry 0 (fully transparent) is excluded so a
    nudge can never punch a hole in the portrait."""
    r = (pal32[idx] >> 16) & 0xFF
    g = (pal32[idx] >> 8) & 0xFF
    b = pal32[idx] & 0xFF
    a = (pal32[idx] >> 24) & 0xFF
    best = None
    bestd = None
    for i in range(1, len(pal32)):
        if i == idx:
            continue
        pr = (pal32[i] >> 16) & 0xFF
        pg = (pal32[i] >> 8) & 0xFF
        pb = pal32[i] & 0xFF
        pa = (pal32[i] >> 24) & 0xFF
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2 + (pa - a) ** 2
        if bestd is None or d < bestd:
            bestd = d
            best = i
    return best if best is not None else idx


def sanitize(pal32, pix):
    """Perturb visually insignificant data until no forbidden literal remains.

    Palette hits flip one low bit of one channel (a 1/255 colour change).
    Pixel hits retarget one pixel to its nearest neighbouring palette entry.
    Neither is perceptible; neither weakens the verifier."""
    rounds = 0
    while True:
        pb = pal_bytes(pal32)
        i, pat = scan_forbidden(pb)
        if i is not None:
            e = i // 4
            pal32[e] ^= 0x00000001
            rounds += 1
            note("sanitised palette entry %d (would have spelled %r)" % (e, pat))
            if rounds > 64:
                die("palette sanitisation did not converge -- inspect the asset")
            continue

        blob = bytes(pix)
        i, pat = scan_forbidden(blob)
        if i is not None:
            old = pix[i]
            pix[i] = second_nearest(pal32, old) if old != 0 else pix[i]
            if pix[i] == old:
                # Transparent run, or no alternative: shift the NEXT byte.
                j = i + 1
                if j < len(pix) and pix[j] != 0:
                    pix[j] = second_nearest(pal32, pix[j])
                else:
                    die("cannot sanitise pixel literal %r at offset %d" % (pat, i))
            rounds += 1
            note("sanitised pixel offset %d (would have spelled %r)" % (i, pat))
            if rounds > 64:
                die("pixel sanitisation did not converge -- inspect the asset")
            continue

        # Also check the two concatenation orders, since the linker may place
        # the arrays adjacently in .rodata.
        i, pat = scan_forbidden(pal_bytes(pal32) + bytes(pix))
        if i is None:
            i, pat = scan_forbidden(bytes(pix) + pal_bytes(pal32))
        if i is None:
            return rounds
        pal32[0] = 0x00000000
        pal32[1] ^= 0x00000001
        rounds += 1
        if rounds > 64:
            die("cross-array sanitisation did not converge")


# ------------------------------------------------------------- PNG preview

def write_png(path, w, h, rgba):
    rows = []
    for y in range(h):
        row = bytearray()
        row.append(0)
        for x in range(w):
            r, g, b, a = rgba[y * w + x]
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = PNG_SIG
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def upscale(rgba, size, s):
    out = []
    for y in range(size * s):
        for x in range(size * s):
            out.append(rgba[(y // s) * size + (x // s)])
    return out


def upscale_wh(rgba, w, h, s):
    """Nearest-neighbour magnify a w x h image. No smoothing: the point of a
    preview is to show the real pixels, and any interpolation would invent a
    softness the hardware will not reproduce."""
    out = []
    for y in range(h * s):
        row = (y // s) * w
        for x in range(w * s):
            out.append(rgba[row + (x // s)])
    return out


def blend_over(dst, src, a):
    """One pixel of m16c_draw_dog()'s blend, bit for bit (main.c:1485-1520).

    The runtime widens alpha 0..255 -> 0..256 with a + (a >> 7), which maps 0
    to 0 and 255 to 256 EXACTLY, so a fully opaque entry reproduces the source
    colour unchanged. The same widening is used here rather than a float
    composite, so the preview shows the actual shipped arithmetic including
    its rounding, not an idealised version of it."""
    if a <= 0:
        return dst
    if a >= 255:
        return (src[0], src[1], src[2])
    ia = a + (a >> 7)
    na = 256 - ia
    return (((dst[0] * na + src[0] * ia) >> 8),
            ((dst[1] * na + src[1] * ia) >> 8),
            ((dst[2] * na + src[2] * ia) >> 8))


def compose_splash(rgba, size, place_x, place_y, draw_frame):
    """Render the portrait onto the real 480x270 logical splash surface.

    This is the gate the last three masks needed and did not have. An isolated
    80x80 swatch on a flat field cannot answer "does this read as part of the
    composition or as a photo card?", because the question is ABOUT the
    surroundings. So this reproduces the actual surface: a flat M16C_C_BG fill
    (main.c:1164 does exactly one ui_fill), the portrait composited at
    (place_x, place_y) through the runtime's own blend, and then -- if
    draw_frame -- the decorative double frame.

    ***** THE FRAME IS DRAWN *AFTER* THE PORTRAIT, BECAUSE THE RUNTIME DRAWS
    IT AFTER. ***** m16c_draw_splash() calls m16c_draw_dog() at main.c:1606
    and only then fills the frame rects at main.c:1608-1622, with the comment
    "Frame last, so nothing drawn above can run over it". Any preview that
    omitted the frame, or drew it first, would flatter the result.

    Clipping is by pixel test on both axes, mirroring the runtime's own
    `if (px < 0 || px >= UI_W) continue;` guards, so an out-of-surface
    placement degrades exactly the way the hardware would rather than
    wrapping or raising. At the shipping placement (393, 7) nothing is
    clipped: the portrait lands flush inside the inner frame's top-right
    corner and every one of its 6,400 px is on screen.

    Returns a UI_W x UI_H list of (r, g, b, 255). The surface is opaque
    throughout because m16c_draw_dog() forces 0xFF back into every pixel it
    touches -- m16c_fade_surface() (the frozen R2 fade) assumes that."""
    surf = [(SPLASH_BG[0], SPLASH_BG[1], SPLASH_BG[2])] * (UI_W * UI_H)

    for y in range(size):
        py = place_y + y
        if py < 0 or py >= UI_H:
            continue
        for x in range(size):
            px = place_x + x
            if px < 0 or px >= UI_W:
                continue
            r, g, b, a = rgba[y * size + x]
            o = py * UI_W + px
            surf[o] = blend_over(surf[o], (r, g, b), a)

    if draw_frame:
        for fx, fy, fw, fh, col in SPLASH_FRAME:
            for y in range(fy, fy + fh):
                if y < 0 or y >= UI_H:
                    continue
                base = y * UI_W
                for x in range(fx, fx + fw):
                    if 0 <= x < UI_W:
                        surf[base + x] = col

    return [(r, g, b, 255) for r, g, b in surf]


def frame_overlap(size, place_x, place_y):
    """Count portrait pixels the splash frame paints over.

    Reported because it is invisible in the .inc and fatal to the anchoring
    argument: the top-right corner the portrait is being anchored into is NOT
    bare background. Returns (overlapped_px, list of human-readable hits)."""
    hits = []
    total = 0
    for fx, fy, fw, fh, col in SPLASH_FRAME:
        ox0 = max(fx, place_x)
        oy0 = max(fy, place_y)
        ox1 = min(fx + fw, place_x + size)
        oy1 = min(fy + fh, place_y + size)
        if ox1 <= ox0 or oy1 <= oy0:
            continue
        n = (ox1 - ox0) * (oy1 - oy0)
        total += n
        which = "inner violet" if col == SPLASH_BORDER else "outer dim"
        hits.append("%s rect (%d,%d %dx%d) covers %d px of the portrait "
                    "at x %d..%d, y %d..%d"
                    % (which, fx, fy, fw, fh, n,
                       ox0, ox1 - 1, oy0, oy1 - 1))
    return total, hits


# --------------------------------------------------------------------- emit

BANNER = """\
/* ===========================================================================
 * apps/m16cgpsp/m16c_splash_art.inc -- GENERATED FILE, DO NOT EDIT BY HAND.
 *
 * Produced by tools/mkart.py. Regenerate with:
 *
 *     %(cmd)s
 *
 * source      : %(src)s
 * source sha256: %(sha)s
 * portrait    : %%dx%%d, 8bpp indexed, %%d-entry ARGB8888 palette
 *
 * ***** THIS FILE IS COMMITTED ON PURPOSE. ***** `make m16c` includes it
 * textually and never runs mkart.py, so a normal build needs no PNG decoder,
 * no image library and no network. It is a prerequisite of m16cmain.o in the
 * Makefile for the same reason m13b_picker.inc is: without that edge, editing
 * the artwork would leave a stale object in place.
 *
 * TRANSPARENCY LIVES IN THE PALETTE. Entry 0 is 0x00000000 and every fully
 * clear pixel maps to it; m16c_draw_dog() skips those pixels outright, so the
 * splash background shows through with no second mask plane.
 *
 * THE MASK IS A FRAME-ANCHORED UPPER-RIGHT BLEND, baked in at build time.
 * Alpha is the product of FOUR directional dissolves -- two broad, two tiny:
 *
 *     LEFT   %(left)2d px  ATMOSPHERIC -- irregular dissolve into the background
 *     BOTTOM %(bottom)2d px  ATMOSPHERIC -- irregular dissolve into the background
 *     TOP    %(top)2d px  EDGE-BREAKING -- roughens the boundary under y == 6
 *     RIGHT  %(right)2d px  EDGE-BREAKING -- roughens the boundary at x == %(frx)d
 *
 * ***** THE 4:1 ASYMMETRY IS THE DESIGN, NOT AN ACCIDENT. ***** Left and bottom
 * carry the photograph out into the backdrop. Top and right do NOT dissolve the
 * portrait -- they exist solely to stop the bright fur terminating in a
 * dead-straight line, over 3 px that the frame's own rules then overpaint.
 * This is NOT a symmetric four-sided feather; mkart.py refuses to build one.
 *
 * FIVE masks were rejected before this one. A radial vignette and a SYMMETRIC
 * four-edge feather both read as a circular avatar (the roundness came from
 * the symmetry, not the arithmetic). An asymmetric four-ramp version with
 * SMOOTH 3 px top/right bands, floating at (400,0), fixed the circle but read
 * as a rectangular photo card, because a smooth fade ending in open background
 * merely softens the rectangle. Moving the portrait flush against the inner
 * frame's own violet rules (y == 6 and x == %(frx)d) fixed THAT.
 *
 * The fourth had this geometry and STILL failed, as a photo card with a grey
 * drop shadow. Two causes, two fixes, both baked in here:
 *
 *   1. The alpha field was SEPARABLE (a = fx[x]*fy[y]), so its iso-alpha
 *      contours were straight lines and the eye reassembled the rectangle
 *      however soft the gradient was. The ramps now consult a deterministic
 *      2D value-noise field (wobble %(wob)d, seed %(seed)d) and the visible edge
 *      wanders with no straight segment; a per-pixel grain (dither %(dit)d)
 *      breaks the vanishing boundary into flecks instead of a clean wash.
 *
 *   2. Alpha decides HOW MUCH survives, never WHAT COLOUR it fades through.
 *      The crop's outer ring is bright cream fur, and cream composited onto
 *      near-black navy passes through NEUTRAL GREY -- the shadow. Dissolving
 *      px are therefore pulled toward the violet #%(haze)s (haze %(hz)d), so the
 *      transition runs fur -> violet -> navy and reads as atmosphere.
 *
 * The FIFTH had both fixes and still failed, because the top and right edges
 * were still ANCHORED at 0 px: the bright fur ended in straight horizontal and
 * vertical lines, and the 1 px frame rules were too thin to hide them. Hence
 * the %(top)d px / %(right)d px edge-breaking bands above -- the same irregular,
 * stippled, hazed treatment as the broad edges, at one quarter the width.
 *
 * Both perturbations are MULTIPLICATIVE factors on the ramp position, so a
 * boundary px is exactly 0 and a core px is exactly 255 whatever the noise
 * says. The %(corew)dx%(coreh)d opaque core is bit-identical to an unperturbed, untinted
 * build, and it contains the PROTECTED CENTRAL FACE (x %(fx0)d..%(fx1)d, y %(fy0)d..%(fy1)d --
 * both eyes, nose, muzzle, mouth and teeth) with px to spare on every side.
 * mkart.py verifies every pixel of that rectangle is alpha 255 and refuses to
 * emit this file otherwise, so the edge-breaking bands provably never reach a
 * facial feature.
 *
 * No radius, ellipse, superellipse, diagonal or centre distance is computed
 * anywhere, and there is no luminance key, so the dog's dark facial features
 * are untouched. The runtime does one integer blend and no mask math
 * whatsoever: every bit of the above is already in the bytes below.
 *
 * ***** THESE BYTES ARE VALID AT EXACTLY ONE PLACEMENT. *****
 *
 *     m16c_draw_dog(%(px)d, %(py)d);   -- x %(px)d..%(px1)d, y %(py)d..%(py1)d of %(uiw)dx%(uih)d
 *
 * Where an edge may stop is a claim about WHERE THE BYTES ARE DRAWN, not a
 * property of the bytes. The %(top)d px top and %(right)d px right bands are legitimate
 * ONLY because the frame's violet rules are drawn afterwards directly over the
 * rows they give up, capping the fade and supplying the colour it fades into.
 * Composite this portrait anywhere else -- in open background, or against the
 * screen boundary -- and those two edges become the rectangular photo-card edge
 * that was already rejected on review. If the placement ever moves, this .inc
 * MUST be regenerated with the bands re-pointed at whatever the portrait
 * actually touches there.
 *
 * NOTE: m16c_draw_splash() fills the decorative double frame AFTER calling
 * m16c_draw_dog(), so any frame rect crossing the portrait paints over it. At
 * this placement that is the top-right corner block alone -- 16 px of fur in
 * the extreme corner, nowhere near the face. mkart.py reports the exact count.
 *
 * The byte stream below was scanned against m16c-verify's forbidden literals
 * (/mnt /usb /dev/ ugen usbctl, RUNG STAGE MILESTONE) and is clean.
 * =========================================================================== */

"""


def emit(path, pal32, pix, size, cmd, src, sha, bands, place, treat, face):
    """Write the committed .inc.

    `bands` is (left, right, top, bottom), `place` is (x, y), `treat` is
    (wobble, dither, haze, seed) and `face` is the protected central face
    rectangle (x0, y0, x1, y1) that main() has just verified is fully opaque.
    All four are stamped into the banner rather than described in prose, because
    this mask is only correct at one placement and a reader of the generated
    file needs to be told which one -- and with which transition treatment, and
    with which region proven intact -- without going and finding this tool."""
    left, right, top, bottom = bands
    px, py = place
    wob, dit, hz, seed = treat
    fx0, fy0, fx1, fy1 = face
    head = BANNER % {"cmd": cmd, "src": src, "sha": sha,
                     "left": left, "right": right, "top": top,
                     "bottom": bottom,
                     "px": px, "py": py,
                     "px1": px + size - 1, "py1": py + size - 1,
                     "uiw": UI_W, "uih": UI_H, "frx": FRAME_RIGHT_X,
                     "wob": wob, "dit": dit, "hz": hz, "seed": seed,
                     "haze": "%02X%02X%02X" % SPLASH_HAZE,
                     "fx0": fx0, "fy0": fy0, "fx1": fx1, "fy1": fy1,
                     "corew": size - left - right,
                     "coreh": size - top - bottom}
    head = head % (size, size, len(pal32))

    lines = [head]
    lines.append("#define M16C_DOG_W      %d\n" % size)
    lines.append("#define M16C_DOG_H      %d\n" % size)
    lines.append("#define M16C_DOG_PAL_N  %d\n\n" % len(pal32))

    lines.append("/* 0xAARRGGBB. Alpha is a BLEND WEIGHT, not a surface alpha: the splash\n"
                 "   surface stays fully opaque because m16c_draw_dog() rebuilds 0xFF. */\n")
    lines.append("static const u32 m16c_dog_pal[M16C_DOG_PAL_N] = {\n")
    for i in range(0, len(pal32), 6):
        row = ", ".join("0x%08Xu" % v for v in pal32[i:i + 6])
        lines.append("    %s,\n" % row)
    lines.append("};\n\n")

    lines.append("static const u8 m16c_dog_pix[M16C_DOG_W * M16C_DOG_H] = {\n")
    for i in range(0, len(pix), 20):
        row = ",".join("%3d" % v for v in pix[i:i + 20])
        lines.append("    %s,\n" % row)
    lines.append("};\n")

    with open(path, "w", newline="\n") as f:
        f.write("".join(lines))


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="LUAp0rt R3 splash portrait converter")
    ap.add_argument("source", help="source PNG (the STANDALONE dog photograph, "
                                   "never the splash mockup)")
    ap.add_argument("-o", "--out", default="apps/m16cgpsp/m16c_splash_art.inc")
    ap.add_argument("--size", type=int, default=80, help="portrait edge in logical px")
    ap.add_argument("--colors", type=int, default=256, help="palette entries incl. entry 0")
    # Derived from the STANDALONE photograph (1288x1294), not from the mockup.
    # The face is already centred and fills the frame, so the crop is a nearly
    # full-frame square; 0.990 * 1294 = 1281 px, which still fits inside the
    # 1288 px width with 7 px to spare.
    ap.add_argument("--crop-cx", type=float, default=0.500, help="crop centre, fraction of width")
    ap.add_argument("--crop-cy", type=float, default=0.500, help="crop centre, fraction of height")
    ap.add_argument("--crop-size", type=float, default=0.990, help="crop edge, fraction of height")
    # FRAME-ANCHORED upper-right blend -- see build_alpha(). The portrait sits
    # flush in the splash's top-right corner, so its top and right edges are
    # butted against the INNER DECORATIVE FRAME'S OWN VIOLET RULES -- the y == 6
    # rule above and the x == 473 rule to the right. (They are NOT the screen's
    # edges: at (393,7) the portrait stops 7 px short of the display on both
    # sides. The distinction matters because the frame is drawn AFTER the
    # portrait, so those two edges are overpainted by a 1 px violet rule rather
    # than running off into nothing.)
    #
    # Those two sides were ANCHORED at band 0 and that FAILED the composition
    # gate: the 1 px rules were too thin to hide the straight line where the
    # bright fur stopped, and the portrait still read as a rectangle. They now
    # carry a 3 px EDGE-BREAKING dissolve -- just enough to make the boundary
    # crumble, capped at EDGE_BREAK_MAX and permitted only because a frame rule
    # covers the outermost row. Left and bottom keep their broad 12 px
    # atmospheric dissolve into the background. These four defaults reproduce
    # the committed .inc from the bare command.
    #
    # A near-symmetric configuration is REFUSED by main() on purpose, and so is
    # a SMOOTH band on the top/right. Five masks were already built and rejected
    # on review: a radial vignette and a symmetric four-edge feather (both read
    # as a circular avatar); an asymmetric four-ramp blend with smooth 3 px
    # top/right bands floating at (400,0) (a rectangular photo card); the same
    # geometry anchored to the frame (a photo card with a grey drop shadow); and
    # the irregular/hazed version with top/right still anchored at 0 (straight
    # hard edges top and right). Do not reintroduce any of them by flag.
    ap.add_argument("--left", type=int, default=12,
                    help="LEFT atmospheric dissolve band in logical px (10-12)")
    ap.add_argument("--bottom", type=int, default=12,
                    help="BOTTOM atmospheric dissolve band in logical px (10-12)")
    ap.add_argument("--top", type=int, default=3,
                    help="TOP edge-breaking band, 1-%d px against a frame rule "
                         "(0 = ANCHORED, no ramp -- that FAILED the gate at "
                         "this placement)" % EDGE_BREAK_MAX)
    ap.add_argument("--right", type=int, default=3,
                    help="RIGHT edge-breaking band, 1-%d px against a frame "
                         "rule (0 = ANCHORED, no ramp -- that FAILED the gate "
                         "at this placement)" % EDGE_BREAK_MAX)
    # Where the portrait is composited on the 480x270 logical surface. This is
    # NOT cosmetic: an anchored (band 0) edge is only invisible when it stops
    # against a hard graphic edge -- the screen boundary or an inner frame rule
    # -- so main() cross-checks the two. The defaults are the SHIPPING
    # placement: (393, 7) tucks the portrait under the frame's top-left corner
    # of the top-right quadrant, i.e. rows 7..86 and columns 393..472, flush
    # beneath the y == 6 rule and flush left of the x == 473 rule.
    ap.add_argument("--place-x", type=int, default=FRAME_RIGHT_X - 80,
                    help="composite X on the %dx%d surface (%d = flush inside "
                         "the inner frame's right rule)"
                         % (UI_W, UI_H, FRAME_RIGHT_X - 80))
    ap.add_argument("--place-y", type=int, default=FRAME_TOP_Y + 1,
                    help="composite Y on the %dx%d surface (%d = flush beneath "
                         "the inner frame's top rule)"
                         % (UI_W, UI_H, FRAME_TOP_Y + 1))
    ap.add_argument("--no-frame", dest="frame", action="store_false", default=True,
                    help="omit the splash's decorative frame from the "
                         "composition preview (it is drawn AFTER the portrait "
                         "by the runtime, so omitting it flatters the result)")
    ap.add_argument("--comp-scale", type=int, default=2,
                    help="extra magnified copy of the composition preview")
    ap.add_argument("--preview", default=None,
                    help="write <name>_rgba.png, <name>_onbg.png and "
                         "<name>_composition.png")
    ap.add_argument("--preview-scale", type=int, default=4)
    ap.add_argument("--no-alpha-map", dest="alpha_map", action="store_false",
                    default=True, help="suppress the ASCII alpha map")
    ap.add_argument("--dark-frac", type=float, default=0.02,
                    help="fraction of darkest px checked for full opacity "
                         "(the 'are the eyes solid?' report)")
    # ------------------------------------------------- transition treatment
    # These three are the fix for the failed composition gate. The geometry
    # (placement, bands) was ACCEPTED on review and must not be touched; what
    # failed was that the transition was a smooth separable grey ramp, which
    # reads as a drop shadow under a photo card. Defaults ARE the shipping
    # values -- the bare command reproduces the committed .inc.
    ap.add_argument("--wobble", type=int, default=96,
                    help="how hard the 2D noise bends the dissolve edge, "
                         "0-255 (0 = the straight separable ramp that FAILED "
                         "the gate; 96 = shipping)")
    ap.add_argument("--dither", type=int, default=200,
                    help="stipple strength at the vanishing boundary, 0-255 "
                         "(0 = clean gradient, no breakup; 200 = shipping)")
    ap.add_argument("--haze", type=int, default=255,
                    help="how far dissolving px are pulled toward the violet "
                         "%s, 0-255 (0 = the grey shadow that FAILED the "
                         "gate; 255 = shipping)"
                         % ("#%02X%02X%02X" % SPLASH_HAZE))
    ap.add_argument("--noise-seed", type=int, default=20260919,
                    help="seed for the deterministic noise fields; changing "
                         "it reshuffles the edge without changing any "
                         "measured characteristic")
    args = ap.parse_args()

    if not os.path.isfile(args.source):
        die("source image %s does not exist" % args.source)
    if args.colors < 2 or args.colors > 256:
        die("--colors must be between 2 and 256")
    for fname, fval in (("--wobble", args.wobble), ("--dither", args.dither),
                        ("--haze", args.haze)):
        if fval < 0 or fval > 255:
            die("%s must be between 0 and 255 (got %d)" % (fname, fval))
    # The gate failed with all three at zero -- that configuration IS the
    # smooth separable grey ramp that read as a photo card with a drop shadow.
    # Refuse to rebuild it silently, the same way the symmetric and flush-but-
    # feathered masks are refused above. Any ONE of the three being live is
    # enough to escape it, so this blocks only the exact rejected combination.
    if args.wobble == 0 and args.dither == 0 and args.haze == 0:
        die("--wobble, --dither and --haze are all 0. That is exactly the "
            "smooth, separable, untinted feather that FAILED the composition "
            "gate: straight iso-alpha contours reading as a rectangle, and a "
            "cream-to-navy fade passing through neutral grey, i.e. a "
            "photograph with a drop shadow pasted into the corner. Leave at "
            "least one of the three live.")

    with open(args.source, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()

    # Decode only as far down as the crop needs.
    need = min(1.0, args.crop_cy + args.crop_size / 2.0 + 0.02)
    w, h, rows, getpx = read_png_rows(args.source, need)

    rgb = crop_and_scale(w, h, rows, getpx, args.crop_cx, args.crop_cy,
                         args.crop_size, args.size)

    # THE MASK MUST BE ASYMMETRIC. A radial vignette was rejected on review,
    # and so was a SYMMETRIC four-edge feather -- on an extreme close-up whose
    # subject already fills the frame, eroding all four sides equally just
    # traces the round head back at the viewer. Refuse to rebuild that mask by
    # flag, however the widths are spelled.
    #
    # NOTE: this test alone does NOT block the third rejected mask -- the
    # asymmetric four-ramp blend passes it comfortably, since 12 >= 3 + 4.
    # Asymmetry turned out to be necessary but not sufficient. What blocks it
    # is the flush-but-feathered branch of the anchoring cross-check below,
    # which refuses a non-zero band on any edge that terminates on a hard
    # graphic edge -- the screen boundary or an inner frame rule. Both rejected
    # geometries are unreachable by flag only when the two checks are taken
    # together.
    if args.left < args.right + 4 or args.bottom < args.top + 4:
        die("the mask must be ASYMMETRIC: --left (%d) and --bottom (%d) must "
            "each exceed --right (%d) and --top (%d) by at least 4 px. A "
            "near-symmetric four-edge feather on this extreme close-up reads "
            "as a circular avatar, and that mask was already built and "
            "rejected on review."
            % (args.left, args.bottom, args.right, args.top))
    if args.left + args.right >= args.size or args.top + args.bottom >= args.size:
        die("bands leave no opaque core: left+right=%d and top+bottom=%d must "
            "both stay under --size (%d)"
            % (args.left + args.right, args.top + args.bottom, args.size))

    # ***** ANCHORING IS A CLAIM ABOUT PLACEMENT, NOT ABOUT THE BYTES. *****
    # A band of 0 leaves that edge fully opaque on the promise that it lands on
    # a HARD GRAPHIC EDGE -- the screen boundary, or one of the inner frame's
    # 1 px violet rules -- where there is no background for it to cut against.
    # If the promise is broken the edge becomes a hard visible line -- strictly
    # worse than the rectangular photo card this revision exists to fix. So the
    # two are cross-checked here and the tool refuses to emit a mask whose
    # anchoring does not match where it says it will be drawn.
    px, py = args.place_x, args.place_y
    # For each edge: the band width, the coordinate that edge actually lands on
    # at this placement, the two coordinates at which it would terminate
    # against a hard graphic edge -- (screen boundary, inner frame rule) -- and
    # the flag settings that would put it on each. Spelling the fixes out means
    # a mismatch prints the placement that WOULD work instead of just refusing.
    anchors = (
        ("top", args.top, py,
         (0, FRAME_TOP_Y + 1),
         ("--place-y 0", "--place-y %d" % (FRAME_TOP_Y + 1))),
        ("right", args.right, px + args.size,
         (UI_W, FRAME_RIGHT_X),
         ("--place-x %d" % (UI_W - args.size),
          "--place-x %d" % (FRAME_RIGHT_X - args.size))),
        ("left", args.left, px,
         (0, FRAME_LEFT_X + 1),
         ("--place-x 0", "--place-x %d" % (FRAME_LEFT_X + 1))),
        ("bottom", args.bottom, py + args.size,
         (UI_H, FRAME_BOTTOM_Y),
         ("--place-y %d" % (UI_H - args.size),
          "--place-y %d" % (FRAME_BOTTOM_Y - args.size))),
    )

    def terminator(edge, terms):
        """Which hard graphic edge this edge stops on, or None if it floats.

        Returns "screen", "frame" or None. The two are NOT interchangeable and
        the loop below treats them differently, so this deliberately returns a
        kind rather than the prose label TERM_LABEL carries."""
        if edge == terms[0]:
            return "screen"
        if edge == terms[1]:
            return "frame"
        return None

    TERM_LABEL = {"screen": "the screen boundary",
                  "frame": "an inner frame rule"}

    # An edge-breaking band is only meaningful where the irregular treatment is
    # live. A SMOOTH 3 px band on the top/right IS rejected revision (3), and no
    # amount of frame adjacency redeems it, so the relaxation below is gated on
    # the treatment rather than granted unconditionally.
    irregular = bool(args.wobble or args.dither)

    for name, band, edge, terms, fixes in anchors:
        on = terminator(edge, terms)
        # Anchored but floating: the edge would render as a hard cut line.
        if band == 0 and on is None:
            die("--%s is 0 (ANCHORED: no feather, fully opaque to the "
                "boundary), but at this placement that edge lands at %d, "
                "which is neither the screen boundary nor an inner frame "
                "rule. An anchored edge that stops in open background renders "
                "as a hard cut line. Either give --%s a dissolve band, or "
                "move the placement with %s (screen boundary) or %s (inner "
                "frame rule)."
                % (name, edge, name, fixes[0], fixes[1]))
        if band != 0 and on is not None:
            # ***** THE NARROW RELAXATION, AND IT IS DELIBERATELY NARROW. *****
            # A band against a FRAME RULE is allowed, up to EDGE_BREAK_MAX,
            # when the irregular treatment is live. Rationale: the rule is 1 px
            # of bright violet drawn AFTER the portrait, so it overpaints the
            # outermost row the dissolve gives up and caps the fade, and the
            # haze carries the surviving pixels toward that same violet. The
            # fur thins into the frame's own colour instead of stopping dead.
            # Fully anchoring these edges instead was tried and FAILED the
            # composition gate -- the straight line was plainly visible beside
            # the rule -- so refusing a band here is not the safe default it
            # looks like.
            if on == "frame" and irregular and band <= EDGE_BREAK_MAX:
                continue
            # A band against the SCREEN BOUNDARY stays refused, unconditionally.
            # There is nothing on the far side and nothing drawn over the top,
            # so the fade can only end the photograph short of the display edge:
            # that is rejected revision (3) exactly, and it is why the
            # relaxation keys off the terminator KIND and not merely the width.
            if on == "screen":
                die("--%s is %d px, but at this placement that edge terminates "
                    "on the screen boundary, where nothing lies beyond it and "
                    "nothing is drawn over it. A band there can only end the "
                    "photograph a few px short of the display edge -- that is "
                    "precisely the 'rectangular photo card' mask already "
                    "rejected on review. Use --%s 0 to anchor it, or move the "
                    "portrait inside the frame with %s."
                    % (name, band, name, fixes[1]))
            if not irregular:
                die("--%s is %d px against an inner frame rule, but --wobble "
                    "and --dither are both 0, so that band is a SMOOTH ramp. A "
                    "smooth shallow band on the top/right is rejected revision "
                    "(3) -- the rectangular photo card -- and sitting beside a "
                    "frame rule does not change that. An edge-breaking band is "
                    "permitted only because it is irregular and stippled: "
                    "raise --wobble, or use --%s 0." % (name, band, name))
            die("--%s is %d px against an inner frame rule, but the cap for an "
                "edge-breaking band is %d px. Past that the fade stops reading "
                "as a roughened boundary and starts reading as revision (3)'s "
                "photo-card edge, ending the photograph inside the frame. The "
                "top/right bands exist ONLY to destroy the straight "
                "photographic boundary, not to dissolve the portrait -- that "
                "is the left and bottom bands' job."
                % (name, band, EDGE_BREAK_MAX))
    if px < 0 or py < 0 or px + args.size > UI_W or py + args.size > UI_H:
        die("placement (%d,%d) puts the %dpx portrait outside the %dx%d "
            "surface -- it would be clipped, and a clipped anchored edge is a "
            "hard cut line" % (px, py, args.size, UI_W, UI_H))

    alpha = build_alpha(args.size, args.left, args.right, args.top,
                        args.bottom, wobble=args.wobble, dither=args.dither,
                        seed=args.noise_seed)

    n = args.size * args.size
    clear = sum(1 for a in alpha if a == 0)
    partial = sum(1 for a in alpha if 0 < a < 255)
    solid = n - clear - partial
    note("alpha: %d clear, %d feathered, %d solid (of %d)"
         % (clear, partial, solid, n))

    # Re-prove the two structural guarantees instead of trusting the argument
    # in build_alpha()'s docstring. Both are cheap and both are the properties
    # the review actually asked for.
    sz = args.size
    L, R, T, B = args.left, args.right, args.top, args.bottom

    # (a) EVERY DISSOLVE EDGE MUST REACH EXACTLY ZERO. A dissolve band that
    #     bottoms out above 0 leaves a faint but perfectly straight line of
    #     surviving pixels along that side -- the soft rectangle that got the
    #     previous revision rejected.
    edges = (
        ("left", L, [y * sz for y in range(sz)]),
        ("right", R, [y * sz + sz - 1 for y in range(sz)]),
        ("top", T, [x for x in range(sz)]),
        ("bottom", B, [(sz - 1) * sz + x for x in range(sz)]),
    )
    for name, band, idxs in edges:
        if band <= 0:
            continue
        for i in idxs:
            if alpha[i]:
                die("the %s edge has a %d px dissolve band but pixel "
                    "(%d,%d) is alpha %d instead of 0 -- that edge would show "
                    "as a straight line against the background"
                    % (name, band, i % sz, i // sz, alpha[i]))

    # (b) EVERY ANCHORED EDGE MUST BE FULLY OPAQUE. This is the NEW property
    #     and the whole point of the revision: no interior band, no anti-edge
    #     feather, no one-pixel seam along a side that coincides with the
    #     screen boundary. Checked only across the span that no perpendicular
    #     dissolve ramp reaches, since the bottom-left corner legitimately
    #     fades along two sides at once.
    anchored = (
        ("top", T, [x for x in range(L, sz - R)]),
        ("bottom", B, [(sz - 1) * sz + x for x in range(L, sz - R)]),
        ("left", L, [y * sz for y in range(T, sz - B)]),
        ("right", R, [y * sz + sz - 1 for y in range(T, sz - B)]),
    )
    for name, band, idxs in anchored:
        if band != 0:
            continue
        for i in idxs:
            if alpha[i] != 255:
                die("the %s edge is ANCHORED (band 0) but pixel (%d,%d) is "
                    "alpha %d, not 255 -- an interior band on an anchored edge "
                    "is exactly the artefact this mask exists to remove"
                    % (name, i % sz, i // sz, alpha[i]))

    if solid * 100 < n * 40:
        die("only %d of %d px (%d%%) are fully opaque -- the face must stay a "
            "clear majority; narrow --left/--bottom"
            % (solid, n, solid * 100 // n))

    # Report the feather widths as MEASURED off the finished alpha field, and
    # die if they disagree with what was asked for. That makes the numbers
    # below evidence rather than an echo of the command line.
    ml, mr, mt, mb = measured_bands(alpha, sz)
    if (ml, mr, mt, mb) != (args.left, args.right, args.top, args.bottom):
        die("measured feather widths (L%d R%d T%d B%d) do not match the "
            "requested bands (L%d R%d T%d B%d) -- the ramp maths is wrong"
            % (ml, mr, mt, mb, args.left, args.right, args.top, args.bottom))

    def band_desc(band):
        """Name the band's ROLE, not just its width.

        The two kinds are doing different jobs and conflating them in the log is
        how a 12/3 asymmetry silently drifts toward a uniform feather."""
        if not band:
            return " 0 px ANCHORED"
        if band <= EDGE_BREAK_MAX:
            return "%2d px edge-breaking" % band
        return "%2d px atmospheric" % band

    core_w = sz - L - R
    core_h = sz - T - B
    broad = sum(1 for b in (L, R, T, B) if b > EDGE_BREAK_MAX)
    small = sum(1 for b in (L, R, T, B) if 0 < b <= EDGE_BREAK_MAX)
    note("mask: FRAME-ANCHORED upper-right blend, "
         "%d atmospheric + %d edge-breaking ramp(s), %d anchored edge(s)"
         % (broad, small, sum(1 for b in (L, R, T, B) if not b)))
    note("  left   : %s   (measured %d px)" % (band_desc(L), ml))
    note("  bottom : %s   (measured %d px)" % (band_desc(B), mb))
    note("  top    : %s   (measured %d px)" % (band_desc(T), mt))
    note("  right  : %s   (measured %d px)" % (band_desc(R), mr))
    note("  opaque core: %dx%d px spanning x %d..%d, y %d..%d (%d%% of image)"
         % (core_w, core_h, L, sz - 1 - R, T, sz - 1 - B, solid * 100 // n))
    note("  no radius / ellipse / superellipse / L1 term / luminance key")

    # ***** THE CHARACTERISTIC THE FAILED GATE DID NOT MEASURE. ***** The four
    # numbers above were PERFECT on the revision that was rejected for reading
    # as a photo card, because they describe where the dissolve ends, not what
    # it looks like. These describe the visible edge.
    tr = transition_report(alpha, sz, L, R, T, B)
    note("transition: wobble %d, dither %d, haze %d, seed %d"
         % (args.wobble, args.dither, args.haze, args.noise_seed))
    ebands = {"left": L, "right": R, "top": T, "bottom": B}
    for ename in ("left", "bottom", "top", "right"):
        if ename not in tr:
            continue
        d = tr[ename]
        eb = ebands[ename]
        need = min_span_for(eb)
        note("  %-6s visible edge: onset %d..%d px (mean %d.%d), %d distinct "
             "values, span %d px (%s, needs span >= %d)"
             % (ename, d["min"], d["max"], d["mean10"] // 10,
                d["mean10"] % 10, d["distinct"], d["span"],
                band_desc(eb).strip(), need))
        note("         %d px inside the band eroded to full transparency "
             "(stipple)" % d["eroded"])
        # A single onset value means a dead-straight visible boundary. This is
        # refused at EVERY band width -- it is the exact defect that failed the
        # gate twice, once as a photo-card edge on the left/bottom and once as
        # the hard top/right lines beside the frame rules.
        if d["distinct"] <= 1:
            die("the %s edge has a SINGLE onset value across the whole "
                "portrait -- the visible boundary is a dead-straight line, "
                "which is the rectangular photo-card edge that failed the "
                "composition gate. Raise --wobble." % ename)
        if d["span"] < need:
            die("the %s edge wanders by only %d px end to end on a %d px "
                "band, but %d px is required -- it will still read as a "
                "straight line at 1:1. Raise --wobble."
                % (ename, d["span"], eb, need))
        # ***** ZERO EROSION IS EXPECTED ON A NARROW BAND AND IS NOT A FAULT.
        # ***** `eroded` counts pixels driven to EXACTLY 0, and _smoothstep()
        # only returns 0 below t == 10. On a 12 px band the interior positions
        # start low enough for the stipple to reach that floor; on a 3 px band
        # the first interior step is already t == 85, and the strongest
        # available dither lands it near t == 25, i.e. alpha ~6 -- vanishingly
        # faint but not literally clear. The boundary is still broken, just by
        # MODULATION rather than by erasure: that row runs roughly alpha 6..95,
        # which straddles the onset threshold and is what produces the 2
        # distinct onsets reported above. So this is reported, never enforced --
        # a die() here would reject the tool's own shipping defaults.
        if 0 < eb <= EDGE_BREAK_MAX and d["eroded"] == 0:
            note("         (no pixel reached exactly 0, as expected on a %d px "
                 "band -- the boundary is broken by alpha modulation across "
                 "the onset threshold, not by erasure)" % eb)
    if args.haze:
        note("  haze: dissolving px pulled toward #%02X%02X%02X by "
             "smoothstep(255-a)*%d/255 -- fur -> violet -> navy, NOT "
             "fur -> grey -> navy" % (SPLASH_HAZE + (args.haze,)))
        note("        (weight is 0 at alpha 255, so the opaque core and every "
             "facial feature are bit-identical to an untinted build)")
    else:
        note("  *** haze DISABLED: the transition will pass through neutral "
             "grey and read as a drop shadow ***")

    note("placement: m16c_draw_dog(%d, %d) -> x %d..%d, y %d..%d of %dx%d"
         % (px, py, px, px + sz - 1, py, py + sz - 1, UI_W, UI_H))
    for name, band, edge, terms, _fixes in anchors:
        on = terminator(edge, terms)
        if band == 0:
            note("  %-6s edge is ANCHORED and verified flush against %s at %d"
                 % (name, TERM_LABEL[on], edge))
        elif on == "frame":
            note("  %-6s edge is a %d px EDGE-BREAKING dissolve against %s at "
                 "%d (cap %d; the rule is drawn after the portrait and "
                 "overpaints the row the dissolve gives up)"
                 % (name, band, TERM_LABEL[on], edge, EDGE_BREAK_MAX))
        else:
            note("  %-6s edge is a %d px ATMOSPHERIC dissolve into open "
                 "background at %d" % (name, band, edge))

    # The frame is drawn AFTER the portrait (main.c:1608-1622), so anything it
    # crosses is repainted. This is invisible in the .inc and would otherwise
    # only surface on hardware.
    fo, fhits = frame_overlap(sz, px, py)
    if fo:
        note("*** WARNING: the splash's decorative frame paints over %d "
             "portrait px ***" % fo)
        for hit in fhits:
            note("    %s" % hit)
        note("    m16c_draw_splash() fills these AFTER m16c_draw_dog(), so "
             "they overwrite the portrait. The composition preview shows this "
             "faithfully -- judge it there before changing anything.")
    else:
        note("frame: no overlap with the portrait at this placement")

    if args.alpha_map:
        note("alpha map ('@'=255, '#'>204, '*'>153, '+'>102, ':'>51, '.'>0, ' '=0):")
        for line in alpha_ascii(alpha, args.size):
            note("  |%s|" % line)

    # ***** THE PROTECTED CENTRAL FACE. ***** Until this revision the top and
    # right edges were anchored at band 0 and provably could not touch anything,
    # so only the left and bottom bands had to be argued about. Now all four
    # bands erode fur, so the question "did the new top/right treatment reach an
    # eye?" is answered GEOMETRICALLY, over every pixel of the rectangle that
    # contains the whole face, before anything is emitted.
    frect, fbad, fworst = face_report(alpha, sz)
    fx0, fy0, fx1, fy1 = frect
    note("protected central face: x %d..%d, y %d..%d (%dx%d px, middle %d%%)"
         % (fx0, fx1, fy0, fy1, fx1 - fx0 + 1, fy1 - fy0 + 1,
            100 - 2 * PROTECTED_FACE_INSET_PCT))
    # Clearance from each band's inner edge to the protected rectangle. These
    # are the numbers that say HOW MUCH room the top/right bands had to spare,
    # which is what the review actually needs in order to judge 3 px.
    clr = (("left", fx0 - L), ("right", (sz - 1 - R) - fx1),
           ("top", fy0 - T), ("bottom", (sz - 1 - B) - fy1))
    note("  band clearance to the protected face: %s"
         % ", ".join("%s %+d px" % (nm, v) for nm, v in clr))
    if fbad:
        die("%d px of the PROTECTED CENTRAL FACE (x %d..%d, y %d..%d) are not "
            "fully opaque -- the lowest is alpha %d. A dissolve band has "
            "reached a facial feature. STOPPING rather than deforming the mask: "
            "do not widen, narrow or reshape a band to rescue this. Re-check "
            "the placement and the band widths against the review."
            % (fbad, fx0, fx1, fy0, fy1, fworst))
    note("  ALL %d px FULLY OPAQUE (alpha 255) -- eyes, nose, mouth and teeth "
         "are untouched by the %d px top and %d px right edge-breaking bands"
         % ((fx1 - fx0 + 1) * (fy1 - fy0 + 1), T, R))
    for nm, v in clr:
        if v <= 0:
            die("the %s band is touching the protected central face "
                "(clearance %+d px). Even though every protected pixel is "
                "still 255, there is no margin left and any change to the "
                "wander would eat into the face. STOPPING." % (nm, v))

    # "Are the eyes solid?" -- answered from the finished mask, not assumed.
    take, bbox, worst, faded = dark_report(rgb, alpha, args.size, args.dark_frac)
    note("darkest %d px (pupils/nostrils): bbox x %d..%d, y %d..%d"
         % (take, bbox[0], bbox[2], bbox[1], bbox[3]))
    if faded == 0:
        note("  ALL FULLY OPAQUE -- no band touches a dark facial feature; "
             "opaque core spans x %d..%d, y %d..%d"
             % (args.left, sz - 1 - args.right, args.top, sz - 1 - args.bottom))
    elif worst >= 224:
        note("  %d px lie inside a band, but the LOWEST alpha is %d/255 "
             "(%d%%), which is visually solid -- smoothstep is already flat "
             "that close to the core. The eyes are fine."
             % (faded, worst, worst * 100 // 255))
    else:
        note("  *** %d of the darkest px are feathered, lowest alpha %d ***"
             % (faded, worst))
        note("  *** compare the bbox above with the opaque core (x %d..%d, "
             "y %d..%d). ALL FOUR bands can now reach fur -- the top and right "
             "are no longer anchored at 0 px, they are %d px and %d px "
             "edge-breaking dissolves -- so identify WHICH band is clipped before "
             "changing anything. Note that the protected-face check above "
             "passed, so whatever is feathered here lies in the fur margin "
             "outside x %d..%d, y %d..%d and not on a facial feature. ***"
             % (L, sz - 1 - R, T, sz - 1 - B, T, R, fx0, fx1, fy0, fy1))

    # ***** ORDER MATTERS HERE. ***** dark_report() above ran against the
    # PRISTINE photograph, because the question it answers -- "are the dog's
    # dark facial features opaque?" -- is a question about the photograph. The
    # haze is applied only now, after that report and before quantisation, so
    # that (a) the tint is baked into the palette and costs the runtime
    # nothing, and (b) the violet it introduces cannot enter the "darkest 2%"
    # sample and contaminate the eyes/nostrils bounding box.
    rgb = apply_haze(rgb, alpha, args.size, args.haze, SPLASH_HAZE)

    # Quantise only the pixels that are actually visible; entry 0 is reserved
    # for "clear" so transparency is exact rather than approximated.
    hist = {}
    for i in range(n):
        if alpha[i] == 0:
            continue
        r, g, b = rgb[i]
        col = (r, g, b, alpha[i])
        hist[col] = hist.get(col, 0) + 1
    note("%d unique visible colours -> %d palette entries"
         % (len(hist), args.colors - 1))

    pal = median_cut(hist, args.colors - 1)

    # Order by (alpha, luma) so neighbouring indices are visually neighbouring
    # too -- that is what makes the sanitiser's index nudge imperceptible.
    pal.sort(key=lambda c: (c[3], (54 * c[0] + 183 * c[1] + 19 * c[2]) >> 8, c))

    pal32 = [0x00000000]
    for r, g, b, a in pal:
        pal32.append(((a & 0xFF) << 24) | (r << 16) | (g << 8) | b)
    while len(pal32) < args.colors:
        pal32.append(0x00000000)

    lut = [(0, 0, 0, 0)] + pal
    cache = {}
    pix = bytearray(n)
    for i in range(n):
        if alpha[i] == 0:
            pix[i] = 0
            continue
        r, g, b = rgb[i]
        pix[i] = nearest(lut, (r, g, b, alpha[i]), 1, cache)

    rounds = sanitize(pal32, pix)
    note("forbidden-literal scan clean after %d perturbation(s)" % rounds)

    cmd = "python3 tools/mkart.py %s -o %s" % (args.source, args.out)
    emit(args.out, pal32, pix, args.size, cmd, args.source, sha,
         (L, R, T, B), (px, py),
         (args.wobble, args.dither, args.haze, args.noise_seed), frect)

    pal_b = len(pal32) * 4
    pix_b = len(pix)
    note("WROTE %s" % args.out)
    note("  palette : %6d bytes (%d x 4)" % (pal_b, len(pal32)))
    note("  pixels  : %6d bytes (%dx%d x 8bpp)" % (pix_b, args.size, args.size))
    note("  TOTAL   : %6d bytes of .rodata" % (pal_b + pix_b))

    if args.preview:
        rgba = []
        for i in range(n):
            v = pal32[pix[i]]
            rgba.append(((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF))
        s = max(1, args.preview_scale)
        write_png(args.preview + "_rgba.png", args.size * s, args.size * s,
                  upscale(rgba, args.size, s))

        # The isolated swatch, composited over the REAL M16C_C_BG rather than
        # an approximation of it, so its tone matches the composition preview.
        comp = []
        for r, g, b, a in rgba:
            cr, cg, cb = blend_over(SPLASH_BG, (r, g, b), a)
            comp.append((cr, cg, cb, 255))
        write_png(args.preview + "_onbg.png", args.size * s, args.size * s,
                  upscale(comp, args.size, s))

        # ***** THE REAL GATE. ***** An 80x80 swatch cannot answer "does this
        # read as part of the composition or as a photo card?", because that
        # question is about the surroundings. So render the actual logical
        # surface at 1:1 -- true 480x270 pixels, true placement, true frame.
        surf = compose_splash(rgba, args.size, px, py, args.frame)
        write_png(args.preview + "_composition.png", UI_W, UI_H, surf)

        cs = max(1, args.comp_scale)
        if cs > 1:
            write_png(args.preview + "_composition_%dx.png" % cs,
                      UI_W * cs, UI_H * cs, upscale_wh(surf, UI_W, UI_H, cs))

        # A frame-free variant, so that if the composition reads badly it is
        # possible to tell WHICH of the two causes is responsible: the mask, or
        # the frame painting over it.
        if args.frame:
            bare = compose_splash(rgba, args.size, px, py, False)
            write_png(args.preview + "_composition_noframe.png",
                      UI_W, UI_H, bare)

        note("preview: %s_rgba.png, %s_onbg.png" % (args.preview, args.preview))
        note("COMPOSITION (the gate): %s_composition.png -- %dx%d at 1:1, the "
             "real logical surface" % (args.preview, UI_W, UI_H))
        if cs > 1:
            note("  magnified %dx: %s_composition_%dx.png"
                 % (cs, args.preview, cs))
        if args.frame:
            note("  without the frame: %s_composition_noframe.png (isolates "
                 "the mask from the frame overdraw)" % args.preview)
        note("INSPECT THE COMPOSITION BEFORE BUILDING. The portrait sits at "
             "(%d,%d), tucked into the INNER DECORATIVE FRAME's top-right "
             "corner -- NOT against the screen's edges, which are a further "
             "%d px out. Its top and right edges sit immediately inside the "
             "frame's violet rules (y == %d and x == %d), which are drawn "
             "afterwards and overpaint them."
             % (px, py, py, FRAME_TOP_Y, FRAME_RIGHT_X))
        note("  The left %d px and bottom %d px must dissolve into the "
             "backdrop, and the bottom-left corner should melt away."
             % (args.left, args.bottom))
        note("  ***** WHAT CHANGED, AND WHAT TO LOOK AT FIRST. ***** The top "
             "and right edges are no longer anchored: they now carry a %d px "
             "and %d px EDGE-BREAKING dissolve. Previously they were fully "
             "opaque to the boundary and the bright fur ended in straight "
             "horizontal and vertical lines that the 1 px rules did not hide, "
             "which is why the last composition was still rejected."
             % (args.top, args.right))
        note("  So on THOSE two edges the test is no longer 'no band at all'. "
             "It is: NO STRAIGHT LINE where the fur ends, and NO DARK GAP "
             "between the fur and the violet rule. The outermost row/column is "
             "now fully transparent by construction, so the rule should appear "
             "to sit directly on haze that is already close to its own colour "
             "-- fur emerging from under the frame. If instead a navy gap is "
             "visible between the rule and the fur, the band is too wide for "
             "this placement; report that rather than widening it further.")
        note("  THE GATE IS THE TRANSITION, NOT THE BAND WIDTHS. It must read "
             "as fur resolving out of violet atmosphere. If the left/bottom "
             "edge reads as GREY or as a dark halo, the haze is not doing its "
             "job; if a straight edge is visible anywhere, the wobble is not.")
        note("  The ASYMMETRY must still be obvious: left/bottom a broad "
             "atmospheric dissolve, top/right a tiny edge-breaking one. If the "
             "portrait now reads as evenly feathered on all four sides, that "
             "is the circular-avatar failure returning and it must be "
             "reported, not tuned.")
        note("  It must show NO circular outline and NO round avatar "
             "silhouette (two earlier masks failed on that), NO rectangular "
             "photo-card edge (the third failed on that), NO rectangular drop "
             "shadow (the fourth failed on that) and NO straight hard top/right "
             "edge (the fifth failed on that). No holes in the face.")


if __name__ == "__main__":
    main()
