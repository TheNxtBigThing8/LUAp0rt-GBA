#!/usr/bin/env python3
"""Incremental per-group size attribution for the M1 gpSP closure.

    python3 tools/m1stages.py build/m1

WHY THIS MEASURES OBJECTS AND NOT LINKS
---------------------------------------
The M1 implementation order calls for a measurement after each group is added:
fixture, +cpu, +main/gba_memory, +video, +sound, +serial/rfu/gbp, +cheats. The
obvious way to do that is to link after each group and read the image size --
and it does not work, because a partial object set has undefined symbols BY
CONSTRUCTION. gpsp/cpu.cc calls update_gba() which lives in gpsp/main.c, so
"cpu.o alone" is not a linkable image and never can be. Trying anyway would
either fail, or -- worse -- succeed with a relaxed link and report a number that
means nothing.

So the per-group figures here are read from the OBJECT FILES: allocated section
sizes summed per group, then accumulated. That is the honest way to attribute
cost per subsystem, and it is what makes a later YELLOW cleanup evidence-based
instead of speculative.

WHAT THESE NUMBERS ARE NOT
--------------------------
They are NOT the gate. Object-level sums differ from the linked image because
the linker merges string and constant pools, aligns sections, drops unreferenced
COMDAT/linkonce template instantiations, and adds .rela.dyn. The AUTHORITATIVE
number is the linked image from tools/sizereport.py, and only that one is
compared against the JIT budget.

Read this table for SHAPE -- which subsystem costs what, and where the surprise
is -- and read sizereport.py for the verdict.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHF_WRITE = 0x1
SHT_NOBITS = 8

# The groups, in the order the implementation plan adds them. The first entry is
# every LUAport-owned object -- the fixture, the unmodified shim, and the three
# adapters -- which is the baseline the gpSP groups are measured on top of.
GROUPS = [
    ("fixture only (LUAport)", [
        "m1main.o", "shim.o",
        "gba_fixture.o", "gba_filestream.o", "gba_compat.o",
    ]),
    ("+ cpu.cc  (ARM7TDMI interpreter)", ["gpsp_cpu.o"]),
    ("+ main.c + gba_memory.c", ["gpsp_main.o", "gpsp_gba_memory.o"]),
    ("+ video.cc  (DECISIVE STEP)", ["gpsp_video.o"]),
    ("+ sound.c", ["gpsp_sound.o"]),
    ("+ serial/serial_proto/rfu/gbp", [
        "gpsp_serial.o", "gpsp_serial_proto.o", "gpsp_rfu.o", "gpsp_gbp.o",
    ]),
    ("+ cheats.c", ["gpsp_cheats.o"]),
    # NOT optional. The plan called these "Tier 2" and expected to measure them
    # after Tier 1 had a number; the first link showed Tier 1 is not
    # link-complete without them. cpu.o/main.o/gba_memory.o/sound.o all contain
    # *_savestate functions that call the five BSON READERS, and those readers
    # are defined only in savestate.c. savestate.c in turn calls
    # input_*_savestate, which is input.c. With whole-object linking and no
    # --gc-sections that cost is unavoidable, so it belongs INSIDE the total --
    # but on its own row, so the price of the BSON closure stays visible.
    ("+ savestate.c + input.c (BSON closure)",
     ["gpsp_savestate.o", "gpsp_input.o"]),
]


def classify(sec):
    """Bucket one section the way tools/sizereport.py does."""
    name = sec["name"]
    if not (sec["flags"] & SHF_ALLOC):
        return None                      # debug/comment: not in the image
    if sec["type"] == SHT_NOBITS:
        return "bss"
    if sec["flags"] & SHF_EXECINSTR:
        return "text"
    if sec["flags"] & SHF_WRITE:
        return "data"                    # includes .data.rel.ro
    if name.startswith(".rodata") or name.startswith(".rel"):
        return "rodata"
    return "rodata"


def measure(path):
    """{'text','rodata','data','bss'} allocated bytes in one object."""
    try:
        e = Elf64(path)
    except ElfError as ex:
        raise SystemExit("m1stages: %s" % ex)

    out = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    for sec in e.sections:
        bucket = classify(sec)
        if bucket:
            out[bucket] += sec["size"]
    return out


def add(a, b):
    return {k: a[k] + b[k] for k in a}


def row(label, g, cum, ro_delta):
    return ("  %-34s %9d %9d %9d %10d  | %9d %9d"
            % (label, g["text"], g["rodata"], g["data"], g["bss"],
               cum["text"] + cum["rodata"], ro_delta))


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build/m1"

    print("=" * 104)
    print("M1 INCREMENTAL SIZE -- per group, measured from the OBJECT FILES")
    print("=" * 104)
    print("  These are object-level sums, NOT the linked image. The linked")
    print("  image from tools/sizereport.py is the authoritative gate number;")
    print("  this table exists to attribute cost per subsystem.")
    print()
    print("  %-34s %9s %9s %9s %10s  | %9s %9s"
          % ("group", "text", "rodata", "data", "bss",
             "cum RO", "delta RO"))
    print("  " + "-" * 100)

    cum = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    prev_ro = 0
    missing = []

    for label, objs in GROUPS:
        g = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
        for o in objs:
            p = os.path.join(build, o)
            if not os.path.exists(p):
                missing.append(p)
                continue
            g = add(g, measure(p))
        cum = add(cum, g)
        ro = cum["text"] + cum["rodata"]
        print(row(label, g, cum, ro - prev_ro))
        prev_ro = ro

    total = dict(cum)
    print("  " + "-" * 100)
    print(row("M1 CLOSURE TOTAL (objects)", total, total, 0))

    if missing:
        print()
        print("  NOT BUILT (excluded from the sums above):")
        for p in sorted(set(missing)):
            print("    %s" % p)

    print()
    print("  cum RO   = cumulative text+rodata across groups (object-level)")
    print("  delta RO = what THIS group added to it")
    print()
    print("  The gate is RO_end from the LINKED image:")
    print("    RO_end = .text + .rodata + .rela.dyn + alignment")
    print("    __data_start  = ALIGN(RO_end, 0x10000) + 0x10000")
    print("    JIT footprint = ALIGN(__data_start, 0x4000)")
    print("  GREEN  RO_end <= 393216      (>= one spare 64 KB block)")
    print("  CEILING RO_end <= 458752     (JIT footprint exactly 524288)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
