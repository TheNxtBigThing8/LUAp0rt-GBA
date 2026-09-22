#!/usr/bin/env python3
"""LUAport M6 -- tools/m6_baseline.py

OFFLINE ONLY. THIS FILE IS NEVER COMPILED, NEVER LINKED AND NEVER UPLOADED.
It is not referenced by any build target and contributes zero bytes to the M6
image. It exists solely so the Tier B expected values can be derived
INDEPENDENTLY of the fixture that reports the measured ones.

WHAT IT IS FOR
--------------
apps/m6gpsp/main.c REPORTS an FNV-1a hash of the 76,800 visible framebuffer
bytes and never compares it against anything. Promoting that number to an
assertion requires deriving the same number from the reference image WITHOUT
running gpSP, which is what this script does:

    baseline_0000.png -> RGB888 -> quantise DOWN to GBA 5-bit -> BGR555
                      -> gpSP convert_palette() -> RGB565 -> LE bytes -> FNV-1a

If the number this script prints equals the number the console prints, the two
emulators agree pixel-for-pixel and Tier B can be promoted. If they differ, that
is a FINDING TO DIAGNOSE, NOT A NUMBER TO EDIT -- see the note at the bottom.

WHY QUANTISE *DOWN* AND NOT EXPAND UP
-------------------------------------
A byte-identical RGB888 comparison is invalid. gpSP's green occupies bits 10-6
with bit 5 forced to zero, so a naive 565->888 expansion that reads six green
bits produces a different value than mGBA's 5-bit-derived green. Going the other
way -- 888 down to 555 by >>3 -- is EXACT provided mGBA's colours are themselves
5-bit derived, which step 2 below CHECKS rather than assumes.

WHY NO PILLOW
-------------
Standard library only (zlib + struct). A Tier B derivation that cannot run
because a third-party package is missing is worse than no tool at all.

USAGE
    python tools/m6_baseline.py
    python tools/m6_baseline.py --png <path> [--compare 0xXXXXXXXX]
"""

import argparse
import os
import struct
import sys
import zlib

DEFAULT_PNG = os.path.join("mgba", "cinema", "gba", "obj", "2d-wrap",
                           "baseline_0000.png")

GBA_W, GBA_H = 240, 160

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF


# --------------------------------------------------------------- PNG decode --

def decode_png(path):
    """Return (width, height, rows) where rows is a list of bytearrays of RGB888.

    Supports exactly what the baseline is: bit depth 8, colour type 2
    (truecolour, no alpha), no interlacing. Anything else is rejected loudly
    rather than silently mis-decoded.
    """
    with open(path, "rb") as fh:
        data = fh.read()

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file: %s" % path)

    pos = 8
    width = height = depth = colour = interlace = None
    idat = bytearray()

    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length          # 4 len + 4 type + body + 4 crc

        if ctype == b"IHDR":
            (width, height, depth, colour, _comp, _filt,
             interlace) = struct.unpack(">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if width is None:
        raise ValueError("no IHDR chunk")
    if depth != 8 or colour != 2:
        raise ValueError(
            "unsupported PNG: bit depth %d colour type %d "
            "(this tool handles 8-bit truecolour RGB only)" % (depth, colour))
    if interlace != 0:
        raise ValueError("interlaced PNG is not supported")

    raw = zlib.decompress(bytes(idat))

    bpp = 3                                    # RGB888
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    off = 0

    for _y in range(height):
        ftype = raw[off]
        off += 1
        line = bytearray(raw[off:off + stride])
        off += stride

        if ftype == 0:                          # None
            pass
        elif ftype == 1:                        # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:                        # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:                        # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:                        # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                line[i] = (line[i] + pr) & 0xFF
        else:
            raise ValueError("unknown PNG filter type %d" % ftype)

        rows.append(line)
        prev = line

    return width, height, rows


# ------------------------------------------------------- the gpSP transform --

def convert_palette(v):
    """gpsp/common.h:108-109, the branch taken when USE_XBGR1555_FORMAT is NOT
    defined -- which is the case for every LUAport build (the only gpSP defines
    in the Makefile are -DINLINE=inline -DNDEBUG and -DROM_BUFFER_SIZE=2).

        (((v & 0x1F) << 11) | ((v & 0x03E0) << 1) | ((v >> 10) & 0x1F))

    Input is GBA-native BGR555; output is RGB565 with bit 5 PERMANENTLY ZERO.
    """
    return (((v & 0x1F) << 11) | ((v & 0x03E0) << 1) | ((v >> 10) & 0x1F))


def rgb888_to_bgr555(r, g, b):
    """Quantise DOWN into the GBA's native 5-bit-per-channel domain."""
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def fnv1a32(data):
    h = FNV_OFFSET
    for byte in data:
        h ^= byte
        h = (h * FNV_PRIME) & MASK32
    return h


def is_5bit_derived(value):
    """True if an 8-bit channel could have come from a 5-bit source.

    Two expansions are in common use and both are accepted, because which one
    mGBA used is an implementation detail we must not assume:
        (v << 3) | (v >> 2)     -> 31 becomes 255, 1 becomes 8
        round(v * 255 / 31)     -> 31 becomes 255, 1 becomes 8 (differs midway)
    """
    q = value >> 3
    return value in ((q << 3) | (q >> 2), (q * 255 + 15) // 31)


# ---------------------------------------------------------------------- main --

def main():
    ap = argparse.ArgumentParser(
        description="Derive M6's expected framebuffer hash from the mGBA "
                    "baseline image. OFFLINE ONLY -- never linked.")
    ap.add_argument("--png", default=DEFAULT_PNG,
                    help="baseline PNG (default: %s)" % DEFAULT_PNG)
    ap.add_argument("--compare", default=None,
                    help="a hash reported by the console, e.g. 0x1234ABCD")
    ap.add_argument("--dump", default=None,
                    help="write the derived raw u16 framebuffer here")
    args = ap.parse_args()

    if not os.path.exists(args.png):
        print("ERROR: no such file: %s" % args.png)
        return 2

    width, height, rows = decode_png(args.png)
    print("PNG            : %s" % args.png)
    print("dimensions     : %dx%d (require %dx%d)" %
          (width, height, GBA_W, GBA_H))

    if (width, height) != (GBA_W, GBA_H):
        print("ERROR: the baseline is not 240x160; refusing to derive.")
        return 2

    # ---- step 2: is this image losslessly representable in the GBA domain? --
    #
    # REPORTED, NOT ASSUMED. If any channel is not 5-bit derived then the
    # down-quantisation is lossy and an exact hash comparison is NOT a valid
    # test -- which is a finding in itself, and one worth knowing BEFORE
    # promoting Tier B rather than after a red build.
    bad = 0
    for row in rows:
        for value in row:
            if not is_5bit_derived(value):
                bad += 1
    print("5-bit derived  : %s (%d of %d channel samples are not)" %
          ("YES" if bad == 0 else "NO", bad, width * height * 3))
    if bad:
        print("WARNING: the baseline is NOT losslessly representable in the")
        print("         GBA 5-bit domain. An exact hash comparison is invalid;")
        print("         a tolerance-based comparison is required instead.")

    # ---- steps 3-5: quantise, convert, serialise ---------------------------
    out = bytearray()
    distinct = {}
    zero = 0
    for y in range(height):
        row = rows[y]
        for x in range(width):
            r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            px = convert_palette(rgb888_to_bgr555(r, g, b))
            if px == 0:
                zero += 1
            distinct[px] = distinct.get(px, 0) + 1
            out += struct.pack("<H", px)        # LITTLE-ENDIAN, matching the
                                                # payload's in-memory byte order

    assert len(out) == GBA_W * GBA_H * 2

    # ---- step 6: the hash --------------------------------------------------
    h = fnv1a32(out)

    print("")
    print("EXPECTED visible hash : 0x%08X  (FNV-1a over %d LE bytes)"
          % (h, len(out)))
    print("zero (0x0000) pixels  : %d" % zero)
    print("distinct values       : %d" % len(distinct))
    print("")
    print("value histogram (most common first):")
    for px, n in sorted(distinct.items(), key=lambda kv: -kv[1])[:12]:
        print("    0x%04X  %7d  (%5.2f%%)" % (px, n, 100.0 * n / (GBA_W * GBA_H)))

    # ---- step 7: representative pixels, matching the fixture's step 117 ----
    print("")
    print("representative pixels (same coordinates the fixture reports):")
    for (x, y) in ((0, 0), (119, 59), (120, 60), (127, 63),
                   (120, 94), (127, 97), (120, 80), (239, 159)):
        row = rows[y]
        r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
        px = convert_palette(rgb888_to_bgr555(r, g, b))
        print("    (%3d,%3d) RGB888 %02X%02X%02X -> 0x%04X" % (x, y, r, g, b, px))

    if args.dump:
        with open(args.dump, "wb") as fh:
            fh.write(out)
        print("")
        print("raw derived framebuffer written to %s (%d bytes)"
              % (args.dump, len(out)))

    if args.compare is not None:
        want = int(args.compare, 0) & MASK32
        print("")
        print("console reported : 0x%08X" % want)
        print("derived expected : 0x%08X" % h)
        if want == h:
            print("MATCH -- gpSP agrees with the mGBA baseline pixel for pixel.")
            print("Tier B may be promoted to a hard assertion.")
            return 0
        print("MISMATCH.")
        print("")
        print("THIS IS A FINDING TO DIAGNOSE, NOT A NUMBER TO EDIT.")
        print("The baseline is mGBA's output and this is specifically mGBA's")
        print("2d-wrap OBJ corner-case test. gpSP is an independent emulator")
        print("and may legitimately differ on OBJ tile-index wrapping, which")
        print("would be an EMULATOR BEHAVIOUR DIFFERENCE and not a LUAport")
        print("framebuffer integration fault. Compare the per-pixel dumps")
        print("(--dump) before concluding anything about M6.")
        return 1

    print("")
    print("Tier B is NOT promoted by this run. Compare against the console's")
    print("reported hash with:  --compare 0x<hash from the M6 notification>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
