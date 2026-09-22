#!/usr/bin/env python3
"""The M1 measurement instrument, validated at M0.

    python3 tools/sizereport.py build/m0diag.elf build/m0diag.bin \
            --map build/m0diag.map --budget 524288

Report section 16 requires eight figures before M1 can be assigned a colour.
This produces all of them, each labelled with the memory category from report
section 11 so that a number can never be read against the wrong budget.

THREE THINGS THIS EXISTS TO GET RIGHT
-------------------------------------

1. .text and .rodata reported SEPARATELY.
   LuaPSX's linker script folds *(.rodata) into the .text OUTPUT section, so
   after linking there is exactly one section and no tool can tell the two
   apart. runtime/linker.ld adds __text_end / __rodata_start / __rodata_end
   marker symbols for this reason. Without them the gba_cc_lut question at M1
   is unanswerable from the ELF.

2. The JIT-backed footprint is NOT the blob size, and it is the binding one.
   The Lua loader reserves align(__data_start, 0x4000) of JIT memory, split
   into adjacent 256KB mappings. That is larger than the blob, because the
   linker script leaves an unconditional alignment gap between the end of the
   image and __data_start. A build can therefore pass a blob-size check and
   still exceed the JIT reservation. The budget verdict below is taken against
   the JIT footprint.

3. Runtime memory is NOT in the ELF at all.
   The arena and the framebuffers are mmap'd at run time. They are report
   categories F/G/H, they cost nothing in the transferred image, and they are
   printed from DECLARED CONSTANTS in a clearly separated block. Folding them
   into the image total would overstate the image; omitting them entirely would
   understate real memory. Both mistakes are easy to make from a bare
   `readelf -SW`.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

JIT_PAGE = 0x4000                          # reservation alignment (mklua.py)
JIT_CHUNK = 0x40000                        # one JIT mapping caps at 256KB

# Mirrors runtime/core.h. Kept here as declared constants because these are
# runtime mmap sizes that never appear in the ELF -- see point 3 above.
# KEEP IN SYNC WITH core.h.
SCR_W, SCR_H = 1920, 1080
FB_SIZE = SCR_W * SCR_H * 4
FB_ALIGNED = (FB_SIZE + 0x1FFFFF) & ~0x1FFFFF
FB_TOTAL = FB_ALIGNED * 2

REQUIRED_SYMS = [
    "__text_start", "__text_end", "__rodata_start", "__rodata_end",
    "__rela_start", "__rela_end", "__data_load", "__data_start", "__data_end",
    "__got_start", "__got_end", "__bss_start", "__bss_end", "__image_end",
]


def kb(n):
    return "%.1f KB" % (n / 1024.0)


def row(label, value, category, note=""):
    return "  %-26s %10d  %-10s %-8s %s" % (label, value, kb(value), category, note)


# --------------------------------------------------------------- link map --

MAP_LINE = re.compile(
    r"^\s*(\.[\w.\-]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")
MAP_CONT = re.compile(
    r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")


def parse_map(path):
    """Per-object contribution, in bytes, keyed by (object, section-prefix).

    GNU ld wraps the line when the section name is long, so a continuation form
    is handled too. This is best-effort: the map is a convenience for spotting
    large contributors, never the authority for a total.
    """
    if not path or not os.path.exists(path):
        return None

    contrib = {}
    pending = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = MAP_LINE.match(line.rstrip("\n"))
            if m:
                sec, _addr, size, obj = m.group(1), m.group(2), int(m.group(3), 16), m.group(4)
                pending = None
                if size and not obj.startswith("0x"):
                    contrib.setdefault(obj, {}).setdefault(_bucket(sec), 0)
                    contrib[obj][_bucket(sec)] += size
                continue

            stripped = line.rstrip("\n")
            if re.match(r"^\s*\.[\w.\-]+\s*$", stripped):
                pending = stripped.strip()
                continue

            if pending:
                m2 = MAP_CONT.match(stripped)
                if m2:
                    size, obj = int(m2.group(2), 16), m2.group(3)
                    if size and not obj.startswith("0x"):
                        contrib.setdefault(obj, {}).setdefault(_bucket(pending), 0)
                        contrib[obj][_bucket(pending)] += size
                pending = None
    return contrib


def _bucket(sec):
    if sec.startswith(".text"):
        return "text"
    if sec.startswith(".rodata"):
        return "rodata"
    if sec.startswith(".data") or sec.startswith(".got"):
        return "data"
    if sec.startswith(".bss") or sec.startswith(".common"):
        return "bss"
    if sec.startswith(".rela"):
        return "rela"
    return "other"


# ------------------------------------------------------------------ main --

def main():
    ap = argparse.ArgumentParser(description="LUAport image size report")
    ap.add_argument("elf")
    ap.add_argument("binary", nargs="?")
    ap.add_argument("--map", dest="mapfile")
    ap.add_argument("--budget", type=int, default=524288)
    ap.add_argument("--arena", type=int, default=0,
                    help="runtime arena size in bytes, if one is mapped yet")
    args = ap.parse_args()

    try:
        e = Elf64(args.elf)
    except ElfError as ex:
        raise SystemExit("sizereport: %s" % ex)

    syms = e.symbol_values(REQUIRED_SYMS)
    missing = [n for n in REQUIRED_SYMS if n not in syms]
    if missing:
        raise SystemExit(
            "sizereport: missing linker symbols: %s\n"
            "  runtime/linker.ld must define the measurement markers."
            % ", ".join(missing))

    text = syms["__text_end"] - syms["__text_start"]
    rodata = syms["__rodata_end"] - syms["__rodata_start"]
    rela = syms["__rela_end"] - syms["__rela_start"]
    rela_n = rela // 24
    data = syms["__data_end"] - syms["__data_start"]
    got = syms["__got_end"] - syms["__got_start"]
    bss = syms["__bss_end"] - syms["__bss_start"]
    data_load = syms["__data_load"]
    data_start = syms["__data_start"]
    bss_end = syms["__bss_end"]

    blob = None
    if args.binary and os.path.exists(args.binary):
        blob = os.path.getsize(args.binary)

    jit_footprint = (data_start + JIT_PAGE - 1) & ~(JIT_PAGE - 1)
    jit_maps = (jit_footprint + JIT_CHUNK - 1) // JIT_CHUNK
    expected_blob = data_load + data
    gap = data_start - (blob if blob is not None else expected_blob)
    rw_image = bss_end - data_start

    print("=" * 78)
    print("LUAport image size report -- %s" % args.elf)
    print("=" * 78)
    print("  ELF type                   %s%s" % (
        e.type_name,
        "" if e.is_dyn else "   <-- EXPECTED DYN; see tools/check_image.sh"))
    print()

    print("IMAGE (measured from the ELF)")
    print("  %-26s %10s  %-10s %-8s %s" % ("component", "bytes", "", "cat", "note"))
    print(row(".text", text, "B", "executable, JIT-backed"))
    print(row(".rodata", rodata, "C", "in text PHDR -> JIT-backed"))
    print(row("relocation data", rela, "B",
              "%d R_X86_64_RELATIVE entries" % rela_n))
    print(row(".data", data, "D", "incl. %d B GOT" % got))
    print(row(".bss", bss, "E", "NOLOAD -- zero bytes in the blob"))
    print()

    print("TRANSFERRED PAYLOAD")
    if blob is not None:
        print(row("final binary size", blob, "A", "sent over TCP"))
        if blob != expected_blob:
            print("  note: blob (%d) != __data_load + .data (%d); objcopy may have"
                  % (blob, expected_blob))
            print("        trimmed or padded -- treat the file size as authoritative.")
    else:
        print(row("final binary (predicted)", expected_blob, "A",
                  "no .bin supplied"))
    print()

    print("JIT-BACKED RESERVATION  <-- the binding constraint, not the blob")
    print(row("__data_start", data_start, "-", "reservation runs to here"))
    print(row("JIT footprint", jit_footprint, "B",
              "align(__data_start, 0x%X)" % JIT_PAGE))
    print("  %-26s %10d  %-10s %-8s %s" % (
        "JIT mappings", jit_maps, "", "B",
        "of 0x%X; must land adjacent" % JIT_CHUNK))
    print(row("alignment gap", gap, "B",
              "unused padding inside the reservation"))
    if jit_footprint:
        print("  %-26s %9.1f%%  %-10s %-8s %s" % (
            "gap as share of footprint", 100.0 * gap / jit_footprint, "", "B",
            "MEASUREMENT TARGET ONLY -- not optimised at M0"))
    print()

    print("ORDINARY RW MEMORY")
    print(row("image RW requirement", rw_image, "D+E",
              ".data + .bss, mapped by _start"))
    print()

    print("RUNTIME ALLOCATIONS  (DECLARED CONSTANTS -- absent from the ELF)")
    print(row("framebuffers", FB_TOTAL, "H",
              "2 x %s aligned, direct memory" % kb(FB_SIZE)))
    if args.arena:
        print(row("shim arena", args.arena, "F", "malloc backing store"))
    else:
        print("  %-26s %10s  %-10s %-8s %s" % (
            "shim arena", "n/a", "", "F", "not mapped until Stage 1"))
    print("  GBA emulated memory (F) and ROM storage (G) are zero at M0:")
    print("  there is no emulator core and no ROM in this image.")
    print()

    print("BUDGET")
    print("  budget                     %10d  %s" % (args.budget, kb(args.budget)))
    print("  JIT footprint              %10d  %s" % (jit_footprint, kb(jit_footprint)))
    headroom = args.budget - jit_footprint
    if headroom >= 0:
        print("  headroom                   %10d  %s  (%.1f%% of budget used)"
              % (headroom, kb(headroom), 100.0 * jit_footprint / args.budget))
    else:
        print("  OVER BUDGET by             %10d  %s" % (-headroom, kb(-headroom)))
    if blob is not None:
        print("  (blob alone would report %d B of headroom -- the difference is"
              % (args.budget - blob))
        print("   the alignment gap, which is why the blob is not the gate.)")
    print()

    contrib = parse_map(args.mapfile)
    if contrib:
        print("LARGEST CONTRIBUTORS  (from the link map, best-effort)")
        totals = []
        for obj, buckets in contrib.items():
            image_bytes = buckets.get("text", 0) + buckets.get("rodata", 0) + \
                buckets.get("data", 0)
            totals.append((image_bytes, obj, buckets))
        totals.sort(reverse=True)
        print("  %-34s %8s %8s %8s %8s" % ("object", "text", "rodata", "data", "bss"))
        for image_bytes, obj, b in totals[:15]:
            if image_bytes == 0 and b.get("bss", 0) == 0:
                continue
            print("  %-34s %8d %8d %8d %8d" % (
                obj[-34:], b.get("text", 0), b.get("rodata", 0),
                b.get("data", 0), b.get("bss", 0)))
    elif args.mapfile:
        print("LARGEST CONTRIBUTORS: link map not found at %s" % args.mapfile)
    print()

    print("=" * 78)
    print("Categories: A transferred image | B JIT-backed | C .rodata (in B)")
    print("            D .data | E .bss | F emulated/heap | G ROM | H buffers")
    print("A-D are image cost. E-H are runtime cost. They are NOT the same")
    print("budget and must never be summed into a single total.")
    print("=" * 78)
    return 0


if __name__ == "__main__":
    sys.exit(main())
