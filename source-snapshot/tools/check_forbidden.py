#!/usr/bin/env python3
"""Prove the M1 image contains no dynarec, no libretro frontend, and no C++ runtime.

    python3 tools/check_forbidden.py build/m1/m1gpsp.elf build/m1/*.o

check_closure.py answers "is the dependency graph closed, and is there emulator
code here that should not be". This tool answers the three questions that are
specific to the M1 gate, and that a closure check cannot see:

  1. DYNAREC EXCLUSION. M1's entire premise is an interpreter-only build. The
     object list never names cpu_threaded.c, so the dynarec cannot be linked --
     but "cannot" is an argument about the Makefile, and this is the check on
     the artefact. Any of the forbidden names appearing as a DEFINED symbol
     means an excluded file got in; appearing as UNDEFINED means retained code
     still references it, which is just as much a failure because the link
     would have to be satisfied from somewhere.

  2. THE .bss TRIPWIRE. gpsp/cpu.h:153-154 declares

         extern u8 rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE];  /* 10 MB */
         extern u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];  /* 512 KB */

     in the default (non-_3DS, non-VITA, non-MMAP_JIT_CACHE) branch, and they
     are DEFINED only in the excluded cpu_threaded.c:37-54. If either ever gets
     defined, .bss gains roughly 10.5 MB in one step. That is the single most
     recognisable failure signature in this build, so it is checked as a size
     bound as well as by name: a 10.5 MB jump is unmistakable, and a name check
     alone would miss a leak that arrived under a different symbol.

  3. C++ RUNTIME. The audit found zero C++ features in cpu.cc and video.cc
     beyond templates, so no __cxa_*, _Unwind_*, __gxx_*, operator new/delete
     and no .init_array/.fini_array should exist. -fno-rtti,
     -fno-threadsafe-statics and -fno-use-cxa-atexit make that structural;
     this verifies it on the artefact.

  4. LIBRETRO FRONTEND. Plan A drops the frontend entirely. The five
     filestream_* entry points are ALLOWED because adapters/gba/gba_filestream.c
     defines them deliberately over the shim's FILE*; everything else wearing a
     frontend name is a leak.

A note on how symbols are read: this uses the same tools/elf.py that every other
LUAport checker uses, so the four tools agree about what an ELF says. Nothing
shells out to nm, which keeps the result identical on any host.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf64, ElfError            # noqa: E402

SHN_UNDEF = 0
STT_SECTION = 3
STT_FILE = 4

# ---------------------------------------------------------------------------
# 1. Dynarec. Exactly the set named in the M1 authorisation.
# ---------------------------------------------------------------------------
DYNAREC_SYMS = {
    "rom_translation_cache",
    "ram_translation_cache",
    "rom_translation_ptr",
    "ram_translation_ptr",
    "rom_branch_hash",
    "translate_block_arm",
    "translate_block_thumb",
    "execute_arm_translate",
    "init_emitter",
    "init_dynarec_caches",
    "flush_dynarec_caches",
    "dump_translation_cache",
    "map_jit_block",
    "unmap_jit_block",
    "dynarec_enable",
}

# Substring probes for dynarec code that might arrive under a name not on the
# exact list above -- block lookup and cache flush helpers, mainly.
DYNAREC_PATTERNS = [
    "translation_cache",
    "block_lookup_address",
    "translate_block",
    "_stub",
    "emit_",
]

# ...but these legitimately match a pattern and are not dynarec.
DYNAREC_PATTERN_ALLOW = {
    # gpsp/cpu.cc's interpreter and gba_memory's page loader use no such names;
    # this set exists so a future false positive is fixed here, with a reason,
    # rather than by weakening the pattern list.
}

# ---------------------------------------------------------------------------
# 2. C++ runtime.
# ---------------------------------------------------------------------------
CXX_PATTERNS = [
    "__cxa_", "_Unwind_", "__gxx_", "_ZdlPv", "_ZdaPv", "_Znwm", "_Znam",
    "__dso_handle", "__cxx_global", "_GLOBAL__sub_I",
]

FORBIDDEN_SECTIONS = [".init_array", ".fini_array", ".ctors", ".dtors",
                      ".eh_frame", ".gcc_except_table"]

# ---------------------------------------------------------------------------
# 3. Libretro frontend.
# ---------------------------------------------------------------------------
LIBRETRO_PATTERNS = [
    "retro_", "libretro_", "vfs_", "path_", "string_is_", "filestream_",
    "rtime_", "encoding_", "fopen_utf8", "gba_cc_lut", "open_gba_bios_rom",
]

# The five filestream entry points are LUAport-owned (adapters/gba/
# gba_filestream.c) and implement the upstream opaque RFILE interface over the
# shim's FILE*. They are the deliberate, measured cost of Plan A.
LIBRETRO_ALLOW = {
    "filestream_open",
    "filestream_seek",
    "filestream_read",
    "filestream_close",
    "filestream_get_size",

    # --- gpsp/input.c, not gpsp/libretro/libretro.c ------------------------
    # input.c entered the closure as part of the BSON reader dependency chain
    # (savestate.c's gba_load_state/gba_save_state call input_*_savestate).
    # It carries five symbols that WEAR a frontend name but are DEFINED BY THE
    # CORE FILE ITSELF, at input.c:22-25 and input.c:37 -- verified by reading
    # the file, not inferred from the prefix.
    #
    # They are allowed because the pattern list exists to catch libretro.c and
    # libretro-common leaking IN, and these are neither: excluding input.c
    # would break the link, and renaming them would mean editing gpsp/**, which
    # is forbidden. Each is a plain feature flag with no frontend dependency:
    # all four bools are initialised false and only libretro.c's environment
    # probe ever sets them true, so they stay false for the life of the M1
    # image. retro_set_input_state is a one-line setter for a static callback
    # pointer that nothing in M1 ever calls, leaving it NULL -- which is what
    # makes update_input() return 0 immediately (input.c:75-76).
    #
    # If any of these ever appears as UNDEFINED rather than defined, that means
    # input.c left the link and the reference is coming from libretro.c after
    # all. The check below is therefore tightened to require DEFINED.
    "libretro_supports_bitmasks",
    "libretro_supports_ff_override",
    "libretro_ff_enabled",
    "libretro_ff_enabled_prev",
    "retro_set_input_state",
}

# Of LIBRETRO_ALLOW, the names that are only acceptable when DEFINED by a file
# we deliberately link. An undefined reference to one of these means it is being
# imported from the excluded frontend, which is the exact leak this tool exists
# to catch -- so the allowance must not extend to that case.
LIBRETRO_ALLOW_DEFINED_ONLY = {
    "libretro_supports_bitmasks",
    "libretro_supports_ff_override",
    "libretro_ff_enabled",
    "libretro_ff_enabled_prev",
    "retro_set_input_state",
}

# 10.5 MB is the translation-cache signature. 2 MB is the authorised ceiling:
# the honest interpreter closure is about 1.28 MB of emulated memory plus the
# fixture's 77 KB screen buffer and the shim's own .bss.
BSS_LIMIT = 2 * 1024 * 1024


def symbols_of(path):
    """[(name, defined)] for every real symbol in one ELF."""
    try:
        e = Elf64(path)
    except ElfError as ex:
        raise SystemExit("check_forbidden: %s" % ex)

    out = []
    for s in e.symbols():
        name = s["name"]
        if not name:
            continue
        if s["type"] in (STT_SECTION, STT_FILE):
            continue
        out.append((name, s["shndx"] != SHN_UNDEF))
    return out


def bss_size(elf_path):
    """__bss_end - __bss_start, from the linker markers."""
    try:
        e = Elf64(elf_path)
    except ElfError as ex:
        raise SystemExit("check_forbidden: %s" % ex)

    syms = e.symbol_values(["__bss_start", "__bss_end"])
    if "__bss_start" not in syms or "__bss_end" not in syms:
        return None
    return syms["__bss_end"] - syms["__bss_start"]


def section_names(elf_path):
    try:
        e = Elf64(elf_path)
    except ElfError as ex:
        raise SystemExit("check_forbidden: %s" % ex)
    # Elf64.sections is an attribute (a list of dicts), not a method.
    return [s["name"] for s in e.sections]


def check_group(found, title, explain):
    if not found:
        print("  %-34s clean" % title)
        return True
    print()
    print("%s:" % title)
    for name, path, why in found:
        print("  %-40s %-28s %s" % (name, os.path.basename(path), why))
    print("  -> %s" % explain)
    print()
    return False


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit("usage: check_forbidden.py <elf> [objects...]")

    elf_path = args[0]
    all_paths = args

    dyn_found = []
    cxx_found = []
    lr_found = []

    for path in all_paths:
        for name, defined in symbols_of(path):
            low = name.lower()
            state = "DEFINED" if defined else "referenced (undefined)"

            # --- dynarec, exact names
            if name in DYNAREC_SYMS:
                dyn_found.append((name, path, state))
                continue

            # --- dynarec, patterns
            hit = None
            if name not in DYNAREC_PATTERN_ALLOW:
                for pat in DYNAREC_PATTERNS:
                    if pat in low:
                        hit = pat
                        break
            if hit:
                dyn_found.append((name, path, "%s (matched %r)" % (state, hit)))
                continue

            # --- C++ runtime
            hit = None
            for pat in CXX_PATTERNS:
                if pat in name:
                    hit = pat
                    break
            if hit:
                cxx_found.append((name, path, "%s (matched %r)" % (state, hit)))
                continue

            # --- libretro frontend
            if name in LIBRETRO_ALLOW:
                # A conditional allowance: see LIBRETRO_ALLOW_DEFINED_ONLY. In
                # an OBJECT the symbol may legitimately be undefined (another
                # TU defines it); only the linked ELF must have it resolved.
                if (name in LIBRETRO_ALLOW_DEFINED_ONLY
                        and not defined and path == elf_path):
                    lr_found.append(
                        (name, path,
                         "UNDEFINED in the linked image -- allowed only when "
                         "defined by gpsp/input.c"))
                continue
            hit = None
            for pat in LIBRETRO_PATTERNS:
                if pat in low:
                    hit = pat
                    break
            if hit:
                lr_found.append((name, path, "%s (matched %r)" % (state, hit)))

    print("=" * 78)
    print("M1 forbidden-symbol check -- %s" % elf_path)
    print("=" * 78)
    print("  objects inspected                  %d" % (len(all_paths) - 1))
    print()

    ok = True
    ok &= check_group(
        dyn_found, "dynarec symbols",
        "M1 is interpreter-only. A DEFINED hit means an excluded file was "
        "linked; an undefined hit means retained code still calls the dynarec.")
    ok &= check_group(
        cxx_found, "C++ runtime symbols",
        "cpu.cc/video.cc use no C++ feature beyond templates. Any hit means "
        "-fno-rtti/-fno-threadsafe-statics/-fno-use-cxa-atexit did not hold.")
    ok &= check_group(
        lr_found, "libretro frontend symbols",
        "Plan A drops the frontend. Only the five LUAport-owned filestream_* "
        "entry points are allowed.")

    # --- forbidden sections
    secs = section_names(elf_path)
    bad_secs = [s for s in secs
                if any(s == f or s.startswith(f + ".") for f in FORBIDDEN_SECTIONS)]
    if bad_secs:
        ok = False
        print()
        print("FORBIDDEN SECTIONS PRESENT: %s" % ", ".join(bad_secs))
        print("  -> runtime/linker.ld neither places nor discards .init_array,")
        print("     so a static constructor would be an orphan AND would never")
        print("     be called by _start. Link with -Wl,--orphan-handling=error.")
        print()
    else:
        print("  %-34s clean" % "static-init sections")

    # --- the .bss tripwire
    bss = bss_size(elf_path)
    if bss is None:
        print("  %-34s UNKNOWN (missing __bss_start/__bss_end)" % ".bss size")
        ok = False
    else:
        mb = bss / (1024.0 * 1024.0)
        if bss > BSS_LIMIT:
            ok = False
            print()
            print("BSS TRIPWIRE: %d bytes (%.2f MB) exceeds the %d byte limit."
                  % (bss, mb, BSS_LIMIT))
            print("  -> A jump of roughly 10.5 MB is the unmistakable signature")
            print("     of rom_translation_cache (10 MB, gpsp/cpu.h:153) and")
            print("     ram_translation_cache (512 KB, cpu.h:154) having been")
            print("     defined -- i.e. cpu_threaded.c leaked into the link.")
            print("     STOP and find the leak before trusting any size number.")
            print()
        else:
            print("  %-34s %d bytes (%.2f MB), under the %d limit"
                  % (".bss tripwire", bss, mb, BSS_LIMIT))

    print()
    print("VERDICT: %s" % ("no forbidden content" if ok
                           else "FORBIDDEN CONTENT PRESENT"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
