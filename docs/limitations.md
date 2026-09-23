# Known limitations and deferred items — LUAp0rt GBA v0.0.1

Recorded so that a future reader knows what this release does *not* claim.

---

## 1. Size — 16 bytes of margin

| Item | Value |
| --- | ---: |
| `__data_load` (`0x4F820`) | 325,664 |
| Ceiling (`0x50000`) | 327,680 |
| **Margin** | **2,016** |
| Hard floor | 2,000 |
| **Spare** | **16 bytes** |

If `__data_load` crosses `0x50000`, `__data_start` steps from 393,216 to 458,752
and JIT headroom halves from 131,072 to 65,536 (`Makefile:3475-3477`).

**Consequence: there is no shipping-source edit that is safe by inspection.** A
comment is free; a string literal is not; an added diagnostic certainly is not.
Any change to the shipping source is a size-gate event and must be measured, not
assumed.

Note also (`Makefile:2061`): **do not optimise merely to preserve `0x50000`.**
Stepping to 458,752 is a legitimate outcome with 65,536 bytes still in hand — it is
a decision to be taken deliberately, not avoided reflexively.

## 2. The 240-minute watchdog is shipped behaviour

- `M13C_PLAY_MAX_ITERS = 864000` iterations, `M13C_WD_MINUTES = 240`,
  compile-time pinned by `_Static_assert`.
- One iteration is one emulated frame. There is **no wall-clock session
  terminator** — the bound is on monotonic iterations.
- Reaching the cap is **graded a watchdog failure** and **does not commit that
  session's save**.
- **Progress-based hang detection remains deferred** and was deliberately not
  implemented. A session that is running but making no progress is not currently
  distinguishable from one that is simply long.

This is inherited unchanged from frozen v1 (report §25.5) and is not to be removed,
raised or converted to a progress detector as part of v0.0.1.

## 3. Netplay / M18 is not shipped

- The M18 block exists in the `Makefile` between `@@M18_BLOCK_BEGIN@@` and
  `@@M18_BLOCK_END@@` sentinels and is **purely additive**.
- `runtime/net.o` appears in **no shipping object list**; `M16C_OWN_OBJS`
  (`Makefile:15330-15357`) does not reference it.
- `m18-verify` carries a positive control asserting that `M16C_OWN_OBJS` is *not*
  inside the M18 block — i.e. the build system actively checks that M18 has not
  absorbed the shipping object list.

## 4. EEPROM subtype is not inferable from region size

`gba_savehdr_region_of` returns **8192 for both EEPROM subtypes**. Region size alone
therefore cannot discriminate EEPROM512 from EEPROM8K — **the declared save size is
authoritative**. See `evidence/hardware-matrix.md` §1.1.

## 5. M16-0 and M16-1 are diagnostics, not shipping artifacts

- Defined at `Makefile:15190-15203`.
- They **cannot write saves**.
- Neither has a frozen SHA; only M13C does.
- They carry roughly **48 bytes of unreachable dead code** from the FLASH64 fix,
  because `gba_restorefile.o` — which holds the only callers — is excluded from
  their object sets by `M16_FORBIDDEN_OBJS` / `M16B_FORBIDDEN_OBJS`. This is
  deliberate and was accepted in the scope audit; see
  `evidence/flash64-closure.md` §5.

**M16C is the sole shipping artifact.**

## 6. `mgba/` is not part of the product

`mgba/` (≈142 MB) is **excluded** from this release package. Dependency evidence,
gathered read-only:

| Check | Result |
| --- | --- |
| `-Imgba` in any M16C flag chain | **absent** — `M16C_INCLUDES` (`Makefile:15244-15245`) resolves to `adapters/gba/include`, `runtime/libc`, `runtime`, `apps/m16cgpsp`, `apps/m13bgpsp`, `adapters/gba`, and via `GPSP_INCLUDES` also `gpsp`, `gpsp/libretro/libretro-common/include` |
| `mgba` in `M16C_OWN_OBJS` / `M16C_GPSP_OBJS` | **absent** |
| `mgba` occurrences in the `Makefile` | **comments only**, all referring to M4/M8 test ROMs |
| `mgba` occurrences in shipping headers | **comments only** (`gba_input.h:38-39`, `gba_m8map.h:16-17`), documenting bit-order agreement |
| `mgba` in `apps/m16cgpsp/main.c` | **zero occurrences** |

**Conclusion: M16C consumes nothing from `mgba/`.** It is a reference/test-ROM
tree used by earlier milestones.

## 7. Content deliberately excluded from the package

No ROMs, BIOS images, user saves, or temporary files are included. Specifically
excluded:

- `gpsp/bios/open_gba_bios.bin` — a BIOS binary present in the vendored gpSP tree.
- `mgba/` — includes test ROMs (`mgba/cinema/gba/**/*.gba`).
- `gpsp/.git/` — vendored upstream history; the pinned commit is recorded instead.

## 8. Version control

The project tree is **not under repository-level version control**; no `.git`
exists at the project root. Provenance for this release therefore rests on:

- `source-snapshot/` — a **complete byte-for-byte copy** of the source tree that
  produced the shipping binary.
- `evidence/release-source-manifest.sha256` — SHA-256 of every file in
  `source-snapshot/`, machine-verifiable with `sha256sum -c`.
- `evidence/source-manifest.sha256` — the historical Pass 1 manifest covering every
  source file participating in the M16C shipping build, plus the `Makefile`,
  harnesses and report.
- The vendored `gpsp/` tree, which **is** git-managed and is pinned at commit
  `8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f` (branch `master`).

**Resolved (Pass 2C): the source snapshot is complete.** An earlier revision of this
document recorded a gap — that only the `Makefile` and the portability report had
been copied, because the environment's copy tooling would not perform recursive
directory copies. **That is no longer true.** The full trees were copied and audited:

| Check | Result |
| --- | --- |
| Files in `source-snapshot/` | **207** |
| Verified against the frozen originals | **207** |
| **Failures** | **0** |
| Method | Full recursive `diff` against the frozen source, plus SHA-256 manifest |

Distributed trees: `gpsp/` (145 files), `adapters/gba/` (48), `tools/` (24),
`runtime/` (19), `apps/m16cgpsp/` (2), `apps/m13bgpsp/` (1), `lua/` (1), plus the
`Makefile` and the portability report. The only deliberate exclusions are `gpsp/.git/`,
`gpsp/bios/open_gba_bios.bin` and `mgba/` (§6, §7).

The snapshot is therefore **corresponding source in the GPL sense** — sufficient to
rebuild, not merely to detect drift.

### 8.1 Annotated-manifest parse artifacts (cosmetic)

`evidence/source-manifest.sha256` — the **historical** Pass 1 manifest — contains 11
human-readable annotation lines that `sha256sum -c` cannot parse. They report as
format errors, **not hash mismatches**, and are a known cosmetic defect of that file.

Use **`evidence/release-source-manifest.sha256`** for machine verification; it is
clean and verifies 207/207 with zero failures. The historical file is retained
unmodified as a provenance record.

## 8A. `source-snapshot/tools/upload.py` is not self-contained (use the launcher's copy)

`source-snapshot/tools/upload.py` is included because it is part of the source tree,
but **that copy cannot be used as-is.** At `tools/upload.py:212` it resolves its
console-side receiver to `<snapshot root>/lua/upload.lua`, and `source-snapshot/lua/`
contains only `m16c.lua.in`, the loader template for the shipping payload. The
receiver was never part of the M16C shipping build graph, so it is correctly outside
the corresponding-source scope that the snapshot was assembled from. The snapshot is
unchanged (207 files) and this remains a documentation observation, not a
compliance defect.

**Since v0.1.0 the working pair ships next to the launcher instead:**

```
launcher/tools/upload.py   (byte-identical to the snapshot copy)
launcher/lua/upload.lua    (the console-side receiver, LUAp0rt's own file)
```

The desktop launcher uses that pair to send the BIOS and the checked games when you
press **Launch**, and the same pair can be run by hand from the `launcher/` folder:

```sh
python launcher/tools/upload.py <PS5_IP> <local file> /temp0/<name>
```

The receiver is only ever sent to the loader when it is listening; while the emulator
is running the loader is not, and the launcher refuses to send until it is armed again.

## 9. Other configuration limits

| Item | Value |
| --- | --- |
| ROM picker capacity | **64 ROMs** |
| `ROM_BUFFER_SIZE` | **2** (two 1 MB buffers — what makes >2 MB cartridges demand-paged) |
| `gba_m11save.c` | Deliberately **unlinked**; absent from `m16c-check` and from `M16C_OWN_OBJS` |
| Optional cleanups **O1–O7** | Outside the baseline; documentation and non-shipping only |

## 10. Destructive-operation warning

`make m16c-clean` runs `rm -rf $(M16CBUILD)` (`Makefile:16662-16663`), destroying
the active `build/m16c/` artifacts — the shipping binary, loader, ELF, map,
relocation log and both host-gate binaries.

The correct release-engineering statement is: **`m16c-clean` destroys the active
`build/m16c` artifacts, therefore the validated artifacts must be copied into the
release package before any destructive operation.** That copy has been made — see
`binary/` and `SHA256SUMS`. Full-project backup copies may also exist elsewhere;
this package does not assume it holds the only copy.

No destructive operation was performed during release packaging.
