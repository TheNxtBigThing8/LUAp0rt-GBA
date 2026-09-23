# LUAp0rt GBA v0.0.1 — THIRD-PARTY NOTICES

Formal per-component notices for LUAp0rt GBA v0.0.1.

Every statement here was derived from a **read-only audit of the frozen v0.0.1
source snapshot**: license files, per-file copyright headers, in-source provenance
comments, vendored git metadata, the Makefile's include/flag definitions, and the
linker map `binary/m16cgpsp.map`. Nothing is asserted from memory.

**Covered executable:** `binary/m16cgpsp.bin`
**SHA-256:** `16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2`
**Shipped with:** `binary/m16cgpsp.lua` (loader; the two ship as a pair)

> ### ✅ ALL RELEASE-BLOCKING ITEMS ARE CLOSED
>
> **Resolved in Pass 3 — LUAp0rt's project license is now DECLARED.** NBT
> explicitly authorised **GPL-2.0-or-later** for the original LUAp0rt work. The
> canonical GNU GPL version 2 text is reproduced verbatim at `LICENSE`, and the
> project copyright/licence notice is at `COPYRIGHT`. This was the last
> release-blocking item. → §4, §6.2.
>
> **Resolved in Pass 2C:** **corresponding source is now complete** (§5, §5.8) —
> 207 files under `source-snapshot/`, every one verified byte-identical to the
> frozen source, with a clean machine-verifiable manifest. **GPL-2.0 §3 is
> satisfied.**
>
> **Resolved in Pass 2B:** the Luac0re question (§3.F, §6.4) — no Luac0re code is
> incorporated, so no Luac0re license text is required.
>
> **Open but NOT blocking — EmuC0re attribution** (§6.3). Downgraded in Pass 2B
> from "license undetermined" to an attribution item: the incorporated font data
> is a byte-identical copy of a LuaPSX file and therefore reaches LUAp0rt under
> LuaPSX's GPL-2.0-or-later, whose text is present and verbatim. What remains
> unread is EmuC0re's *own* notice. **A Pass 3 retrieval attempt was made and
> failed** — see §6.3. **Nothing was fabricated to close it.**
>
> This file records findings. It does **not** certify compliance.

---

## 1. How components were classified

| Class | Meaning |
| --- | --- |
| **A** | Incorporated / linked code — present in the shipping binary |
| **B** | Materially adapted or derived code — present in the shipping binary |
| **C** | Build-time or runtime-host dependency — not in the binary |
| **D** | Test / development dependency only |
| **E** | Reference / inspiration only — no code incorporated |
| **F** | Not part of the v0.0.1 release |

**Method for deciding "in the binary".** The link line was read from
`binary/m16cgpsp.map`, which records exactly **41 objects**. Include paths were
read from the Makefile (`M16C_INCLUDES:15244`, `GPSP_INCLUDES:286-287`). A
component is class A/B only where the map or an include path proves its presence.

---

## 2. Summary

| # | Component | Class | License | In binary | Corresponding source included | License text |
| --- | --- | --- | --- | --- | --- | --- |
| A | gpSP (libretro fork) | **A** | GPL-2.0-or-later | **Yes** — 12 objects | ✅ **Yes** — `source-snapshot/gpsp/` | `licenses/GPL-2.0-gpSP-COPYING.txt` |
| B | libretro-common | **A** (headers) / C | MIT, per file | **Yes** — headers only | ✅ **Yes** — `gpsp/libretro/libretro-common/` | `licenses/MIT-libretro-common.txt` |
| C | LuaPSX | **B** | GPL-2.0-or-later | **Yes** — 6 objects | ✅ **Yes** — `source-snapshot/runtime/` | `licenses/GPL-2.0-LuaPSX-COPYING.txt` |
| D | LUAp0rt (NBT) | original | **GPL-2.0-or-later** — © 2026 NBT (§4) | **Yes** — 23 objects | ✅ **Yes** — `adapters/`, `apps/`, `lua/`, `tools/` | `LICENSE` (+ `COPYRIGHT`) |
| E | EmuC0re | **A** (font, via LuaPSX) + E | Copy distributed under **LuaPSX GPL-2.0-or-later**; EmuC0re's own notice unread (§6.3) | **Yes** — `font_data` | covered by LuaPSX §5 | `licenses/GPL-2.0-LuaPSX-COPYING.txt` (governs the copy) |
| F | Luac0re | **C** | **Not required** — no code incorporated (§3.F) | No | n/a | not required |
| G | mGBA | **E** | MPL-2.0 | **No** (proven) | n/a | not required |
| H | LuaGB / LuaMD | **E** | — | No | n/a | not required |
| I | PCSX / PCSX-ReARMed | **E** (license lineage) | GPL-2.0-or-later | No | n/a | not required |
| J | mast1c0re research | **E** | — | No | n/a | not required |
| K | GNU toolchain, Python 3 | **C** | GPL / PSF | No | not required (§5.3) | not required |
| L | `open_gba_bios.bin` | **F** | — | **No** (proven) | n/a | not required |

---

## 3. Component detail

### A. gpSP — GBA emulator core · CLASS A · GPL-2.0-or-later

- **Upstream:** `https://github.com/libretro/gpsp.git`
- **Pinned commit:** `8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f` (branch `master`),
  from the vendored `gpsp/.git` metadata
- **Copyright:** `Copyright (C) 2006 Exophase <exophase@gmail.com>`, plus
  subsequent contributors (notaz; davidgfnet; libretro contributors)
- **License text found at:** `gpsp/COPYING` — GNU GPL version 2, June 1991
- **Per-file statement** (e.g. `gpsp/common.h:5-8`, `gpsp/gba_memory.c:5-8`):

  > *"This program is free software; you can redistribute it and/or modify it
  > under the terms of the GNU General Public License as published by the Free
  > Software Foundation; either version 2 of the License, **or (at your option)
  > any later version**."*

  The "or later" wording makes the effective license **GPL-2.0-or-later**, not
  GPL-2.0-only.
- **Preserved license text:** `licenses/GPL-2.0-gpSP-COPYING.txt`, copied
  **verbatim** (byte-identical, SHA-256
  `189b1af95d661151e054cea10c91b3d754e4de4d3fecfb074c1fb29476f7167b`)
- **Incorporated as** 12 of 41 linked objects:

  ```
  gpsp_cpu.o    gpsp_main.o     gpsp_gba_memory.o  gpsp_video.o
  gpsp_sound.o  gpsp_serial.o   gpsp_serial_proto.o
  gpsp_rfu.o    gpsp_gbp.o      gpsp_cheats.o
  gpsp_savestate.o  gpsp_input.o
  ```

- **Modification status:** the LUAp0rt sources state repeatedly that `gpsp/` is
  unmodified (e.g. `adapters/gba/gba_bios.c:16` — *"gpsp/ IS NOT MODIFIED. Not one
  line."*). Adaptation is done in `adapters/gba/`, outside the upstream tree.
- **Obligation:** GPL-2.0-or-later is copyleft. Distributing the binary triggers a
  corresponding-source obligation for the **whole covered work**, not only for
  gpSP's own files. See §5.

### B. libretro-common — RetroArch support headers · CLASS A (headers) · MIT

- **Copyright:** `Copyright (C) 2010-2020 The RetroArch team`
- **License:** MIT-style, **declared per file**; there is no top-level LICENSE
  file for libretro-common in this tree. Each header scopes its own grant
  ("*The following license statement only applies to this file (<name>)*").
- **Preserved license text:** `licenses/MIT-libretro-common.txt` — the notices are
  reproduced **verbatim**, unedited, with the per-file scoping wording intact.
- **Why it is in the shipping build:** gpSP translation units that are compiled
  into v0.0.1 include libretro-common headers —
  `gpsp/gba_memory.c:21` (`#include "streams/file_stream.h"`) and
  `gpsp/gba_memory.h:23` (`#include "libretro.h"`) — and the header directory is
  on the include path via
  `GPSP_INCLUDES = … -Igpsp/libretro/libretro-common/include` (`Makefile:286-287`).
  `gpsp/retro_inline.h` carries the same notice and sits at the gpSP tree root.
- **What is NOT in the binary:** **no libretro-common `.c` file is compiled or
  linked.** The map shows no such object. LUAp0rt supplies its own `RFILE`
  implementation in `adapters/gba/gba_filestream.c`, replacing libretro's VFS.
- **Obligation:** MIT requires the copyright notice and permission notice to be
  retained in distributions. That is satisfied by
  `licenses/MIT-libretro-common.txt` **provided the headers themselves are also
  shipped** with the corresponding source (§5).
- **Compatibility:** MIT is a permissive licence and is generally compatible with
  GPL-2.0-or-later distribution. **No incompatibility was found.**

### C. LuaPSX — PS5 runtime layer · CLASS B (materially derived) · GPL-2.0-or-later

- **Upstream:** `https://github.com/soniciso1/LuaPSX.git`
- **Local clone at commit:** `d2794ef1c1f3653dfc713f21ac194694a6c58b94`
  (branch `main`; tag `v1.0` = `81af353feb791f09f3f2fa5676fc3f993b7db983`)
- **Author:** soniciso1
- **License text found at:** `LuaPSX/COPYING` — GNU GPL version 2, June 1991.
  `LuaPSX/README.md:191` declares: *"GPL-2.0-or-later, inherited from
  PCSX-ReARMed."*
- **Preserved license text:** `licenses/GPL-2.0-LuaPSX-COPYING.txt`, copied
  **verbatim** (byte-identical, SHA-256
  `a9107fe8417ceeaca41ce17987cde004cb919789511c0a92fd114d1226a624a5`).
  It is preserved as a **separate file** from gpSP's because the two `COPYING`
  files are not byte-identical (different FSF postal address and whitespace), and
  neither upstream file may be altered.
- **Relationship — this is derivation, not inspiration.** LUAp0rt's own source
  headers record the relationship, including byte-level identity:

  | LUAp0rt file | Source statement |
  | --- | --- |
  | `runtime/shim.c` | `runtime/platform.h:118` claims *"byte-identical to LuaPSX/src/shim.c"*. **Pass 2B correction:** the files now differ by exactly one additive block (`shim.c:41-75`, `#ifdef LUAPORT_SESSION_REUSE`, added at M16); no deletions, no other edits |
  | `runtime/gfx.c` | *"Adapted from LuaPSX/src/ui.c lines 1-75. The bodies below are byte-identical"* (`:4`) |
  | `runtime/savedata.{c,h}` | *"Ported from LuaPSX/src/savedata.c … This is a PORT"* (`savedata.h:9`) |
  | `runtime/core.h` | *"Adapted from LuaPSX/src/core.h"* (`:7`); `:73` records a byte-identical block |
  | `runtime/platform.{h,c}` | *"Adapted from LuaPSX/src/main.c lines 300-376 … 801-809"* (`platform.h:10`) |
  | `runtime/audio.{c,h}` | *"Adapted from LuaPSX/src/main.c:396-440, :756-761"* (`audio.c:7`) |
  | `runtime/linker.ld` | *"Adapted from LuaPSX/linker.ld"* (`:5`) |
  | `adapters/gba/m13store.h` | *"Carried from LuaPSX/src/discs.c:19-28"* (`:160`) |
  | `tools/send.py` | *"Adapted from LuaPSX/send2.py, unchanged in protocol"* (`:6`) |
  | `tools/verify_reloc.py`, `tools/check_symtypes.py` | *"Adapted from LuaPSX/tools/…"* |

- **Pass 2B — thirteen verbatim copies proven by hash.** Beyond the adaptations
  above, SHA-256 comparison of both trees shows these LUAp0rt files are
  **byte-identical** to their LuaPSX originals: `runtime/tables.h`
  (`ca8224fe…`), `runtime/boot.inc` (`34213a3e…`), `runtime/fault.inc`
  (`5cd94f95…`), and all ten `runtime/libc/*.h` headers — `assert.h`
  (`20452ffa…`), `inttypes.h` (`120ba3f1…`), `malloc.h` (`cf34efda…`),
  `math.h` (`74f9d313…`), `setjmp.h` (`efeda0cd…`), `stdio.h` (`3d41fbd1…`),
  `stdlib.h` (`defe05a3…`), `string.h` (`9a0c7e86…`), `time.h` (`6ec5ae70…`),
  `unistd.h` (`bb477b5a…`). The shipped Lua loader is likewise a LuaPSX
  derivative (`lua/m0.lua.in:23`, *"ADAPTED FROM LuaPSX/lua/psx.lua.in"*).
  **This strengthens the class-B finding; it does not change it.**
- **Incorporated as** 6 of 41 linked objects: `shim.o`, `gfx.o`, `platform.o`,
  `audio.o`, `savedata.o`, and `m16cmain.o` in part (the `_start` bring-up
  sequence and `runtime/linker.ld` layout).
- **Obligation:** identical in kind to gpSP's. LuaPSX is a **second, independent**
  GPL-2.0-or-later reason the combined work is copyleft. Even if gpSP were
  replaced, the `runtime/` layer alone would keep v0.0.1 under GPL-2.0-or-later.

### D. LUAp0rt — original work · author NBT

23 of 41 linked objects (the GBA adapter layer and the launcher). Authorship is
recorded by the binary itself (`apps/m16cgpsp/main.c:1814`, "BY NBT").

**Copyright (C) 2026 NBT. Licensed GPL-2.0-or-later** — declared by NBT in Pass 3.
The canonical GPLv2 text is at `LICENSE`; the project notice is at `COPYRIGHT`.
See §4.

**LUAp0rt was created by NBT.** NBT is **not** the author of gpSP, LuaPSX,
EmuC0re, libretro-common, Luac0re or mGBA; equally, no upstream project or
upstream developer is the creator of LUAp0rt.

### E. EmuC0re — 8x8 bitmap font · CLASS A (data) + CLASS E (pattern) · LICENSE UNDETERMINED

- **Attributed to:** egycnq / EgyDevTeam
  (`runtime/core.h:12`, `lua/m16c.lua.in:41`, shipped `m16cgpsp.lua:41`)
- **Incorporated item:** the `font_data` table in `runtime/tables.h:6`, introduced
  by the comment *"8x8 bitmap font, ASCII 32..90 (space through 'Z'), **lifted
  from EmuC0re**."* It is rendered by `draw_char()` (`runtime/gfx.c:26-30`) and is
  therefore **compiled into the shipping binary**.
- **Separately, as CLASS E:** EmuC0re is credited as the *"PS5 runtime pattern"* —
  video/audio/pad bring-up, shellcode ABI, picker structure. That influence is
  reference material and carries no distribution obligation by itself.
- **Pass 2B — the transmission path, established by hash.** LUAp0rt did **not**
  take this font from EmuC0re directly. `runtime/tables.h` and
  `LuaPSX/src/tables.h` are **byte-identical** — both 2,811 bytes, both SHA-256
  `ca8224fe3c055ec4b6a2b732685367d3dc1ea7ea73cde772e020f9d436a4dbb6` — including
  the *"lifted from EmuC0re"* comment itself. The font table is therefore a
  **verbatim copy of a LuaPSX file**, and the copy LUAp0rt distributes is
  governed by **LuaPSX's GPL-2.0-or-later** (§3.C), whose full text is present
  and verbatim at `licenses/GPL-2.0-LuaPSX-COPYING.txt`.
- **License status — reclassified.** There is **no license gap in the chain
  LUAp0rt actually relied on**: the immediate upstream is known, read and
  reproduced. What could **not** be determined is EmuC0re's *own* license text
  and copyright holder of record: no copy of EmuC0re exists in this workspace,
  and the upstream repository could not be inspected — a **Pass 3 retrieval
  attempt was made and denied** (`WebFetch` and `WebSearch` both lack permission
  in this environment). **Not guessed, and not fabricated.** See §6.3.
- **Why this still matters.** If EmuC0re is MIT — as its upstream repository is
  reported to state — MIT is GPL-2.0-compatible, so there is **no licence
  conflict** on either reading. But MIT also requires its copyright notice to be
  **reproduced verbatim**, and the exact holder string cannot be reproduced
  without reading it. Neither `runtime/tables.h` nor `LuaPSX/src/tables.h`
  carries a copyright line — only the prose attribution.
- **Attribution status:** the EmuC0re credit **is** carried in the shipped loader
  (`m16cgpsp.lua:41`) and in `runtime/core.h:12`, so attribution is preserved even
  though EmuC0re's own notice text is unread.
- **Not modified:** the font has **not** been replaced, and the shipping binary is
  untouched.

### F. Luac0re — delivery substrate · CLASS C · NO CODE INCORPORATED (RESOLVED)

- **Attributed to:** Gezine (`runtime/core.h:13`, shipped `m16cgpsp.lua:42`)
- **Relationship:** Luac0re is the Lua/JIT delivery framework that runs on the
  console. LUAp0rt's loader is delivered by it and calls into it — its syscall
  table, its `tonumber` semantics, its 500 KB script cap
  (`tools/mklua.py:6`, `lua/m16c.lua.in:34-38`, `lua/upload.lua:125`).
- **Not incorporated:** Luac0re contributes no object to the link line. The
  loader's transport is described in-source as LUAp0rt's own, unchanged and
  re-proved from M0 onward (`lua/m16c.lua.in:32-38`).
- **Pass 2B — relationship RESOLVED as interoperation, not incorporation.** The
  determination rests on positive evidence, not on absence alone:
  1. **Every derivation claim in the tree names LuaPSX, LuaMD or LuaGB — never
     Luac0re.** The shipped loader's own stated ancestry is
     *"ADAPTED FROM LuaPSX/lua/psx.lua.in"* (`lua/m0.lua.in:23`), followed by an
     itemised list of what was removed from that file. The Lua layer has a known,
     different parent.
  2. **Luac0re appears only as a host that LUAp0rt calls, targets or works
     around** — resolving `sceKernelDlsym` *before* Luac0re's built-in guess is
     used (`lua/m0.lua.in:125`), compensating for a wrong built-in dlsym offset
     on all 2.xx/3.xx/4.xx firmware (`:56-59`), avoiding a `syscall.poll` that is
     absent from its table, respecting its 500 KB script cap, and handling its
     string-only `tonumber`. These are consumer accommodations to an external
     runtime, which is the signature of interoperation rather than copying.
  3. **No Luac0re source file, header or fragment exists anywhere in this
     workspace**, and no object, symbol or attribution in the binary or loader is
     traced to it.
- **Consequence:** **no Luac0re license text is required in `licenses/`**, because
  no Luac0re code is redistributed. Its own license terms could not be read (no
  local copy; no network access in this environment) but **do not govern this
  distribution**.
- **Acknowledgement preserved** for the platform/delivery relationship, in this
  file, in `CREDITS.md`, and in the shipped loader itself (`m16cgpsp.lua:42`,
  *"Delivery substrate: Luac0re by Gezine."*) — which is untouched.

### G. mGBA — evaluated alternative · CLASS E · MPL-2.0 · NOT INCORPORATED

- **License found at:** `mgba/LICENSE` — Mozilla Public License Version 2.0
- **Role:** evaluated as an alternative emulator core in report §14 and **not
  selected**; report §21 lists `mgba/` as a reference/upstream tree to leave
  untouched.
- **Proof of exclusion from v0.0.1** (read-only, three independent checks):
  1. **No `-Imgba`** in any M16C include path — `M16C_INCLUDES` (`Makefile:15244`)
     and `GPSP_INCLUDES` (`:286-287`) resolve only to
     `adapters/gba/include`, `runtime/libc`, `runtime`, `apps/m16cgpsp`,
     `apps/m13bgpsp`, `adapters/gba`, `gpsp`, and
     `gpsp/libretro/libretro-common/include`.
  2. **No mGBA object on the link line** — all 41 objects in
     `binary/m16cgpsp.map` are accounted for as gpSP, LUAp0rt adapters, or runtime.
  3. **Textual references are prose only** — occurrences of "mgba" in the Makefile
     and shipping headers are comments about M4/M8 test ROMs and core comparison.
- **Consequence:** mGBA is **not** redistributed with this release and its MPL-2.0
  text is **not** required in `licenses/`. It is documented here as development /
  reference material, which is what the project record supports.

### H. LuaGB / LuaMD · CLASS E

soniciso1. Predecessors in the runtime lineage recorded at `runtime/audio.c:22`:
*"LuaGB -> LuaMD -> LuaPSX -> LUAport."* Their influence reaches v0.0.1 through
LuaPSX (class B) rather than directly; no separate code path was found.

### I. PCSX / PCSX-df / PCSX-ReARMed · CLASS E (license lineage)

notaz and the PCSX teams. The origin of LuaPSX's GPL-2.0-or-later licence
lineage. **No PlayStation emulation code is present in the GBA build** — the
relevance is the licence that LuaPSX inherited and passed to LUAp0rt's `runtime/`.

### J. mast1c0re · CLASS E

CTurt and McCaulay. Underlying exploit research that Luac0re builds on. No code.

### K. Build toolchain · CLASS C

GNU `make` 4.4.1, `gcc` 15.2.0, GNU binutils, Python 3 (used by
`tools/mklua.py`, `tools/elf.py`, `tools/sizereport.py`, `tools/verify_reloc.py`,
`tools/check_*.py`). Standard system tools; see §5.3 for why they are not part of
the corresponding-source obligation.

### L. `gpsp/bios/open_gba_bios.bin` · CLASS F · NOT IN RELEASE, NOT IN BINARY

An open-source BIOS replacement shipped in the upstream gpSP tree. It is
**excluded from this release package** and is **not in the shipping binary**:

- it reaches a build only through `gpsp/bios_data.S` (`.incbin`), and
  **no `bios_data.o` appears on the link line**;
- its symbol `open_gba_bios_rom` is an explicit **forbidden symbol** in
  `tools/check_forbidden.py:110`, enforced against the linked image by
  `make m16c-verify`.

So its absence is **gate-enforced**, not merely observed.

---

### M. LUAp0rt Launcher — desktop dashboard (added in v0.1.0) · author NBT · GPL-2.0-or-later

- **What it is:** `launcher/luap0rt_launcher.py` and `launcher/ui/*` are original
  LUAp0rt work by NBT (Copyright (C) 2026 NBT, GPL-2.0-or-later). The launcher
  runs on the PC only. It does **not** modify, rebuild or replace the shipping
  payload; it invokes `send.py` and `upload.py` exactly as documented and reads the
  payload's UDP log. `launcher/tools/send.py`, `launcher/tools/upload.py` and
  `launcher/lua/upload.lua` are byte-identical copies of the LUAp0rt-owned files
  of the same names in the development tree; `send.py` and `upload.py`/`upload.lua`
  are ported from LuaPSX (`send2.py`, `upload_disc.py`/`upload_resume.lua`), so
  they carry the same GPL-2.0-or-later-via-LuaPSX standing recorded in section C.
- **`launcher/LUAp0rt-Launcher.exe`** is a PyInstaller one-file build of that
  source. It bundles the following third-party components, none of which is part
  of the console payload. Their license texts are shipped verbatim in `licenses/`
  (copied from the installed packages, unmodified):

  | Component | Version | License | Text in `licenses/` |
  | --- | --- | --- | --- |
  | Python (CPython) and its standard library incl. Tkinter | 3.11.9 | PSF-2.0 | `PSF-2.0-Python-3.11-LICENSE.txt` |
  | Tcl/Tk (used by Tkinter for the status window and file dialogs) | 8.6 | Tcl/Tk license (BSD-style) | `TclTk-8.6-license.terms.txt` |
  | Pillow | 12.3.0 | HPND (MIT-CMU) | `HPND-Pillow-12.3.0-LICENSE.txt` |
  | pystray (tray icon) | 0.19.5 | LGPL-3.0 | `LGPL-3.0-pystray-0.19.5-COPYING.txt`, `LGPL-3.0-pystray-0.19.5-COPYING.LGPL.txt` |
  | PyInstaller bootloader | 6.22.3 | GPL-2.0 with the PyInstaller bootloader exception | `GPL-2.0-PyInstaller-6.22.3-COPYING.txt` |

  pystray is used unmodified as a library; its LGPL-3.0 terms are compatible with
  the GPL-2.0-or-later launcher, and the complete launcher source that links it is
  in this package. The exe can be rebuilt from that source with
  `launcher/build_exe.cmd`.

## 4. LUAp0rt's own license status — **DECLARED: GPL-2.0-or-later**

> **RESOLVED IN PASS 3.** NBT has explicitly authorised the project licence. This
> section previously read *"NONE DECLARED — RELEASE-BLOCKING"*; that finding is
> **superseded** and the historical record is preserved below.

**Declaration of record:**

```
LUAp0rt
Copyright (C) 2026 NBT

Original LUAp0rt code is licensed under the GNU General Public
License version 2 or, at your option, any later version.
```

**SPDX-License-Identifier: `GPL-2.0-or-later`**

| Artifact | Path | Content |
| --- | --- | --- |
| Project licence text | `LICENSE` | **Canonical GNU GPL v2, verbatim** — byte-identical to the verified upstream text (SHA-256 `189b1af9…`, 339 lines). Not paraphrased, shortened or rewritten. |
| Project notice | `COPYRIGHT` | Copyright line, scope, SPDX id, standard GPL notice, and an explicit list of third-party work **not** authored by NBT. |

**Scope.** This declaration covers **NBT's original LUAp0rt work only** — the 23
adapter/launcher objects listed in §3.D plus the build system and harnesses.
**Third-party components, adapted material and derived material retain their own
copyrights, licences, notices and attribution** (§3.A–§3.C, §3.E) and are **not**
relicensed by this declaration.

**Method note.** The `LICENSE` text was produced by **byte-exact copy** of the
already-verified canonical GPLv2 text in `licenses/GPL-2.0-gpSP-COPYING.txt`, not
by transcription. Its SHA-256 equals that file's exactly, which is machine-checkable
proof the canonical wording is intact. The `licenses/GPL-2.0-LuaPSX-COPYING.txt`
copy was deliberately **not** used as the source: it is the older
02111-1307/"Library GPL" variant **and carries an appended PSEmu public-domain
note**, so it is not pure canonical GPLv2.

**No source-file headers were added.** The shipping source is size-frozen (16 bytes
spare); licensing is carried at release-documentation level only, per NBT's
instruction. `source-snapshot/` remains byte-identical to the frozen tree.

### Historical record — the finding this supersedes

Before Pass 3, the audit found (read-only):

- **No** `LICENSE`, `COPYING`, `NOTICE` or `COPYRIGHT` file existed at the LUAp0rt
  project root. A recursive scan found such files **only** inside the vendored
  trees `gpsp/`, `LuaPSX/` and `mgba/`.
- `LUAport_GBA_Portability_Report.md` contained **no** licensing section.
- LUAp0rt's own source files carry **no** per-file copyright or license headers.
  **This remains true and is intentional** — see the size-freeze note above.

**No licence was selected, created or implied by the audit itself.** The decision
was made by NBT and is recorded here.

### What the incorporated licenses require of the combined work

This is a statement of what the licence texts say, not legal advice:

1. **The combined work is covered by GPL-2.0-or-later.** gpSP (§3.A) and the
   LuaPSX-derived `runtime/` (§3.C) are both GPL-2.0-or-later and are both
   compiled into `m16cgpsp.bin`. Under GPL-2.0 §2(b), a work distributed as a
   whole that contains or is derived from the Program must be licensed "as a
   whole… to all third parties under the terms of this License." **This applies to
   the binary regardless of whether NBT declares a licence**, and it applies to
   LUAp0rt's own 23 objects as part of that whole.
2. **A license text must accompany the distribution** (GPL-2.0 §1). Preserved here
   in `licenses/`.
3. **Corresponding source must be provided** (GPL-2.0 §3). ✅ **Satisfied** —
   `source-snapshot/`, 207 files, verified byte-identical. See §5, §5.8.
4. **MIT (libretro-common) is permissive and compatible**; its notice must be
   retained, which `licenses/MIT-libretro-common.txt` does.
5. **The practical effect:** NBT can choose GPL-2.0-or-later, or any licence for
   his own code that is compatible with distributing the combined work under
   GPL-2.0-or-later. He **cannot** distribute this binary under a proprietary or
   GPL-incompatible licence while it contains gpSP and the LuaPSX-derived runtime.

### Pass 2B — is GPL-2.0-or-later compatible with every verified component?

**Yes. No component discovered by this audit prevents that choice.** Component by
component, against licences actually read in the frozen tree:

| Component | Verified license | Compatible with GPL-2.0-or-later? |
| --- | --- | --- |
| gpSP | GPL-2.0-or-later (`gpsp/COPYING`, read) | **Yes** — identical terms; it is the reason copyleft applies |
| LuaPSX (incl. the font table) | GPL-2.0-or-later (`LuaPSX/COPYING`, read) | **Yes** — identical terms |
| libretro-common headers | MIT, per file (read) | **Yes** — MIT is permissive and GPL-compatible |
| EmuC0re font *as received* | Distributed under LuaPSX's GPL-2.0-or-later | **Yes** — governed by a licence that was read |
| Luac0re | n/a — **no code incorporated** | **Not applicable** — imposes no constraint |
| mGBA (MPL-2.0) | Not redistributed, not linked | **Not applicable** |

**No GPL-incompatible component was found anywhere in the incorporation set.** The
only licence never read directly is EmuC0re's own (§6.3), and on **both** possible
readings the outcome is the same: if MIT, it is GPL-compatible; and in any case the
copy LUAp0rt distributes already arrives under LuaPSX's GPL-2.0-or-later. **No
discovered component blocks a GPL-2.0-or-later declaration.**

The constraint runs the other way: because gpSP and the LuaPSX-derived `runtime/`
are both GPL-2.0-or-later and both linked, a **proprietary or GPL-incompatible**
licence for the combined binary is **not available** to NBT.

**Action taken in Pass 3, on NBT's explicit written authorisation:** a top-level
`LICENSE` containing the canonical GPLv2 text, plus a `COPYRIGHT` notice declaring
the original LUAp0rt work **GPL-2.0-or-later, © 2026 NBT**.

**Per-file copyright headers were deliberately NOT added** to LUAp0rt's shipping
sources. Adding them would modify the size-frozen tree (16 bytes spare) and
invalidate SHA-256 `16580c90…01efaf2` together with its hardware-validation
record. NBT's instruction was explicit on this point: licensing is carried at
release-documentation level, and the shipping source remains byte-identical.

---

## 5. Corresponding-source audit

**Conclusion: the corresponding-source obligation IS SATISFIED by this package.**

> **Status change — Pass 2C (post-copy audit).** The maintainer executed the §5.6
> allow-list copy commands manually. The corresponding source is now present under
> `source-snapshot/` — **207 files**, every one verified **byte-identical to the
> frozen source that produced the hardware-tested binary**. This section previously
> recorded the obligation as unsatisfied; that finding is now **closed by evidence**,
> not by assumption. The verification record is §5.8.

### 5.1 What GPL-2.0 §3 requires here

The distributed covered executable is `m16cgpsp.bin` (plus its `m16cgpsp.lua`
loader). "Complete corresponding source" is the source for **all 41 linked
objects** — not only gpSP's 12 — plus the scripts and build files used to control
compilation and installation of the executable.

### 5.2 Required vs. present

| Required material | Needed for | In this package? |
| --- | --- | --- |
| gpSP sources for the 12 compiled TUs + their headers | class A | ✅ **Present** (`source-snapshot/gpsp/`, 145 files) |
| libretro-common headers reached by those TUs | class A | ✅ **Present** (`source-snapshot/gpsp/libretro/libretro-common/`, 22 headers) |
| `adapters/gba/**` — 22 compiled TUs + headers | class A (LUAp0rt) | ✅ **Present** (`source-snapshot/adapters/gba/`) |
| `apps/m16cgpsp/main.c` + its `.inc` files | class A (LUAp0rt) | ✅ **Present** (`main.c`, `m16c_splash_art.inc`) |
| `apps/m13bgpsp/m13b_picker.inc` | picker TU | ✅ **Present** |
| `runtime/**` — `shim.c`, `gfx.c`, `platform.c`, `audio.c`, `savedata.c`, headers, `boot.inc`, `fault.inc`, `tables.h` | class B (LuaPSX-derived) | ✅ **Present** (`source-snapshot/runtime/`, incl. `libc/`) |
| `runtime/linker.ld` — linker script | build control | ✅ **Present** |
| `lua/m16c.lua.in` — source of the shipped loader | shipped artifact | ✅ **Present** (38,806 B) |
| `tools/mklua.py`, `elf.py`, `sizereport.py`, `verify_reloc.py`, `check_*.py` | build/verify scripts | ✅ **Present** (`source-snapshot/tools/`, 24 files) |
| `Makefile` — the complete build recipe | build control | ✅ **Present** (`source-snapshot/Makefile`) |
| `LUAport_GBA_Portability_Report.md` | design record (not required) | ✅ Present |
| SHA-256 of every shipping-build source file | integrity/provenance | ✅ `evidence/source-manifest.sha256` (historical) |
| SHA-256 of every **distributed** source file | integrity of this package | ✅ `evidence/release-source-manifest.sha256` (**207 entries, machine-verifiable**) |

**Every row is satisfied.** `evidence/source-manifest.sha256` remains the
historical Pass 1 provenance record; `evidence/release-source-manifest.sha256` is
the new, cleanly parseable manifest covering exactly what this package
distributes.

### 5.3 What is correctly NOT included

- **The GNU toolchain and Python.** GPL-2.0 §3 excepts "anything that is normally
  distributed… with the major components of the operating system on which the
  executable runs." The toolchain is documented in `docs/build-reproduction.md`
  (WSL, `make` 4.4.1, `gcc` 15.2.0) rather than redistributed.
- **Sony PS5 SDK headers or libraries — none are used, so none are needed.** The
  runtime resolves every console entry point at run time by name through
  `sceKernelDlsym` (syscall `0x24F`). A read-only scan for `#include <sce…>`,
  `OpenOrbis`, `PS5SDK` and similar returns **no** matches in the shipping tree.
  The `libSce*.sprx` strings in the Makefile are **module names passed to `dlsym`
  gate assertions**, not SDK files. **No proprietary SDK file is present and none
  would be redistributable.**
- **`gpsp/bios/open_gba_bios.bin`** — not linked (§3.L), so not corresponding
  source for this executable.
- **Game ROMs, BIOS images and user saves** — never part of corresponding source.
- **`mgba/`** — not part of the covered work (§3.G).

### 5.4 Why the copy was performed manually — historical record

Every recursive-copy primitive available to the audit environment (`cp -r`,
`robocopy`, `tar`, `Copy-Item -Recurse`, shell loops) was blocked by sandbox /
permission policy, and each was re-tested. The corresponding-source obligation
covers hundreds of files across six directory trees, which is not practical to
copy one file at a time through a permitted single-operation redirect.

**The copy was therefore performed manually by the maintainer**, using the
allow-list commands in §5.6 verbatim. §5.5 defines what had to be copied; §5.8
records the verification of what actually arrived.

### 5.5 The corresponding-source allow-list — exactly what is required

Derived from the M16C build graph in the `Makefile` (object rules at
`:15330-15366` and `:15473-15702`, include paths at `:15244-15245` and
`:286-287`), **not** from copying the workspace.

**INCLUDE — required to build and modify `m16cgpsp.bin`:**

| # | Path | Why required |
| --- | --- | --- |
| 1 | `Makefile` | The complete build recipe. **Already present.** |
| 2 | `gpsp/**` *(minus `.git/`, minus `bios/`)* | Source for the 12 linked gpSP objects and every header they include; `-Igpsp` is on the flag line. Includes `gpsp/COPYING` and `gpsp/libretro/libretro-common/include/**`, which `-I` also reaches. |
| 3 | `adapters/gba/**` *(incl. `include/`)* | 22 linked LUAp0rt objects plus their headers; `-Iadapters/gba` and `-Iadapters/gba/include`. |
| 4 | `apps/m16cgpsp/**` | `main.c` → `m16cmain.o`, plus `m16c_splash_art.inc`. |
| 5 | `apps/m13bgpsp/m13b_picker.inc` | A **declared prerequisite** of `m16cmain.o` (`Makefile:15474`). |
| 6 | `runtime/**` *(incl. `libc/`, `linker.ld`, `boot.inc`, `fault.inc`)* | 6 linked objects, the freestanding headers (`-Iruntime`, `-Iruntime/libc`), and the linker script named by `M16CLDFLAGS:15271`. |
| 7 | `lua/m16c.lua.in` | The source of the shipped `m16cgpsp.lua` loader. |
| 8 | `tools/**` | `mklua.py` (packages the loader), `sizereport.py`, `check_image.sh`, `verify_reloc.py`, `check_forbidden.py`, `check_closure.py`, `check_symtypes.py`, `elf.py`, `m8_syntax_check.py`, `mkart.py`, `send.py`, and the `*_equiv.c` gate harnesses invoked by `make m16c-verify`. |

**EXCLUDE — deliberately, with the reason:**

| Path | Reason |
| --- | --- |
| `gpsp/bios/` | Contains `open_gba_bios.bin`. **Not corresponding source** — `bios_data.o` is not on the link line (§3.L) — and excluded under the no-BIOS release policy (§7). |
| `gpsp/.git/`, `LuaPSX/.git/` | Repository metadata; provenance is instead pinned by commit hash in §3.A / §3.C. Also avoids re-introducing the BIOS blob through history. |
| `apps/` *(the other 23 milestone dirs)* | `m0diag`, `m1gpsp` … `m18diag` are **not** part of the M16C build graph. Excluded so the snapshot is the covered work's source, not the whole workspace. |
| `lua/` *(the other 25 templates)* | Only `m16c.lua.in` produces the shipped loader. |
| `mgba/` | Not part of the covered work; exclusion proven three ways (§3.G). |
| `LuaPSX/`, `build/`, `assets/`, `.vscode/`, `__pycache__/` | Upstream reference tree, build outputs, non-source material. |
| ROMs, `*.gba`, `*.sav`, `*.hdr`, BIOS, saves, SDK files, credentials | Never corresponding source; none exist in the included trees (verified — see below). |

**Pre-verified:** a recursive scan of all six INCLUDE trees for
`*.gba *.sav *.hdr *.bios *.rom *.bin *.zip *.7z *.png *.jpg *.iso *.img *.pkg
*.elf *.sprx *.prx *.a *.so *.dll *.lib` returns **exactly one hit** —
`gpsp/bios/open_gba_bios.bin`, which rule 2 excludes. `gpsp/tests/` holds only
`.c`/`.S`/`Makefile` code generators, no test ROMs.

### 5.6 Manual copy commands (maintainer runs these)

**Windows PowerShell, run from any directory. These COPY only — nothing is moved,
deleted or modified.** `robocopy` is used because it is the only PS 5.1 primitive
with reliable recursive exclude semantics.

```powershell
$src = "C:\Users\micha\Desktop\LUAport"
$dst = "C:\Users\micha\Desktop\LUAport\release\luap0rt-gba-v0.0.1\source-snapshot"

# 1. gpSP -- EXCLUDING .git and the BIOS directory, plus a belt-and-braces
#    file-level exclusion of binary/ROM/save extensions.
robocopy "$src\gpsp" "$dst\gpsp" /E /XD ".git" "bios" /XF "*.bin" "*.gba" "*.sav" "*.hdr" "*.rom" "*.iso" "*.img"

# 2. LUAp0rt GBA adapter layer (includes adapters\gba\include)
robocopy "$src\adapters" "$dst\adapters" /E /XD "__pycache__"

# 3. The launcher application -- ONLY the M16C app
robocopy "$src\apps\m16cgpsp" "$dst\apps\m16cgpsp" /E

# 4. The one file M16C needs from the M13B app
robocopy "$src\apps\m13bgpsp" "$dst\apps\m13bgpsp" "m13b_picker.inc"

# 5. The PS5 runtime layer (includes libc\, linker.ld, boot.inc, fault.inc)
robocopy "$src\runtime" "$dst\runtime" /E

# 6. Build + verification tooling
robocopy "$src\tools" "$dst\tools" /E /XD "__pycache__"

# 7. The loader template that produces the shipped m16cgpsp.lua
robocopy "$src\lua" "$dst\lua" "m16c.lua.in"
```

> **robocopy exit codes 0-7 mean SUCCESS** (1 = files copied, 2 = extra files,
> 3 = both). Only **8 or above** is a genuine failure. Do not treat a non-zero
> exit as an error without checking.

`Makefile` is already in `source-snapshot/` from Pass 1 and does not need
re-copying.

**Then, and only then**, run the §5.7 audit, regenerate `SHA256SUMS` over the
final package, and verify the copied sources against
`evidence/source-manifest.sha256` — which exists precisely so this copy can be
proven faithful to the frozen build.

**This is a copy of already-frozen source. It requires no build, no clean, no
relink, and no modification of the shipping implementation.**

### 5.7 Post-copy audit — must pass before claiming compliance

**Corresponding-source compliance is NOT satisfied until the copy above has
actually happened and every check below passes.**

| # | Check | Method | Pass condition |
| --- | --- | --- | --- |
| 1 | Required source present | Compare `source-snapshot/` against the §5.5 allow-list | All 8 INCLUDE entries present |
| 2 | Build graph complete | Every source named in `Makefile:15473-15702` resolves inside the snapshot | No missing TU or header |
| 3 | Source matches the frozen build | `sha256sum -c evidence/source-manifest.sha256` — **see the caveat below** | Every pinned file matches — proves no drift |
| 4 | **No `open_gba_bios.bin`** | Recursive name search over `release/` | **Zero hits** |
| 5 | No ROMs / BIOS / firmware | Recursive scan for `*.gba *.rom *.bios *.iso *.img` | Zero hits |
| 6 | No save files | Recursive scan for `*.sav *.hdr` | Zero hits |
| 7 | No stray binaries | Recursive scan for `*.bin` | Exactly 2: `binary/m16cgpsp.bin`, `evidence/m13c-baseline/m13cgpsp.bin` |
| 8 | No commercial assets | Scan for `*.png *.jpg *.jpeg *.bmp` | Zero hits |
| 9 | No proprietary SDK / libs | Scan for `*.sprx *.prx *.a *.so *.dll *.lib` and `#include <sce…>` | Zero hits |
| 10 | No credentials | Scan for `password`, `api[_-]?key`, `secret`, `token`, `BEGIN.*PRIVATE KEY` | Zero real hits |
| 11 | No personal data | Scan for e-mail addresses and non-RFC1918 IPs | Only `255.255.255.255` (broadcast) and `192.168.1.42` (doc example) |
| 12 | No `.git` metadata | Recursive search for `.git` under `release/` | Zero hits |
| 13 | No unnecessary `mgba/` | Recursive search for `mgba` paths under `release/` | Zero hits |
| 14 | License texts present | List `licenses/` | gpSP GPL-2.0, LuaPSX GPL-2.0, libretro-common MIT — each byte-identical to source |
| 15 | Notices correct | Re-read `THIRD_PARTY_NOTICES.md` §2 against the shipped tree | Every class-A/B component's source now marked present |
| 16 | Manifest corresponds to distributed source | Cross-check `evidence/source-manifest.sha256` against the copied files | 1:1 correspondence |
| 17 | `SHA256SUMS` covers the final package | Regenerate, then `sha256sum -c SHA256SUMS` | All files OK, none missing |
| 18 | **Frozen binary unchanged** | Hash `build/m16c/m16cgpsp.bin` and `binary/m16cgpsp.bin` | Both `16580c90…01efaf2` |
| 19 | Shipping tree unmodified | Manifest re-verification (check 3) + binary hashes (check 18) | Zero modifications |

> #### ⚠ Caveat on check 3 — `source-manifest.sha256` is not fully machine-verifiable
>
> **Defect found in Pass 2B.** Eleven manifest lines carry an inline annotation
> *after* the filename — `[v0.0.1 EDIT]`, `(NOT in any shipping object list)`,
> `(BEFORE section 26)`, `(AS RELEASED, with section 26)`. `sha256sum -c` treats
> the annotation as part of the path, so those lines report
> **"FAILED open or read"**. That is a **path-parsing artifact, not a content
> mismatch**, and it is easy to misread as a drift alarm.
>
> **It affects exactly the files that matter most** — all seven v0.0.1-edited
> files are annotated `[v0.0.1 EDIT]`.
>
> **Pass 2B verified all eleven by hashing them directly**, and every one matches
> its manifest value:
>
> ```
> Makefile                         fbb499b7…  MATCH
> adapters/gba/gba_restore.c       eec6d5c4…  MATCH
> adapters/gba/gba_restore.h       60ff8c6a…  MATCH
> adapters/gba/gba_restorefile.c   d3ce2e5a…  MATCH
> adapters/gba/gba_savehdr.c       ac2e13c2…  MATCH
> adapters/gba/gba_savehdr.h       533aa517…  MATCH
> tools/gba_restore_equiv.c        a10edca8…  MATCH
> runtime/net.c                    da6f1e56…  MATCH
> runtime/net.h                    c3d7c3c3…  MATCH
> LUAport_GBA_Portability_Report.md 7e1f00d5… MATCH (the "AS RELEASED" value)
> ```
>
> **Combined result: 121 verified mechanically + 11 verified by hand = 132/132
> files match. Zero drift across the entire shipping source tree.**
>
> When running check 3, expect 121 `OK` and 11 parse failures, and verify those
> eleven by hand. **Do not treat them as drift without checking.** Cleaning up the
> manifest's format would be a post-v0.0.1 change to an evidence file whose own
> hash is already published in `SHA256SUMS`, so it is **not** done here.

---

### 5.8 Post-copy verification record — Pass 2C

The maintainer executed the §5.6 commands. This is what the audit found in the
snapshot **as it actually exists**, not as the commands intended.

**Completeness — 207 files distributed:**

| Tree | Files | Purpose |
| --- | --- | --- |
| `source-snapshot/gpsp/` | 145 | emulator core + libretro-common headers + `COPYING` |
| `source-snapshot/adapters/gba/` | 48 | LUAp0rt GBA adapter layer (+ `include/`) |
| `source-snapshot/runtime/` | 19 | LuaPSX-derived PS5 runtime (+ `libc/`) |
| `source-snapshot/tools/` | 24 | build + verification scripts and equivalence harnesses |
| `source-snapshot/apps/m16cgpsp/` | 2 | `main.c`, `m16c_splash_art.inc` |
| `source-snapshot/apps/m13bgpsp/` | 1 | `m13b_picker.inc` |
| `source-snapshot/lua/` | 1 | `m16c.lua.in` (38,806 B) |
| `source-snapshot/` (root) | 2 | `Makefile`, `LUAport_GBA_Portability_Report.md` |

**Integrity — zero drift.** Rather than rely on manifest parsing, the audit ran a
**full recursive byte comparison** (`diff -rq`) of every copied tree against its
frozen original. Result: **no differing file anywhere.** The only reported
differences were the three *intended* exclusions:

```
Only in gpsp:  .git      (VCS metadata — excluded)
Only in gpsp:  bios      (contains open_gba_bios.bin — excluded by policy)
Only in tools: __pycache__  (build cache — excluded)
```

This is a stronger result than a hash manifest: every distributed source file is
**byte-identical to the source that produced the hardware-tested binary**.

**Exclusion correctness — proven, not assumed.** The copy also requested
`/XF *.bin *.gba *.sav *.hdr *.rom *.iso *.img`. Because `diff -rq` reported **no**
"Only in …" entries beyond the three above, those filters removed **nothing else**
— no such file existed anywhere in `gpsp/` outside `bios/`. **The gpSP source
required by the M16C build is therefore complete despite the exclusions.**

**A file worth naming explicitly: `gpsp/bios_data.S` is present, and that is
correct.** It contains **no BIOS bytes** — it is a 12-line stub whose only payload
directive is `.incbin "bios/open_gba_bios.bin"`, resolved at assembly time from a
file this package deliberately omits. It is also **not built by M16C**: the
Makefile lists it among the excluded gpSP TUs, and `open_gba_bios_rom` appears
**nowhere in the frozen link map**. Excluding `gpsp/bios/` therefore cannot break
corresponding source for this executable.

**Machine-verifiable manifest.** `evidence/release-source-manifest.sha256` pins all
**207** distributed source files. Unlike the historical manifest it carries **no
inline annotations**, so it verifies cleanly:

```
sha256sum -c --quiet evidence/release-source-manifest.sha256
→ no output = all 207 OK, 0 failed
```

**Prohibited-content scan — clean.** A scan of the snapshot for 23 prohibited
extensions (`*.bin *.gba *.sav *.hdr *.rom *.iso *.img *.sfc *.nes *.srm *.bios
*.elf *.so *.dll *.a *.o *.sprx *.pem *.key *.p12 *.pfx *.env`) returned **zero
hits**. No `open_gba_bios.bin`, no `.git/` directory, no `mgba/`, no `LuaPSX/`
tree. Credential and personal-data scans returned **zero** real hits — the only
matches were English prose ("secretly", "inspectors") and upstream gpSP GPL
headers carrying Exophase's own published contact address, which is required
attribution.

**Three upstream dotfiles are included** — `gpsp/.gitignore`, `gpsp/.gitlab-ci.yml`,
`gpsp/.travis.yml`. These are ordinary upstream text config files, **not `.git`
repository metadata**, and are covered by the manifest.

---

## 6. Open items and ambiguities — NOT resolved by assumption

### 6.1 Corresponding source — **RESOLVED IN PASS 2C**

Previously release-blocking. The maintainer performed the allow-list copy and the
audit verified the result: **207 files, byte-identical to the frozen source, zero
drift**, with all prohibited content absent and a clean machine-verifiable
manifest. **GPL-2.0 §3 is satisfied by this package.** See §5.8.

### 6.2 LUAp0rt project license — **RESOLVED IN PASS 3**

Previously the last release-blocking item. **NBT explicitly authorised
GPL-2.0-or-later for the original LUAp0rt work**, © 2026 NBT. The canonical GPLv2
text is at `LICENSE` (verbatim, SHA-256 `189b1af9…`) and the project notice at
`COPYRIGHT`. Third-party components are **not** relicensed and retain their own
terms and attribution. See §4.

### 6.3 EmuC0re attribution unconfirmed — **DOWNGRADED IN PASS 2B**

**Previously "license undetermined — release-blocking". Pass 2B reduced this to an
attribution question**, on hash evidence.

**What is now established.** The incorporated font data is a **byte-identical copy
of `LuaPSX/src/tables.h`** (both SHA-256 `ca8224fe…`, both 2,811 bytes). LUAp0rt
received the font from LuaPSX, not from EmuC0re, so the copy distributed here is
governed by **LuaPSX's GPL-2.0-or-later**, whose full text is present and verbatim
in `licenses/`. **There is no license gap in the chain LUAp0rt actually relied
on**, and no scenario found in which this font makes the release undistributable.

**What remains open.** EmuC0re's *own* license text and copyright holder of record
were not read. No copy of EmuC0re exists in this workspace.

**Pass 3 retrieval attempt — made, and failed.** Network tooling became visible in
the Pass 3 environment, so retrieval was attempted rather than assumed impossible:

| Attempt | Target | Result |
| --- | --- | --- |
| `WebFetch` | `https://raw.githubusercontent.com/egycnq/EmuC0re/main/LICENSE` | **Denied — permission not granted** |
| `WebSearch` | `EmuC0re egycnq GitHub LICENSE MIT copyright` | **Denied — permission not granted** |

NBT reports that external verification after Pass 2C found the current official
EmuC0re repository identifies EmuC0re as **MIT licensed**. **That report is
recorded here as reported, and was NOT independently verifiable in this
environment.** Because the exact upstream copyright line could not be read, **no
MIT notice for EmuC0re was added to `licenses/`, and no copyright line was
invented.** Per NBT's instruction, the optional attribution enhancement was
**stopped**, not faked. Remaining questions:

- What is EmuC0re's exact LICENSE text and copyright line?
- Is the 8x8 font table actually present in that upstream project, or did it enter
  LuaPSX by some other route?
- If MIT: MIT is GPL-2.0-compatible, so there is **no conflict** — but MIT
  requires its copyright notice to be reproduced **verbatim**, and neither
  `runtime/tables.h` nor `LuaPSX/src/tables.h` carries a copyright line to copy.

**Not guessed.** Resolve by reading `https://github.com/egycnq/EmuC0re` and adding
the verbatim notice to `licenses/`. **Replacing the font is explicitly not the
remedy** — it would change the shipping image and invalidate the frozen SHA-256
and its hardware-validation record. `runtime/tables.h` was **not modified**.

**Font provenance, as established and recorded:** `runtime/tables.h` is
byte-identical to LuaPSX's `src/tables.h` copy, and **LuaPSX itself is what
records the font as originating from EmuC0re** (the *"lifted from EmuC0re"*
comment is inside the LuaPSX file and was carried over verbatim). **LUAp0rt
obtained this material through LuaPSX — it is not a direct copy from EmuC0re**,
and no claim of direct copying is made anywhere in this package.

### 6.4 Luac0re — **RESOLVED IN PASS 2B**

**No Luac0re code is incorporated into LUAp0rt** (§3.F). The determination rests
on positive evidence: the Lua layer's stated parent is LuaPSX
(`lua/m0.lua.in:23`), and Luac0re appears in the tree only as a host that LUAp0rt
calls, targets or works around (dlsym-offset correction, absent `syscall.poll`,
500 KB script cap, string-only `tonumber`).

**Consequence:** no Luac0re license text is required, and its unread license terms
do not govern this distribution. Acknowledgement of the delivery relationship is
preserved in the shipped loader and in both attribution documents.

### 6.5 No incompatible licensing was found

Among the licences that **could** be determined — GPL-2.0-or-later (gpSP),
GPL-2.0-or-later (LuaPSX), MIT (libretro-common) — **no incompatibility exists**.
MIT is compatible with GPL-2.0-or-later, and the two GPL components share terms.
**Pass 2B narrowed the residual risk further:** §6.4 is resolved, and §6.3 is now
an attribution question rather than a licensing gap, because the font's governing
licence (LuaPSX, GPL-2.0-or-later) is known and reproduced.

### 6.6 No license requires modifying the frozen implementation

**Nothing found in this audit requires changing the shipping source or binary.**
Every obligation identified is satisfiable by documentation, verbatim licence
texts, and shipping the corresponding source alongside the binary. Notices that
are already embedded in the shipping artifacts — the gpSP headers, the EmuC0re and
Luac0re credits in `m16cgpsp.lua:41-42`, the "BY NBT" credit — are **preserved
unmodified**.

---

## 7. ROM, BIOS and user-content policy

LUAp0rt GBA v0.0.1 contains **no** game content and **no** console firmware.

- **No commercial GBA ROMs** are included — original, patched or test copies.
- **No Nintendo GBA BIOS** is included. The binary embeds no BIOS; `gba_bios.c`
  loads one from a user-supplied file at run time, and `open_gba_bios_rom` is a
  gate-enforced forbidden symbol (§3.L).
- **No open-source BIOS replacement** is included.
- **No user save files** — no `.sav`, no `.hdr`, no save container of any kind.
- **No commercial game artwork, screenshots or extracted assets.**
- **No encryption keys, credentials or proprietary SDK files.**

**Users must supply their own game and firmware files.** LUAp0rt does not
distribute them, does not link to them, and does not direct users to any source
for them. The `.bin` files in this package are **not ROMs**: `m16cgpsp.bin` is the
LUAp0rt shipping payload and `evidence/m13c-baseline/m13cgpsp.bin` is the frozen
v1 payload, both built from the source audited here.

---

## 8. Audit scope and method

Read-only throughout. No file outside `release/luap0rt-gba-v0.0.1/` was created or
modified during this audit, and the shipping binary was hashed before and after
to prove it was untouched.

Sources of evidence:

- license files: `gpsp/COPYING`, `LuaPSX/COPYING`, `mgba/LICENSE`
- per-file headers: `gpsp/common.h`, `gpsp/gba_memory.c`, `gpsp/retro_inline.h`,
  `gpsp/libretro/libretro-common/include/streams/file_stream.h`
- in-source provenance comments across `runtime/`, `adapters/gba/`, `tools/`,
  `lua/`
- vendored git metadata: `gpsp/.git/{config,HEAD,packed-refs}`,
  `LuaPSX/.git/{config,refs/heads/main,packed-refs}`
- the link line and include paths: `binary/m16cgpsp.map`, `Makefile:286-287`,
  `Makefile:15244-15245`
- gate definitions: `tools/check_forbidden.py`
- project record: `LUAport_GBA_Portability_Report.md` §14, §21, §23
