# LUAp0rt GBA v0.0.1 — CREDITS

This file records who made what. It is written from **evidence found in the frozen
v0.0.1 source snapshot** — license files, copyright headers, in-source provenance
comments, vendored git metadata and the linker map — not from recollection.

The companion file `THIRD_PARTY_NOTICES.md` carries the formal per-component
license detail. `licenses/` carries the verbatim license texts.

> **Project licence — declared.** LUAp0rt's original work is
> **GPL-2.0-or-later, Copyright (C) 2026 NBT** (see §1). The canonical GPLv2 text
> is at `LICENSE`; the project notice is at `COPYRIGHT`.
>
> **One open provenance item remains, recorded in §5.** Pass 2B resolved the
> Luac0re question and narrowed the EmuC0re question to a single attribution
> point; both are stated plainly rather than assumed away.

---

## 1. LUAp0rt — original work and integration

**LUAp0rt — Created by NBT.**

```
Original LUAp0rt code:
Copyright (C) 2026 NBT
Licensed under GNU GPL-2.0-or-later.
```

**LUAp0rt GBA v0.0.1 was developed and hardware-validated by NBT.**
Full licence text: `LICENSE` (canonical GPLv2, verbatim). Project notice:
`COPYRIGHT`. This covers NBT's **original** work only — upstream projects retain
their own copyrights, licences and attribution (§2, §3).

The shipping binary credits NBT on its own title screen
(`apps/m16cgpsp/main.c:1814`, `draw_str(... "BY NBT" ...)`), which is the
project's own record of authorship.

Original LUAp0rt work in the v0.0.1 shipping image comprises the entire
**GBA adapter layer** and the **launcher application** — 23 of the 41 objects on
the link line:

```
adapters/gba/   m13store.o  gba_library.o  gba_fixture.o  gba_filestream.o
                gba_compat.o  gba_probe.o  gba_bios.o  gba_rom.o
                gba_save.o  gba_savefile.o  gba_savehdr.o  gba_restore.o
                gba_restorefile.o  gba_sramobs.o  gba_m8map.o  gba_m10map.o
                gba_exec.o  gba_present.o  gba_input.o  gba_r7input.o
                gba_audio.o  gba_session.o
apps/m16cgpsp/  m16cmain.o
```

This layer is where the substance of the project lives, including:

- the **persistent save stack** — header format, class compatibility, restore and
  re-verify logic, and the FLASH64 defect fix that defines v0.0.1
- the **session boundary** that lets the picker come back without restarting the
  payload
- the **ROM library, picker and frontend**
- the **input mapping** (frozen M8 table plus R7 canonicalisation)
- the **gpSP-facing adapters**, including the `RFILE` pool that replaces
  libretro's VFS, the BIOS gate, and demand paging
- the **build system, gates and host equivalence harnesses** (`Makefile`,
  `tools/*.c`, `tools/*.py`)

**gpsp/ was not modified.** The adapter layer exists precisely so that the
upstream emulator core could be consumed unchanged; this is asserted throughout
the source (for example `adapters/gba/gba_bios.c:16` — *"gpsp/ IS NOT MODIFIED.
Not one line."*).

---

## 2. Third-party code actually incorporated into the shipping binary

### gpSP — the GBA emulator core

**Copyright (C) 2006 Exophase <exophase@gmail.com>**, with subsequent work by
**notaz**, **davidgfnet** and the **libretro** contributors.

- License: **GPL-2.0-or-later** (`licenses/GPL-2.0-gpSP-COPYING.txt`)
- Upstream: `https://github.com/libretro/gpsp.git`
- Pinned at commit **`8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f`** (branch `master`)
- Lineage: Exophase's original gpSP → notaz's fork → the libretro fork maintained
  by davidgfnet

gpSP is the emulator. It contributes **12 of the 41 linked objects** — the CPU
core, memory system, video, sound, serial/RFU/GBP, cheats, savestate and input.
**This is the largest single body of code in the shipping image and the reason
the release is copyleft.**

### libretro-common — RetroArch support headers

**Copyright (C) 2010-2020 The RetroArch team.**

- License: **MIT-style, stated per file** (`licenses/MIT-libretro-common.txt`)
- Present as part of the vendored gpSP tree

gpSP source files that *are* compiled into v0.0.1 include libretro-common headers
(`gpsp/gba_memory.c:21` includes `streams/file_stream.h`; `gpsp/gba_memory.h:23`
includes `libretro.h`), so these headers are part of the shipping build even
though **none of libretro-common's `.c` files is compiled or linked** — LUAp0rt
supplies its own `RFILE` implementation in `adapters/gba/gba_filestream.c`.

### EmuC0re — the 8x8 bitmap font (received via LuaPSX)

**egycnq / EgyDevTeam**, as attributed by LuaPSX.

`runtime/tables.h:6` states the font table is *"lifted from EmuC0re"*. The table
(`font_data`) is compiled into the shipping image and is what `draw_char()` in
`runtime/gfx.c` renders, so **this is incorporated third-party data, not merely
an influence**.

**Pass 2B established the transmission path by hash.** LUAp0rt did not take this
font from EmuC0re directly:

```
runtime/tables.h      SHA-256 ca8224fe3c055ec4b6a2b732685367d3dc1ea7ea73cde772e020f9d436a4dbb6
LuaPSX/src/tables.h   SHA-256 ca8224fe3c055ec4b6a2b732685367d3dc1ea7ea73cde772e020f9d436a4dbb6
```

The two files are **byte-identical** (2,811 bytes each), including the
*"lifted from EmuC0re"* comment itself. The font table therefore reached LUAp0rt
as a **verbatim copy of a LuaPSX file**, and the immediate upstream governing the
copy LUAp0rt distributes is **LuaPSX, GPL-2.0-or-later** (§3) — a license that is
present, read, and reproduced verbatim in `licenses/`.

> **What remains open is attribution, not license coverage.** EmuC0re's own terms
> and copyright holder of record still could not be read from any authoritative
> source available to this audit. See §5.

---

## 3. Code materially adapted or derived from another project

### LuaPSX — the PS5 runtime layer

**soniciso1** — `https://github.com/soniciso1/LuaPSX.git`, local clone at commit
**`d2794ef1c1f3653dfc713f21ac194694a6c58b94`** (branch `main`, tag `v1.0` present).

- License: **GPL-2.0-or-later** (`licenses/GPL-2.0-LuaPSX-COPYING.txt`;
  LuaPSX's own README states *"GPL-2.0-or-later, inherited from PCSX-ReARMed"*)

**LUAp0rt's `runtime/` layer is a derivative of LuaPSX's runtime, and the source
says so in its own headers.** This is not an architectural resemblance — the
project records byte-level identity in places:

| LUAp0rt file | Relationship, as stated in the source |
| --- | --- |
| `runtime/shim.c` | `runtime/platform.h:118` claims *"byte-identical to LuaPSX/src/shim.c"*. **Pass 2B correction: that claim is now narrowly stale** — the files differ by exactly one *additive* block, `shim.c:41-75`, guarded by `#ifdef LUAPORT_SESSION_REUSE` (`arena_mark()` / `arena_rewind()`, added at M16). There are **no deletions and no other edits**, so the file is LuaPSX's with one guarded addition. |
| `runtime/gfx.c` | *"Adapted from LuaPSX/src/ui.c lines 1-75. The bodies below are byte-identical to that file"* (`runtime/gfx.c:4`) |
| `runtime/savedata.c`, `runtime/savedata.h` | *"Ported from LuaPSX/src/savedata.c and LuaPSX/src/savedata.h. This is a PORT"* (`runtime/savedata.h:9`) |
| `runtime/core.h` | *"Adapted from LuaPSX/src/core.h"*; §`:73` records a block *"byte-identical to LuaPSX/src/core.h:78-80"* |
| `runtime/platform.h`, `runtime/platform.c` | *"Adapted from LuaPSX/src/main.c lines 300-376 … and 801-809"* (`runtime/platform.h:10`) |
| `runtime/audio.c`, `runtime/audio.h` | *"Adapted from LuaPSX/src/main.c:396-440 (bring-up), :756-761 (submission)"* (`runtime/audio.c:7`) |
| `runtime/linker.ld` | *"Adapted from LuaPSX/linker.ld"* (`runtime/linker.ld:5`) |
| `runtime/libc/*.h` | freestanding headers whose comments still point at LuaPSX's layout (*"Implemented in src/shim.c"*) |
| `runtime/boot.inc`, `runtime/fault.inc`, `runtime/tables.h` | **verbatim copies** — proven byte-identical by hash in the table below |
| `adapters/gba/m13store.h:160` | extension-matching logic *"Carried from LuaPSX/src/discs.c:19-28"* |
| `tools/send.py` | *"Adapted from LuaPSX/send2.py, unchanged in protocol"* |
| `tools/verify_reloc.py`, `tools/check_symtypes.py` | *"Adapted from LuaPSX/tools/…"* |

Six of the 41 linked objects — `shim.o`, `gfx.o`, `platform.o`, `audio.o`,
`savedata.o` and the headers behind them — originate here.

**Pass 2B — thirteen files proven byte-identical to their LuaPSX originals.**
These are not adaptations; they are verbatim copies, established by SHA-256 over
both trees:

| LUAp0rt file | LuaPSX original | SHA-256 (identical in both) |
| --- | --- | --- |
| `runtime/tables.h` | `LuaPSX/src/tables.h` | `ca8224fe3c055ec4b6a2b732685367d3dc1ea7ea73cde772e020f9d436a4dbb6` |
| `runtime/boot.inc` | `LuaPSX/src/boot.inc` | `34213a3e58d78acbdb6fb35d523ff5b32b0593c8a8d18659c0a33a5c604364e4` |
| `runtime/fault.inc` | `LuaPSX/src/fault.inc` | `5cd94f952059f9692b37d1c52c1df029ef8a264eeaadf6f298371c2f2d44cb49` |
| `runtime/libc/assert.h` | `LuaPSX/src/libc/assert.h` | `20452ffa9a3904f569efe8b28834745731aa75a8b9a39d24c62f46062c86c245` |
| `runtime/libc/inttypes.h` | `LuaPSX/src/libc/inttypes.h` | `120ba3f162835f649e0d9e6346bf23617a6d2fde5852cfa8f515f6f2cc7f77f3` |
| `runtime/libc/malloc.h` | `LuaPSX/src/libc/malloc.h` | `cf34efdacf01435b5912ef97e532c43dcffab482987d50a900700fdc1e7e61f3` |
| `runtime/libc/math.h` | `LuaPSX/src/libc/math.h` | `74f9d31379b7d0ee33b0df65dc4e63fca33d22252a15fc0ad842a2f8cc447913` |
| `runtime/libc/setjmp.h` | `LuaPSX/src/libc/setjmp.h` | `efeda0cd2287f3cd875f496747fe20d3477b3d9818f687217ab3ef4c3ec4bb68` |
| `runtime/libc/stdio.h` | `LuaPSX/src/libc/stdio.h` | `3d41fbd1e6507c8a55790defcdcf3a3228edb74a59f418a564b920a1fbe77f43` |
| `runtime/libc/stdlib.h` | `LuaPSX/src/libc/stdlib.h` | `defe05a39873dc0dfc6f424294ab1397a00fdc5f62aacb7aea15900a9347e5fb` |
| `runtime/libc/string.h` | `LuaPSX/src/libc/string.h` | `9a0c7e863a3a70bd0672c89ee18b3b9601f3ac077df369f3b9e255d9c66c0671` |
| `runtime/libc/time.h` | `LuaPSX/src/libc/time.h` | `6ec5ae70b313e82e1e6531713675092e7c3d3b6fdfdfc242ae7bc965957f184f` |
| `runtime/libc/unistd.h` | `LuaPSX/src/libc/unistd.h` | `bb477b5ad1abdb525685d2f52dd7570b6debebaab26ad53f38dd6e4f03f6a8d9` |

The shipped Lua loader is also a LuaPSX derivative: `lua/m0.lua.in:23` opens
*"ADAPTED FROM LuaPSX/lua/psx.lua.in"* and enumerates what was removed;
`lua/m16c.lua.in` descends from it with the transport unchanged
(`lua/m16c.lua.in:32-38`).

**This strengthens, rather than changes, the §3 conclusion:** LuaPSX is a
GPL-2.0-or-later upstream whose code is present in the shipping binary both
verbatim and in adapted form.

**Consequence:** LuaPSX is **not** a reference-only project for LUAp0rt. It is a
GPL-2.0-or-later upstream whose code was adapted into the shipping binary, and it
is a second independent reason the combined work is copyleft.

---

## 4. Reference, inspiration, and platform substrate — code NOT incorporated

These projects shaped LUAp0rt or host it at runtime. **No code from them is
compiled into the v0.0.1 binary** (with the single, separately-stated exception of
the EmuC0re font in §2).

- **Luac0re** — *Gezine*. The Lua/JIT delivery substrate.
  **Pass 2B resolved this relationship: LUAp0rt incorporates no Luac0re code.**
  The relationship is *interoperation with a runtime host*, and every piece of
  evidence points the same way:
  - **Every derivation claim in the tree names LuaPSX, LuaMD or LuaGB — never
    Luac0re.** The shipped loader's own ancestry is stated as
    *"ADAPTED FROM LuaPSX/lua/psx.lua.in"* (`lua/m0.lua.in:23`).
  - Luac0re appears only as something LUAp0rt **calls, targets or works around**:
    resolving `sceKernelDlsym` *before* Luac0re's own guess is used
    (`lua/m16c.lua.in`-lineage, `lua/m0.lua.in:125`), compensating for a wrong
    built-in dlsym offset (`lua/m0.lua.in:56-59`), avoiding a missing
    `syscall.poll` in its table, respecting its 500 KB script cap, and handling
    its string-only `tonumber`. These are consumer accommodations to a host.
  - No Luac0re source file, header or fragment exists anywhere in this workspace,
    and nothing in the binary or the loader is attributed to it.

  **Consequence: no Luac0re license text is required in `licenses/`**, because no
  Luac0re code is redistributed. Acknowledgement of the platform/delivery
  relationship is preserved — in this file, in `THIRD_PARTY_NOTICES.md`, and in
  the shipped loader itself (`m16cgpsp.lua:42`, *"Delivery substrate: Luac0re by
  Gezine."*), which is untouched.
- **EmuC0re** — *egycnq / EgyDevTeam*. Credited throughout as *"PS5 runtime
  pattern"* (`runtime/core.h:12`, `m16cgpsp.lua:41`): video/audio/pad bring-up,
  the shellcode ABI and the picker are patterned on it. That pattern influence is
  reference material; the font table in §2 is the one piece of actual code.
- **mGBA** — evaluated as an alternative emulator core in report §14 and **not
  selected**; gpSP was chosen. mGBA is a development/reference tree only. Its
  absence from the build is proven, not assumed: no `-Imgba` appears in any M16C
  include path, and no mGBA object appears on the link line. License MPL-2.0.
  **Not redistributed with this release**, and its license text is therefore not
  required in `licenses/`.
- **LuaGB** and **LuaMD** — *soniciso1*. Predecessors in the same runtime lineage
  (`runtime/audio.c:22`: *"LuaGB -> LuaMD -> LuaPSX -> LUAport"*). Their influence
  reaches v0.0.1 through LuaPSX rather than directly.
- **PCSX / PCSX-df / PCSX-ReARMed** — *notaz and the PCSX teams*. The origin of
  LuaPSX's GPL-2.0-or-later license lineage. **No PlayStation emulation code is in
  the GBA build**; the relevance is licensing, not code.
- **mast1c0re** — *CTurt* and *McCaulay*. The underlying exploit research Luac0re
  builds on.

---

## 5. Open provenance items — unresolved

**These are stated, not assumed. Nothing here has been resolved by guessing.**

**RESOLVED in Pass 2B — Luac0re.** Previously item 2. LUAp0rt incorporates no
Luac0re code (§4), so Luac0re's license does not govern this distribution and no
license text is required. Acknowledgement is preserved.

**STILL OPEN — EmuC0re attribution.** Pass 2B materially narrowed this item but
did not close it.

- **What is now established.** The incorporated font data is a **byte-identical
  copy of `LuaPSX/src/tables.h`** (§2). The copy LUAp0rt distributes therefore
  arrives under **LuaPSX's GPL-2.0-or-later**, whose full text is present and
  reproduced verbatim in `licenses/GPL-2.0-LuaPSX-COPYING.txt`. There is no
  licence *gap* in the chain LUAp0rt actually relied on.
- **What is still unresolved.** EmuC0re's own license terms and its copyright
  holder of record could not be read from any authoritative source available to
  this audit: **no copy of EmuC0re exists in this workspace**, and the upstream
  repository could not be inspected. **Pass 3 attempted retrieval and was
  denied** — `WebFetch` and `WebSearch` are both present in that environment but
  **lack permission**. NBT reports that external verification found the official
  EmuC0re repository states **MIT**; that is recorded **as reported** and was not
  independently verifiable here, so **no MIT notice was added and no copyright
  line was invented.**
- **Why it still matters.** If EmuC0re is MIT — as the upstream repository is
  reported to state — MIT is GPL-2.0-compatible and there is no licence conflict;
  but MIT also **requires its copyright notice to be reproduced**, and the exact
  holder string cannot be reproduced verbatim without reading it. Neither
  `runtime/tables.h` nor `LuaPSX/src/tables.h` carries a copyright line — only the
  prose attribution *"lifted from EmuC0re"*.
- **What is needed to close it.** Read `https://github.com/egycnq/EmuC0re` and
  confirm (a) its LICENSE file and exact copyright line, and (b) that the 8x8
  font table is actually present in that MIT-licensed project. Then add the
  verbatim notice to `licenses/`.

**The font has not been replaced, `runtime/tables.h` has not been modified, and
the binary has not been touched.**

**This item does not block release.** The copy LUAp0rt distributes is governed by
a licence that is present and verbatim (LuaPSX, GPL-2.0-or-later), and the EmuC0re
credit is carried in the shipped loader and in `runtime/core.h`.

---

## 6. Content not included in this release

LUAp0rt distributes **no** game content and **no** firmware.

- **No commercial GBA ROMs**, patched or otherwise.
- **No Nintendo GBA BIOS.** The binary contains no BIOS: `gba_bios.c` loads one
  from a user-supplied file at runtime, and `open_gba_bios_rom` is an explicitly
  **forbidden symbol** enforced by `tools/check_forbidden.py` against the linked
  image.
- **No open-source BIOS replacement.** `gpsp/bios/open_gba_bios.bin` exists in the
  upstream gpSP tree but is excluded from this package and is not linked.
- **No user save files** of any kind.
- **No game artwork, screenshots or extracted assets.**

Users must supply their own game and firmware files. **LUAp0rt does not provide,
link to, or direct users toward any ROM or BIOS source.**

---

## 7. Thanks

LUAp0rt is a GBA adapter layer and launcher built on other people's emulator and
other people's PS5 runtime work. The emulation is Exophase's, notaz's,
davidgfnet's and the libretro contributors'; the console runtime descends from
egycnq's EmuC0re through soniciso1's LuaGB, LuaMD and LuaPSX; and none of it runs
without Gezine's Luac0re, which in turn stands on CTurt's and McCaulay's
mast1c0re research. The hard parts are theirs.
