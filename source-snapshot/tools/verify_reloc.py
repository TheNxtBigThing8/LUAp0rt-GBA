#!/usr/bin/env python3
"""Prove the relocation table is safe to apply BEFORE sending to hardware.

    python3 tools/verify_reloc.py build/m0diag.elf

Adapted from LuaPSX/tools/verify_reloc.py. The checks and the verdict are
unchanged; the ELF is now parsed directly (tools/elf.py) instead of by shelling
out to `readelf`, so the check runs anywhere Python does and does not depend on
readelf's column layout.

Every console run costs a game relaunch, so a fault that could have been
predicted from the ELF is an expensive way to learn something.

The specific hazard this checks for:

  The payload executes from a PROT_READ|PROT_EXECUTE JIT mapping. Only the
  region from __data_start upward is the anonymous RW memory that _start maps.
  So a relocation whose r_offset lands BELOW __data_start is a write into
  read-only memory -- it would fault exactly like the unrelocated-pointer bug
  it was meant to fix, just later and more confusingly.

  linker.ld keeps *(.rela.rodata*) among others, and .rodata lives in the
  read-only mapping, so this is a real possibility rather than a theoretical
  one.

Also verifies that every entry is R_X86_64_RELATIVE, since _start can only
apply that type -- anything else needs symbol resolution a flat image cannot
do, and would be silently skipped.

AN EMPTY TABLE IS NOT AUTOMATICALLY A FAILURE
---------------------------------------------

Until M12A every image carried initialised pointer tables, so "zero entries"
could only mean .rela.dyn had been dropped, and this tool failed outright on it.
M12A is the first image with no initialised pointer at all: three objects, whose
.data is nothing but shim.c's two static FILE slots -- two ints, an fd, a file
position and a 4KB byte buffer, not one address among them. Zero entries is the
arithmetically correct output there, and failing it was a false positive.

But "zero because nothing needed relocating" and "zero because the table was
dropped" must stay distinguishable, so the empty case is now decided from the
INPUT OBJECTS instead of assumed either way:

    python3 tools/verify_reloc.py build/m12/m12gpsp.elf build/m12/m12main.o ...

An input object requires a dynamic relocation when it holds an ABSOLUTE-address
relocation (R_X86_64_64, or the 32-bit forms) against an ALLOCATABLE section.
That is what an initialised pointer compiles to, and -static-pie turns each one
into the R_X86_64_RELATIVE that _start applies. Both qualifiers carry weight:

  * PC-relative types (PC32, PLT32, GOTPCREL) fill .rela.text in every ordinary
    object and are resolved entirely at link time, producing no dynamic entry.
    Counting those would fail every image ever built, M12A included.
  * non-allocatable sections -- debug info above all -- carry absolute
    relocations that are never loaded, so they cannot need fixing up at run
    time. The current flags emit none, but the filter means a later -g cannot
    turn this check into a false alarm.

The GOT is deliberately NOT consulted here. GOT slots also hold link-time
addresses, but boot.inc relocates them itself -- it adds the image base to every
non-zero slot in __got_start..__got_end before it ever reads the relocation
table -- so their correctness does not depend on .rela.dyn, and "GOT slots exist
but the table is empty" is a legitimate state rather than evidence of a drop.

So: empty table + no input object needing one = safe. Empty table + an input
object that DID need one = the table really was dropped, and that still fails
exactly as before. With no object list supplied the question cannot be decided
from evidence, and an empty table still fails -- which leaves the M0-M11 call
sites, none of which pass objects, precisely as strict as they were.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

R_X86_64_RELATIVE = 8

SHT_RELA = 4
SHF_ALLOC = 0x2
RELA_ENTSIZE = 24

# The relocation types that mean "an absolute address is stored here". These are
# the ones -static-pie must convert into R_X86_64_RELATIVE; every other type an
# object carries is PC-relative and is finished at link time.
ABS_TYPES = {
    1:  "R_X86_64_64",
    10: "R_X86_64_32",
    11: "R_X86_64_32S",
}


def absolute_relocs(obj_path):
    """Absolute-address relocations against allocatable sections of one object.

    Returns (total, [(target_section, type_name, count), ...]).

    Reuses tools/elf.py's already-parsed section table rather than introducing a
    second ELF reader; only the 8-byte r_info field of each Rela entry is read
    here, because elf.py locates relocations by virtual address and an input .o
    is ET_REL, where section addresses are all zero.
    """
    e = Elf64(obj_path)
    total = 0
    found = []
    for sec in e.sections:
        if sec["type"] != SHT_RELA or not sec["size"]:
            continue
        if sec["info"] >= len(e.sections):
            continue
        target = e.sections[sec["info"]]
        if not (target["flags"] & SHF_ALLOC):
            continue
        counts = {}
        for i in range(sec["size"] // RELA_ENTSIZE):
            off = sec["offset"] + i * RELA_ENTSIZE
            r_info = struct.unpack_from("<Q", e.blob, off + 8)[0]
            rtype = r_info & 0xFFFFFFFF
            if rtype in ABS_TYPES:
                counts[rtype] = counts.get(rtype, 0) + 1
        for rtype in sorted(counts):
            total += counts[rtype]
            found.append((target["name"], ABS_TYPES[rtype], counts[rtype]))
    return total, found


def report_empty_table(objs):
    """Decide an empty .rela.dyn from the input objects. Returns an exit code."""
    print("checks")
    print("  entries parsed             : 0")

    if not objs:
        print("  input objects supplied     : none")
        print("  FAIL: the image has NO relocations, and with no object list")
        print("        this cannot tell whether nothing required relocating or")
        print("        .rela.dyn was dropped. Pass the link's object list to")
        print("        decide it.")
        print()
        print("VERDICT: NOT SAFE -- no relocation table, nothing to check it against")
        return 1

    total = 0
    detail = []
    for o in objs:
        try:
            n, found = absolute_relocs(o)
        except ElfError as ex:
            raise SystemExit("verify_reloc: %s" % ex)
        total += n
        for name, tname, cnt in found:
            detail.append((o, name, tname, cnt))

    print("  input objects inspected    : %d" % len(objs))

    if total:
        print("  input relocations required : YES (%d)" % total)
        print("  FAIL: the input objects hold %d absolute-address relocation(s)"
              % total)
        print("        against allocatable sections, but the linked image has NO")
        print("        relocation table. They were required before the final link")
        print("        and disappeared during it, so every one of those pointers")
        print("        reads back as a link-time offset.")
        for o, name, tname, cnt in detail[:12]:
            print("          %-30s %-18s %-14s %d" % (o, name, tname, cnt))
        print()
        print("VERDICT: NOT SAFE -- relocations were required but are missing")
        return 1

    print("  input relocations required : no")
    print("  no input object stores an absolute address in an allocatable")
    print("  section, so there is no initialised pointer to fix up and an empty")
    print("  table is the correct result rather than a dropped one.")
    print()
    print("VERDICT: safe -- relocation-free image; no input object requires relocation")
    return 0


def main():
    argv = sys.argv[1:]
    elf_path = argv[0] if argv else "build/m0diag.elf"
    # Optional: the object list from the link. Only consulted when the final
    # image has an empty table, so passing objects can never relax a check that
    # runs on a populated one.
    objs = argv[1:]

    try:
        e = Elf64(elf_path)
    except ElfError as ex:
        raise SystemExit("verify_reloc: %s" % ex)

    need = ["__rela_start", "__rela_end", "__data_start", "__bss_end"]
    syms = e.symbol_values(need + ["__data_end", "__bss_start"])
    missing = [n for n in need if n not in syms]
    if missing:
        raise SystemExit("missing linker symbols: %s" % ", ".join(missing))

    rela_start = syms["__rela_start"]
    rela_end = syms["__rela_end"]
    data_start = syms["__data_start"]
    bss_end = syms["__bss_end"]

    print("image layout")
    print("  __rela_start  0x%08X" % rela_start)
    print("  __rela_end    0x%08X  (%d bytes, %d entries)"
          % (rela_end, rela_end - rela_start, (rela_end - rela_start) // 24))
    print("  __data_start  0x%08X   <- writable region begins here" % data_start)
    print("  __bss_end     0x%08X" % bss_end)
    print()

    if rela_end == rela_start:
        return report_empty_table(objs)

    try:
        entries = e.relocations(rela_start, rela_end)
    except ElfError as ex:
        raise SystemExit("verify_reloc: %s" % ex)

    bad_type = [x for x in entries if x[1] != R_X86_64_RELATIVE]
    below = [x for x in entries if x[0] < data_start]
    above = [x for x in entries if x[0] >= bss_end]

    print("checks")
    print("  entries parsed            : %d" % len(entries))

    ok = True

    if bad_type:
        ok = False
        kinds = sorted({x[1] for x in bad_type})
        print("  FAIL non-RELATIVE types   : %d entries, types %s"
              % (len(bad_type), kinds))
        print("        _start skips these, so those pointers stay unrelocated.")
    else:
        print("  all R_X86_64_RELATIVE     : yes")

    if below:
        ok = False
        print("  FAIL targets below __data_start : %d" % len(below))
        print("        These are writes into the READ-ONLY JIT mapping and")
        print("        would fault. Sample:")
        for x in below[:8]:
            print("          offset 0x%08X  addend 0x%08X  in %s"
                  % (x[0], x[2], e.section_of(x[0])))
    else:
        print("  all targets writable      : yes (>= __data_start)")

    if above:
        ok = False
        print("  FAIL targets past __bss_end : %d" % len(above))
    else:
        print("  all targets in range      : yes (< __bss_end)")

    # Addends should point somewhere inside the image; a wild addend means the
    # pointer would be relocated to nonsense rather than left broken.
    wild = [x for x in entries if x[2] < 0 or x[2] >= bss_end]
    if wild:
        ok = False
        print("  FAIL addends outside image: %d" % len(wild))
        for x in wild[:5]:
            print("          offset 0x%08X  addend 0x%08X" % (x[0], x[2]))
    else:
        print("  all addends inside image  : yes")

    # Where do the targets live? Useful sanity signal: they should cluster in
    # .data / .data.rel.ro, which is what initialised pointer tables are.
    print()
    print("target distribution")
    buckets = {}
    for x in entries:
        sec = e.section_of(x[0])
        buckets[sec] = buckets.get(sec, 0) + 1
    for nm in sorted(buckets, key=lambda k: -buckets[k]):
        print("  %-24s %d" % (nm, buckets[nm]))

    print()
    print("VERDICT: %s" % ("safe to apply" if ok else "NOT SAFE -- would fault"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
