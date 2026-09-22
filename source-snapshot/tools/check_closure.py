#!/usr/bin/env python3
"""Prove the M0 dependency closure is actually closed.

    python3 tools/check_closure.py build/main.o build/shim.o build/gfx.o

The approved M0 plan says the runtime must be provable independent of the PSX
emulator. "It compiled" is weaker evidence than it looks: the build could pick
up an emulator symbol through a header, or leave a dependency undefined that the
linker happens to satisfy from somewhere unexpected later.

Two checks, both read straight from the object files:

  1. LEAKAGE -- any symbol, defined or referenced, whose name belongs to an
     emulator core rather than to a platform runtime. At M0 the answer must be
     zero: there is no emulator in this image at all.

  2. UNRESOLVED SYMBOLS -- everything the closure AS A WHOLE still expects
     somebody else to provide.

CLOSURE IS A PROPERTY OF THE SET, NOT OF EACH OBJECT
----------------------------------------------------
The first version of this tool got check 2 wrong. It gathered the undefined
symbols of every object, subtracted only the linker-script symbols, and reported
the rest as unresolved. That flags every ordinary cross-object call: main.o
references klog and shim.o defines it, which is exactly what linking is for. The
checker failed while the real link succeeded -- and when a tool disagrees with
the linker, the tool is what is wrong.

The correct question is not "does this object have undefined symbols" but "does
anything in the closure still need a definition that no member of the closure,
and no linker-script symbol, supplies". So:

    unresolved = undefined(all objects)
                 - defined_globally(all objects)
                 - linker-script symbols

Two details that matter for that subtraction to be accurate:

  * Only NON-LOCAL definitions can satisfy another object's reference. A static
    function in shim.c is STB_LOCAL and invisible to main.o, so counting it as a
    provider would hide a genuine missing symbol behind a same-named local.

  * SHN_COMMON and SHN_ABS entries are definitions. A tentative definition
    (SHN_COMMON) has a non-zero st_shndx but is not a normal section index; a
    naive `shndx != 0` test happens to get this right, and this file keeps it
    right explicitly so the intent survives the next edit.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

SHN_UNDEF = 0
SHN_ABS = 0xFFF1
SHN_COMMON = 0xFFF2

STB_LOCAL = 0
STB_WEAK = 2

STT_SECTION = 3
STT_FILE = 4

# Provided by runtime/linker.ld, so they are legitimately undefined in a .o.
LINKER_SYMS = {
    "__text_start", "__text_end", "__rodata_start", "__rodata_end",
    "__rela_start", "__rela_end", "__data_load", "__data_start", "__data_end",
    "__got_start", "__got_end", "__bss_start", "__bss_end", "__image_end",
}

# Substrings that indicate emulator-core code rather than platform runtime.
# Deliberately broad: a false positive costs one line of review, a false
# negative costs the entire premise of M0.
#
# PSX patterns are ALWAYS leakage, for every core. LUAport was extracted from
# LuaPSX and nothing in any LUAport milestone may contain PlayStation code.
PSX_PATTERNS = [
    "psx", "pcsx", "r3000", "psxmem", "psxbios", "psxdma", "psxhw",
    "spu_", "cdrom", "cdriso", "mdec", "gte", "gpulib", "gpu_",
    "rearmed", "dfsound", "dfxvideo",
]

# Emulator-core patterns, keyed by which core a build is ALLOWED to contain.
#
# --------------------------------------------------------------------------
# WHY THIS IS PARAMETERISED (added at M1)
# --------------------------------------------------------------------------
# The original tool hard-coded "gpsp", "gba_", "arm7", "tdmi" and "mgba" into a
# single leak list. That is exactly right for M0, whose entire premise is that
# the platform runtime contains no emulator at all -- and it must KEEP being
# right for M0, which is why `--core none` remains the default and M0's
# invocation in the Makefile is unchanged.
#
# It is wrong for M1 by construction: M1's whole purpose is to link the gpSP
# interpreter closure and measure it, so every gpSP symbol in that image is
# intentional. Left as it was, the tool would fail M1 by design and the failure
# would carry no information.
#
# So the question the tool asks becomes core-specific: "does this image contain
# emulator code it is not supposed to contain?" For M0 the allowed set is empty.
# For M1 gpSP/GBA symbols are allowed and everything else -- PSX, and mGBA --
# is still leakage.
#
# THE STRICTNESS IS NOT REDUCED, IT IS REDIRECTED. What M1 gains in gpSP
# tolerance it more than gives back in tools/check_forbidden.py, which rejects
# the dynarec and libretro-frontend symbol sets outright.
CORE_PATTERNS = {
    "gpsp": ["gpsp", "gba_", "arm7", "tdmi"],
    "mgba": ["mgba"],
}

# All emulator-core patterns, for the default "no core is allowed" mode.
ALL_CORE_PATTERNS = [p for pats in CORE_PATTERNS.values() for p in pats]

# Names that contain a pattern but are legitimate platform runtime.
LEAK_ALLOW = {
    "clear_fb", "blit_ui",          # 'fb'/'ui' are fine; kept for clarity
}


def leak_patterns(core):
    """Patterns that count as leakage for a build allowed to contain `core`.

    core is None (or "none") for a pure platform runtime such as M0, in which
    case EVERY emulator pattern is leakage -- the original behaviour, preserved
    exactly.
    """
    if core in (None, "none"):
        return PSX_PATTERNS + ALL_CORE_PATTERNS

    if core not in CORE_PATTERNS:
        raise SystemExit(
            "check_closure: unknown core %r (known: %s, none)"
            % (core, ", ".join(sorted(CORE_PATTERNS))))

    allowed = set(CORE_PATTERNS[core])
    return PSX_PATTERNS + [p for p in ALL_CORE_PATTERNS if p not in allowed]


def scan(objs, patterns):
    """Read every object once. Nothing is judged here, only recorded."""
    leaks = []
    undef = {}          # name -> [objects that reference it]
    defined = {}        # name -> [objects that define it, non-local only]
    defined_local = {}  # name -> [objects with a LOCAL definition]
    weak_undef = set()  # undefined AND weak in every object that wants it
    strong_undef = set()

    for path in objs:
        try:
            e = Elf64(path)
        except ElfError as ex:
            raise SystemExit("check_closure: %s" % ex)

        for s in e.symbols():
            name = s["name"]
            if not name:
                continue
            # STT_FILE / STT_SECTION entries are bookkeeping, not dependencies.
            if s["type"] in (STT_SECTION, STT_FILE):
                continue

            shndx = s["shndx"]
            bind = s["bind"]

            if shndx == SHN_UNDEF:
                undef.setdefault(name, []).append(path)
                if bind == STB_WEAK:
                    weak_undef.add(name)
                else:
                    strong_undef.add(name)
            else:
                # SHN_ABS / SHN_COMMON are definitions just as much as a real
                # section index is. Only non-local definitions are visible to
                # the other objects in the closure.
                if bind == STB_LOCAL:
                    defined_local.setdefault(name, []).append(path)
                else:
                    defined.setdefault(name, []).append(path)

            if name in LEAK_ALLOW:
                continue
            low = name.lower()
            for pat in patterns:
                if pat in low:
                    leaks.append((name, path, pat))
                    break

    return {
        "leaks": leaks,
        "undef": undef,
        "defined": defined,
        "defined_local": defined_local,
        "weak_undef": weak_undef,
        "strong_undef": strong_undef,
    }


def resolve(scanned):
    """Apply the closure subtraction. This is the whole point of the tool."""
    undef = scanned["undef"]
    defined = scanned["defined"]

    internal = {}     # name -> (referencing objects, defining objects)
    unresolved = {}   # name -> referencing objects

    for name, refs in undef.items():
        if name in defined:
            internal[name] = (refs, defined[name])
        elif name in LINKER_SYMS:
            pass                       # supplied by runtime/linker.ld
        else:
            unresolved[name] = refs

    # A weak undefined symbol resolves to 0 and does not fail a link. Report it
    # so it is never invisible, but do not call the closure broken over it.
    weak_only = {n: v for n, v in unresolved.items()
                 if n in scanned["weak_undef"] and n not in scanned["strong_undef"]}
    for n in weak_only:
        del unresolved[n]

    edges = sum(len(refs) for refs, _ in internal.values())
    return internal, unresolved, weak_only, edges


def main():
    # Deliberately hand-parsed rather than argparse: this tool is invoked from
    # the Makefile with a bare object list and that call must keep working
    # unchanged, so --core is an optional prefix and nothing else moves.
    argv = sys.argv[1:]
    core = None

    while argv and argv[0].startswith("--"):
        if argv[0] == "--core":
            if len(argv) < 2:
                raise SystemExit("check_closure: --core needs a value")
            core = argv[1]
            argv = argv[2:]
        elif argv[0].startswith("--core="):
            core = argv[0].split("=", 1)[1]
            argv = argv[1:]
        else:
            raise SystemExit("check_closure: unknown option %r" % argv[0])

    objs = argv
    if not objs:
        raise SystemExit(
            "usage: check_closure.py [--core gpsp|mgba|none] <objects...>")

    patterns = leak_patterns(core)

    print("closure policy: core = %s" % (core or "none (pure platform runtime)"))
    print("  leakage patterns rejected   : %d" % len(patterns))
    if core not in (None, "none"):
        print("  intentionally allowed       : %s"
              % ", ".join(CORE_PATTERNS[core]))
    print()

    scanned = scan(objs, patterns)
    internal, unresolved, weak_only, edges = resolve(scanned)

    undef = scanned["undef"]
    defined = scanned["defined"]
    linker_used = sorted(n for n in undef if n in LINKER_SYMS and n not in defined)

    print("dependency closure")
    print("  objects in closure          : %d" % len(objs))
    print("  symbols defined globally    : %d" % len(defined))
    print("  symbols defined locally     : %d" % len(scanned["defined_local"]))
    print("  symbols referenced undefined: %d" % len(undef))
    print("  resolved within the closure : %d symbols (%d references)"
          % (len(internal), edges))
    print("  supplied by the linker script: %d" % len(linker_used))
    print("  genuinely unresolved        : %d" % len(unresolved))
    print()

    ok = True

    if scanned["leaks"]:
        ok = False
        if core in (None, "none"):
            print("EMULATOR-CORE LEAKAGE -- this image must contain no "
                  "emulator code at all:")
        else:
            print("EMULATOR-CORE LEAKAGE -- this image may contain %s and "
                  "nothing else:" % core)
        for name, path, pat in scanned["leaks"]:
            print("  %-40s %s   (matched %r)" % (name, path, pat))
        print()
    else:
        print("  emulator leakage  : none")

    if unresolved:
        ok = False
        print()
        print("UNRESOLVED -- not defined by any object in the closure and not")
        print("provided by runtime/linker.ld:")
        for name in sorted(unresolved):
            print("  %-40s wanted by %s"
                  % (name, ", ".join(sorted(set(unresolved[name])))))
        print()
        print("  Each of these must be satisfied by something in the closure or")
        print("  the link will fail. Review before adding a new dependency.")
    else:
        print("  external deps     : none beyond the linker script")

    if weak_only:
        print()
        print("  weak undefined (resolve to 0, do not fail the link):")
        for name in sorted(weak_only):
            print("    %-38s wanted by %s"
                  % (name, ", ".join(sorted(set(weak_only[name])))))

    if linker_used:
        print("  linker-provided   : %s" % ", ".join(linker_used))

    if internal and os.environ.get("CLOSURE_VERBOSE"):
        print()
        print("  cross-object references resolved internally:")
        for name in sorted(internal):
            refs, defs = internal[name]
            print("    %-38s %s -> %s"
                  % (name,
                     ",".join(sorted(set(refs))),
                     ",".join(sorted(set(defs)))))

    print()
    print("VERDICT: %s" % ("closure is clean" if ok else "closure NOT clean"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
