# LUAp0rt GBA v0.0.1 — RELEASE / FREEZE RECORD

**Status: FROZEN**
**Declared: 2026-09-22**
**Basis: hardware validation complete across all five save classes, plus controls, picker, and session lifecycle.**

This document is the authoritative, self-contained release record for LUAp0rt GBA
v0.0.1. It is written to be readable without the development history, and every
figure in it is either copied from a preserved artifact in this package or derived
from one by a stated method.

---

## 0. NON-MODIFICATION STATEMENT

> **The v0.0.1 shipping source and binary are SIZE-FROZEN and MUST NOT be modified.**
>
> Release preparation was **additive and read-only** with respect to the shipping
> baseline. No source file, no `Makefile` behaviour, no string, comment or UI text,
> no save logic, no `gpsp/**` file, and no build artifact under `build/m16c/` was
> edited, regenerated, cleaned, optimised or tidied. No diagnostics were added.
>
> The image's pre-`0x50000` margin is **2,016 bytes** against a **2,000-byte hard
> floor** — **16 bytes spare**. There is **no edit to the shipping source that is
> safe by inspection.**
>
> Any change that reaches the shipping image invalidates SHA-256
> `16580c90…01efaf2` and the entire hardware-validation record attached to it, and
> is **post-v0.0.1 work requiring its own gate run, its own regeneration, and its
> own hardware validation.**

---

## 1. Artifact identity

The shipping unit is a **pair**. The console cannot be fed the `.bin` alone — the
`.lua` loader carries the JIT reservation computed from the ELF symbol table and is
what the remote loader actually receives.

| Role | File | Size (bytes) | SHA-256 |
| --- | --- | ---: | --- |
| **Shipping binary** | `binary/m16cgpsp.bin` | **336,032** | `16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2` |
| **Loader (ships with it)** | `binary/m16cgpsp.lua` | 38,801 | `1857705345f543fb78ac624211cbcb3a08f3cb48edeef7d80808de1eef87f624` |
| Symbol/provenance witness | `binary/m16cgpsp.elf` | 403,392 | `451e97001f62fa7f5886316970b556b66e903671cffd54318a0ff0c2c1b1d700` |
| Measurement source | `binary/m16cgpsp.map` | 69,721 | `4429a01831d349c4d1876651cacaee407a46a7c0d3780cd73037b6e4afb4c6c3` |

Built 2026-09-22 12:09:33 (binary/ELF/map); loader generated 12:12:06.

**M16C is the sole shipping artifact.** M16-0 and M16-1 are diagnostics
(`Makefile:15190-15203`) and cannot write saves.

Verify with `sha256sum <file>` or
`Get-FileHash -Algorithm SHA256 <file>`.

---

## 2. Resource measurements

All figures below are read from `binary/m16cgpsp.map` and `evidence/reloc.log`,
both preserved in this package. **No rebuild, relink or size run was performed to
produce this table.**

| Item | Value | Hex / note |
| --- | ---: | --- |
| `.text` output section | 320,144 | `0x4E290` — contains `.text` + alignment + `.rodata` |
| ├─ `.text` input | 266,656 | |
| ├─ alignment padding | 4 | |
| └─ `.rodata` | 53,484 | corroborated by `build/m16c/rodata.bin` |
| `.rela.dyn` | 5,520 | `0x4E290`→`0x4F820`, **230 entries** |
| `__data_load` | 325,664 | `0x4F820` |
| `.data` | 10,368 | `0x2880` |
| `.bss` | 2,011,456 | `0x1EB140`; ceiling 2,097,152 → 85,696 spare |
| `__data_start` | 393,216 | `0x60000` — writable region begins |
| `__bss_end` | 2,414,016 | `0x24D9C0` |
| JIT footprint | 393,216 | = `__data_start` |
| JIT budget (`JIT_LIMIT`) | 524,288 | `Makefile:53` |
| **JIT headroom** | **131,072** | 75.0% of budget used |
| Final binary | 336,032 | |

### 2.1 The governing constraint — pre-`0x50000` margin

`__data_start` steps in 0x10000 increments as a function of `__data_load`
(`Makefile:3475-3477`):

```
__data_load <= 327,679  ->  __data_start 393,216   headroom 131,072   <- CURRENT
__data_load <= 393,215  ->  __data_start 458,752   headroom  65,536
__data_load <= 458,751  ->  __data_start 524,288   headroom       0   FAIL
```

Everything loaded before `0x50000` is `.text` output plus `.rela.dyn`:

```
__rela_start  0x0004E290  =  320,144
__rela_end    0x0004F820  =  325,664   ( = __data_load )
ceiling       0x00050000  =  327,680
------------------------------------------------------
MARGIN                        2,016 bytes
hard floor                    2,000 bytes
SPARE                            16 bytes
```

**Cross-checks:** `0x4E290` = 320,144 equals the size of `build/m16c/text.bin`
exactly; `0x4F820 − 0x4E290` = 5,520 equals the byte count `evidence/reloc.log`
states for `.rela.dyn`. Two independent artifacts agree with the arithmetic.

> **Correction of record.** The working figure carried during development was a
> margin of 2,020 with 20 bytes spare. The map-derived value is **2,016 with 16
> bytes spare** — 4 bytes tighter. The 4 bytes are the `.text`→`.rodata` alignment
> pad. The release record uses the map-derived figure because the map is the
> authoritative artifact and is preserved here for independent re-derivation. The
> conclusion is unchanged and slightly strengthened: **the image has single-digit
> tens of bytes of headroom and no shipping-source edit is safe by inspection.**

---

## 3. Verification evidence (host gates)

All gates were run by the operator under WSL with `HOSTCC=gcc`. Full detail in
`evidence/host-gates.md`.

| Gate | Result |
| --- | --- |
| `make m12c-equiv` | **958 checks, 0 failures** |
| `make m16c-save-equiv` (session-reuse) | **1013 checks, 0 failures** |
| `make r7-input-equiv` | **688 checks, 0 failures** |
| `make m13b-equiv` | **283 checks, 0 failures** |
| `make m16c-verify` | **`M16C VERIFY OK`** |
| Relocations | **230** `R_X86_64_RELATIVE`, all targets writable and in range |
| Relocation verdict | **safe to apply** |

---

## 4. Hardware validation

Full matrices in `evidence/hardware-matrix.md`. Summary: **all five save classes
PASS on hardware**, including the two that v1 could only characterise from source.

| Area | Result |
| --- | --- |
| SRAM / FLASH64 / FLASH128 / EEPROM512 / EEPROM8K | **HARDWARE PASS** (all five) |
| R7 controls | **HARDWARE PASS** |
| Return-to-picker chord | **HARDWARE PASS** |
| Picker / details / frontend | **HARDWARE PASS** |
| Gameplay, audio, presentation, demand paging, session lifecycle | **HARDWARE PASS** |

**This supersedes report §23.2** for FLASH64 and EEPROM8K, which it recorded as
*SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED*. The five-term status vocabulary of
§23.1 is retained unchanged.

---

## 5. The FLASH64 defect — closure

Full narrative and code references in `evidence/flash64-closure.md`.

- **Defect:** valid ROM-bound LGS1 FLASH64 saves were rejected
  `SAVE CORRUPT / CLASS INCOMPATIBLE` on a fresh launch, because the live backup
  class is `BACKUP_UNKN` at preboot.
- **Root cause:** `adapters/gba/gba_savehdr.c:249-252` — the class-compatibility
  gate had no evidence path for a legitimately-UNKNOWN live class.
- **Fix:** keyed evidence + late activation, across five production files, **every
  addition behind `#ifdef LUAPORT_SESSION_REUSE`**.
- **Test-first evidence:** harness sections `[10f]`/`[10g]` went
  **RED 1013/36 → GREEN 1013/0** with the test count unchanged.
- **Non-regression:** M12C 958/0 before and after; **M13C SHA byte-identical**.
- **Hardware closure:** Mother 3 (FLASH64 restore + full lifecycle) and
  The Sims 2 (create → relaunch → load).

---

## 6. Relationship to GBA v1 (M13C)

**GBA v1 remains frozen and is NOT superseded.** v0.0.1 is a separate, additive
shipping artifact.

```
build/m13c/m13cgpsp.bin
  size    : 308,784 bytes
  SHA-256 : e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a
```

This binary was verified **byte-identical after the entire FLASH64 fix** — the
proof that every Phase 2 edit compiled out of all pre-session-reuse milestones. A
witness copy is preserved at `evidence/m13c-baseline/m13cgpsp.bin`; see
`evidence/m13c-frozen.txt`.

---

## 7. Controls and return-to-picker

Full detail in `docs/controls.md`.

- **Frozen M8 table** (`adapters/gba/gba_input.c`) is the sole authority on
  pad-bit → GBA-key meaning. Invariants `M8_ST_UNIQUE` and `M8_ST_UNMAPPED` hold
  verbatim.
- **R7 canonicalisation** (`adapters/gba/gba_r7input.c`) *routes* without
  redefining: `CROSS|CIRCLE` → **GBA A**, `SQUARE|TRIANGLE` → **GBA B**.
- **Return-to-picker chord:** all four shoulders,
  `PAD_CHORD = PAD_L1|PAD_R1|PAD_L2|PAD_R2` (`runtime/platform.h:183`), held
  **15 frames** (`M16C_EXIT_HOLD`, `apps/m16cgpsp/main.c:570`).
- **Returning to the picker is not save permission** (`main.c:85-87`).

---

## 8. Known limitations

Full list in `docs/limitations.md`. Headlines:

- **16 bytes** of size margin; any shipping-source change is a size-gate event.
- **240-minute watchdog is shipped behaviour**; reaching the cap does **not**
  commit that session's save. Progress-based hang detection remains deferred.
- **Netplay / M18 is not shipped**; `runtime/net.o` appears in no shipping object
  list.
- Region size alone does not distinguish EEPROM subtypes — the **declared** size is
  authoritative.
- **No repository-level version control** for the project tree; provenance rests on
  `evidence/source-manifest.sha256` (see §9).

---

## 9. Package contents and provenance

```
release/luap0rt-gba-v0.0.1/
  LICENSE                     canonical GNU GPL v2 text, VERBATIM (project licence)
  COPYRIGHT                   LUAp0rt (C) 2026 NBT, GPL-2.0-or-later + third-party notice
  RELEASE.md                  this document
  CREDITS.md                  who made what
  THIRD_PARTY_NOTICES.md      per-component licence detail + compliance audit
  SHA256SUMS                  manifest of every preserved file
  binary/                     m16cgpsp.{bin,lua,elf,map}          (COPIES)
  evidence/
    reloc.log                                                     (COPY)
    host-gates.md  hardware-matrix.md  flash64-closure.md
    m13c-frozen.txt  source-manifest.sha256
    m13c-baseline/m13cgpsp.bin                                    (COPY)
  docs/
    controls.md  limitations.md  build-reproduction.md  deploy-ps5.md
  licenses/
    GPL-2.0-gpSP-COPYING.txt                                      (VERBATIM COPY)
    GPL-2.0-LuaPSX-COPYING.txt                                    (VERBATIM COPY)
    MIT-libretro-common.txt                                       (VERBATIM NOTICES)
  evidence/
    release-source-manifest.sha256   207 distributed source files (machine-verifiable)
  source-snapshot/            COMPLETE CORRESPONDING SOURCE — 207 files (COPIES)
    Makefile                                                      (COPY)
    LUAport_GBA_Portability_Report.md                             (COPY)
    gpsp/          145 files  emulator core + libretro-common headers + COPYING
    adapters/gba/   48 files  LUAp0rt GBA adapter layer
    tools/          24 files  build + verification scripts
    runtime/        19 files  LuaPSX-derived PS5 runtime (incl. libc/)
    apps/m16cgpsp/   2 files  main.c, m16c_splash_art.inc
    apps/m13bgpsp/   1 file   m13b_picker.inc
    lua/             1 file   m16c.lua.in
```

**The corresponding source is complete and verified.** Every one of the 207 files
was confirmed **byte-identical** to the frozen source that produced the
hardware-tested binary, by full recursive comparison — not by manifest alone.
Excluded from the snapshot, deliberately: `gpsp/.git/`, `gpsp/bios/` (the
open-source BIOS image) and `tools/__pycache__/`.

### 9.1 Licensing of the distributed source snapshot

**`source-snapshot/` is covered by the licensing documents at the root of this
package**, which a recipient reads alongside it:

| For | Read |
| --- | --- |
| NBT's **original** LUAp0rt code — `adapters/`, `apps/m16cgpsp/`, `tools/`, `lua/`, `Makefile` | `LICENSE` (canonical GPLv2) + `COPYRIGHT` — **GPL-2.0-or-later, © 2026 NBT** |
| **gpSP** — `source-snapshot/gpsp/` | `licenses/GPL-2.0-gpSP-COPYING.txt`; the tree also ships its own `gpsp/COPYING` and per-file headers |
| **LuaPSX-derived runtime** — `source-snapshot/runtime/` | `licenses/GPL-2.0-LuaPSX-COPYING.txt` |
| **libretro-common headers** — `source-snapshot/gpsp/libretro/libretro-common/` | `licenses/MIT-libretro-common.txt`; MIT notices are also stated per file |
| Per-component detail, provenance and derivation record | `THIRD_PARTY_NOTICES.md`, `CREDITS.md` |

> **No licence header was added to any file in `source-snapshot/`, by design.** The
> shipping source is size-frozen with **16 bytes** of margin; editing it would
> invalidate SHA-256 `16580c90…01efaf2` and its entire hardware-validation record.
> The snapshot is therefore **byte-identical to the source that built the shipping
> binary**, and licensing is carried at release-documentation level — which is what
> makes the corresponding-source claim verifiable in the first place.

**Copied, never regenerated:** the binary, loader, ELF, map, relocation log and the
M13C witness. These are the hardware-validated bytes; regenerating any of them
would risk a different SHA and invalidate the validation record.

**Source provenance.** `evidence/source-manifest.sha256` pins by SHA-256 every
source file that participates in the M16C shipping build, plus the `Makefile`, the
test harnesses and the portability report. `gpsp/` is additionally pinned by its own
vendored git commit **`8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f`** (branch
`master`).

**Excluded from this package**, deliberately: ROMs, BIOS images (notably
`gpsp/bios/open_gba_bios.bin`), user saves, temporary files, and `mgba/` — see
`docs/limitations.md` §6 for the dependency evidence that `mgba/` is not consumed
by M16C.

---

## 10. What the freeze means

1. **This freezes the GBA v0.0.1 baseline.** The artifact identity, gate results,
   resource measurements and hardware matrices recorded here define v0.0.1.
2. **Future work must not silently modify this baseline.** Any change reaching the
   shipping image invalidates the recorded SHA-256 and must be declared, not
   absorbed.
3. **Any future runtime or core change is post-v0.0.1 work and must be validated
   separately.** It does not inherit this release's evidence; it requires its own
   gate run, its own regeneration and its own hardware validation.
4. **GBA v1 (M13C) remains frozen** and byte-identical; v0.0.1 does not supersede
   it.
5. **Netplay / M18 remains post-freeze work.**
6. **The 240-minute watchdog is frozen v0.0.1 behaviour** and is not to be removed,
   raised or converted to a progress detector as part of v0.0.1.

---

## 11. Configuration invariants frozen with v0.0.1

| Invariant | Value |
| --- | --- |
| ROM picker capacity | **64 ROMs** |
| ROM buffer size | **`ROM_BUFFER_SIZE = 2`** |
| Gameplay watchdog | **864,000 iterations / 240 minutes** |
| Session-reuse define | **`LUAPORT_SESSION_REUSE`** (shipping config) |
| Exit chord / hold | **`PAD_L1|PAD_R1|PAD_L2|PAD_R2`, 15 frames** |
| JIT budget | **524,288 bytes** |
| Save/restore policy | as recorded in report §23 and `evidence/hardware-matrix.md` |

---

## 12. Licensing, attribution and distribution status

Full detail in `CREDITS.md` and `THIRD_PARTY_NOTICES.md`; verbatim licence texts
in `licenses/`.

**LUAp0rt GBA v0.0.1 is a combined work built on third-party software.** The
shipping binary links **41 objects**: 12 from **gpSP** (GPL-2.0-or-later, © 2006
Exophase and contributors), 6 from a **LuaPSX-derived PS5 runtime**
(GPL-2.0-or-later, soniciso1), and 23 of **LUAp0rt's own** adapter and launcher
code by **NBT**. **libretro-common** headers (MIT, © The RetroArch team) are
reached by compiled gpSP sources.

Consequently **the combined work is subject to GPL-2.0-or-later.**

### 12.1 LUAp0rt's project licence — DECLARED

```
LUAp0rt
Copyright (C) 2026 NBT

Original LUAp0rt code is licensed under the GNU General Public
License version 2 or, at your option, any later version.
```

**SPDX-License-Identifier: `GPL-2.0-or-later`**

- **`LICENSE`** — the canonical GNU GPL version 2 text, **verbatim**. Produced by
  byte-exact copy of the verified canonical text, not transcription; its SHA-256
  is `189b1af95d661151e054cea10c91b3d754e4de4d3fecfb074c1fb29476f7167b`.
- **`COPYRIGHT`** — the project copyright/licence notice, the scope of NBT's
  original work, and an explicit list of third-party work **not** authored by NBT.

**This declaration covers NBT's original LUAp0rt work only.** gpSP, the
LuaPSX-derived runtime, libretro-common and the EmuC0re-attributed font data
**retain their own copyrights, licences, notices and attribution** and are **not**
relicensed. **LUAp0rt was created by NBT; NBT did not author any upstream
project, and no upstream developer is the creator of LUAp0rt.**

> ### ✅ CLEARED FOR PUBLIC DISTRIBUTION
>
> **All release-blocking compliance items are closed.** Every item was resolved by
> evidence or by NBT's explicit decision — none by assumption, and **none required
> modifying the frozen implementation.**
>
> 1. **Corresponding source — RESOLVED IN PASS 2C.** The allow-list copy was
>    performed by the maintainer and audited. `source-snapshot/` carries the
>    **complete corresponding source for all 41 linked objects** plus the build
>    scripts and linker script — **207 files, every one byte-identical to the
>    frozen source**, verified by full recursive comparison, with a clean
>    machine-verifiable manifest (`evidence/release-source-manifest.sha256`).
>    **GPL-2.0 §3 is satisfied.** → `THIRD_PARTY_NOTICES.md` §5.8.
> 2. **Project licence — RESOLVED IN PASS 3.** NBT explicitly authorised
>    **GPL-2.0-or-later** for the original LUAp0rt work. `LICENSE` and `COPYRIGHT`
>    are present; GPL-2.0 §1 (licence must accompany the distribution) is
>    satisfied. → §12.1, `THIRD_PARTY_NOTICES.md` §4.
> 3. **EmuC0re attribution — OPEN, but NOT release-blocking.** Pass 2B proved by
>    hash that the font table in `runtime/tables.h` is a **byte-identical copy of
>    `LuaPSX/src/tables.h`** (both `ca8224fe…`). It therefore reaches LUAp0rt under
>    **LuaPSX's GPL-2.0-or-later**, whose text is present and verbatim — so there
>    is no licence gap in the chain actually relied on. EmuC0re's *own* notice
>    remains unread: a **Pass 3 retrieval attempt was made and denied** (web
>    tooling lacks permission). **Nothing was fabricated to close it**, and the
>    font was not replaced. → `THIRD_PARTY_NOTICES.md` §6.3.
>
> **RESOLVED in Pass 2B — Luac0re.** No Luac0re code is incorporated; it is a
> runtime host that LUAp0rt calls and works around. No licence text is required,
> and acknowledgement is preserved. → `THIRD_PARTY_NOTICES.md` §3.F, §6.4.
>
> **The freeze itself is unaffected.** The binary, its hardware validation and
> every measurement in this document stand. Licensing was added as
> release-level documentation only — **no shipping source or binary was touched,
> and no rebuild occurred.**

**No incompatible licensing was found** among the licences that could be
determined. **mGBA is not part of this release** — it was evaluated in report §14
and not selected, and its absence from the build is proven by the include paths
and the link line, not assumed.

---

## 13. ROM, BIOS and user-content policy

**This release contains no game content and no console firmware.**

- **No commercial GBA ROMs** — original, patched or test copies.
- **No Nintendo GBA BIOS.** The binary embeds no BIOS at all: `gba_bios.c` loads
  one from a user-supplied file at run time, and the symbol `open_gba_bios_rom` is
  a **forbidden symbol** enforced against the linked image by
  `tools/check_forbidden.py` during `make m16c-verify`. The exclusion is
  gate-enforced, not merely observed.
- **No open-source BIOS replacement** (`gpsp/bios/open_gba_bios.bin` is excluded).
- **No user save files**, no `.sav`, no `.hdr`, no save containers.
- **No commercial game artwork, screenshots or extracted assets.**
- **No credentials, keys, or proprietary SDK files.** No Sony SDK header or library
  is used or required: console entry points are resolved at run time by name.

**Users must supply their own game and firmware files.** LUAp0rt does not
distribute them, does not link to them, and does not direct users to any source
for them.

> **The `.bin` files in this package are not ROMs.** `binary/m16cgpsp.bin` is the
> LUAp0rt shipping payload and `evidence/m13c-baseline/m13cgpsp.bin` is the frozen
> v1 payload. Both are built from the audited source.
