#!/usr/bin/env python3
"""Catch stubs that are the WRONG KIND of symbol.

    python3 tools/check_symtypes.py build/main.o build/shim.o build/gfx.o

Adapted from LuaPSX/tools/check_symtypes.py. Two changes:
  * headers are scanned under runtime/ and apps/ instead of psx/
  * symbol kinds come from the object file's own symbol table (tools/elf.py)
    rather than from parsing `nm` output, so binutils is not required

The bug this exists to catch, in LuaPSX's words:

The linker resolves a symbol to an address and does not care whether the
definition is code or data. So defining

    void ndrc_g(void) { ... }          /* stub */

when the core declares

    extern struct ndrc_globals ndrc_g; /* real */

links cleanly. The core then does `ndrc_g.hacks_pergame = 0`, which stores
into .text -- read-only. That crashed inside CheckCdrom and looked like a disc
problem; on console it is a write into the PROT_READ|PROT_EXECUTE JIT mapping,
which is the same fault signature as an unrelocated pointer and just as slow to
pin down when every run costs a game relaunch.

At M0 there are no stubs yet, so a clean result here is expected rather than
impressive. It is wired up now so that the check already exists -- and is known
to work -- by the time an emulator core starts needing stubs.
"""

import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

# "adapters" added at M1.
#
# This is exactly the case the tool was built for. The M1 fixture
# (adapters/gba/gba_fixture.c) DEFINES symbols that gpSP DECLARES elsewhere --
# selected_boot_mode, sprite_limit, skip_next_frame, idle_loop_target_pc,
# translation_gate_targets, translation_gate_target_pc, netplay_client_id,
# netplay_num_clients as objects, and netpacket_send as a function. Defining any
# one of those with the wrong KIND -- a function where the core expects an
# object, or the reverse -- links cleanly and then reads garbage at runtime,
# which is the bug this checker documents.
#
# gpsp/ is deliberately NOT scanned: its headers are upstream and are not
# LUAport's to audit. What matters is that LUAport's own definitions agree with
# the declarations LUAport controls.
HDR_ROOTS = ["runtime", "apps", "adapters"]

STT_OBJECT = 1
STT_FUNC = 2


def defined_symbols(obj):
    """{name: 'func'|'data'} for globally visible definitions in one object."""
    try:
        e = Elf64(obj)
    except ElfError as ex:
        raise SystemExit("check_symtypes: %s" % ex)

    syms = {}
    for s in e.symbols():
        if not s["name"] or s["shndx"] == 0:
            continue
        if s["type"] == STT_FUNC:
            syms[s["name"]] = "func"
        elif s["type"] == STT_OBJECT:
            syms[s["name"]] = "data"
    return syms


def headers():
    for root in HDR_ROOTS:
        p = pathlib.Path(root)
        if not p.exists():
            continue
        for pat in ("*.h", "*.inc"):
            for f in p.rglob(pat):
                try:
                    yield f, f.read_text(errors="replace")
                except OSError:
                    pass


def main():
    objs = sys.argv[1:]
    if not objs:
        raise SystemExit("usage: check_symtypes.py <objects...>")

    mine = {}
    for o in objs:
        for k, v in defined_symbols(o).items():
            mine[k] = (v, o)

    hdrs = list(headers())

    bad, checked, undeclared = [], 0, []
    for name, (kind, obj) in sorted(mine.items()):
        decl_kind, where = None, None
        for path, text in hdrs:
            for m in re.finditer(r"(?<![A-Za-z0-9_])" + re.escape(name) +
                                 r"(?![A-Za-z0-9_])", text):
                line_start = text.rfind("\n", 0, m.start()) + 1
                line_end = text.find("\n", m.end())
                line = text[line_start:line_end if line_end > 0 else len(text)]
                st = line.strip()
                if st.startswith(("//", "*", "/*", "#define", "#ifdef",
                                  "#ifndef", "#if", "#undef")):
                    continue
                if "typedef" in st:
                    continue
                after = text[m.end():m.end() + 40]
                # name(  -> function decl;  name; name[  name =  -> object
                if re.match(r"\s*\(", after):
                    # could be a function pointer OBJECT: (*name)(...)
                    if re.search(r"\(\s*\*\s*$", text[:m.start()]):
                        decl_kind, where = "data", (path, st)
                    else:
                        decl_kind, where = "func", (path, st)
                elif re.match(r"\s*(;|\[|=|,)", after):
                    if st.startswith("extern") or "extern" in st.split(name)[0]:
                        decl_kind, where = "data", (path, st)
                if decl_kind:
                    break
            if decl_kind:
                break

        if decl_kind is None:
            undeclared.append(name)
            continue

        checked += 1
        if decl_kind != kind:
            bad.append((name, kind, decl_kind, obj, where))

    print("symbol-kind check")
    print("  defined by us     : %d" % len(mine))
    print("  matched a header  : %d" % checked)
    print("  no header decl    : %d  (file-private, not checked)"
          % len(undeclared))
    print()

    if bad:
        print("MISMATCHES -- these link fine and fault at runtime:")
        for name, kind, decl_kind, obj, (path, line) in bad:
            print("  %s" % name)
            print("      we define it as : %s   (in %s)" % (kind.upper(), obj))
            print("      header declares : %s   (%s)" % (decl_kind.upper(), path))
            print("      %s" % line)
        print()
        print("VERDICT: %d mismatch(es)" % len(bad))
        return 1

    print("VERDICT: every definition matches the kind its header declares")
    return 0


if __name__ == "__main__":
    sys.exit(main())
