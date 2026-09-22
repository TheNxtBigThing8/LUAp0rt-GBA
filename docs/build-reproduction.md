# Build and reproduction — LUAp0rt GBA v0.0.1

> **Reproduction is a post-release exercise, not a release-preparation step.**
> The artifacts in `binary/` are the hardware-validated bytes. Nothing in this
> document was executed during release packaging.

---

## 1. Toolchain of record

| Item | Value |
| --- | --- |
| Environment | **WSL** on the Windows development host |
| `make` | `/usr/bin/make` **4.4.1** |
| `gcc` | `/usr/bin/gcc` **15.2.0** |
| Host-gate compiler | passed explicitly as `HOSTCC=gcc` |

**Windows-side `make`, `gcc` and `clang` are not present.** All builds and gates are
run inside WSL. This is why every command below is given in its WSL form.

## 2. Build commands

```sh
make m16c-verify   HOSTCC=gcc    # full gate chain (see §3)
make m16c          HOSTCC=gcc    # link the shipping binary
make m16c-size                   # resource measurements
make m16c-lua                    # generate the loader (.lua) from .bin + .elf
```

`m16c-lua` depends on `m16c-check`, the `.bin` and the `.elf`
(`Makefile:16630-16633`) and runs `tools/mklua.py`. The loader is **not** optional
packaging: `mklua.py` reads `__data_start` and `__bss_end` out of the ELF symbol
table to compute the JIT reservation, and substitutes `@@JIT_SIZE@@` into the
template. The blob is **not** embedded in the script — the remote loader caps
scripts at 500 KB and hex encoding doubles the payload, so the binary travels
separately over TCP.

### 2.1 Toolchain-free syntax check

`make m16c-check` runs `tools/m8_syntax_check.py` over the twelve shipping sources
and needs **no toolchain at all** (`Makefile:15372-15384`). Useful on a workstation
that cannot build.

## 3. Host gates

Run these **sequentially — never with `-j`.** Several harnesses share fixed scratch
filenames (`m12c_equiv_test.sav`, `.hdr`) in the repository root and will race.

| Gate | Command | Expected |
| --- | --- | --- |
| M12C equivalence | `make m12c-equiv HOSTCC=gcc` | **958 checks, 0 failures** |
| M16C save / session reuse | `make m16c-save-equiv HOSTCC=gcc` | **1013 checks, 0 failures** |
| R7 input | `make r7-input-equiv HOSTCC=gcc` | **688 checks, 0 failures** |
| M13B equivalence | `make m13b-equiv HOSTCC=gcc` | **283 checks, 0 failures** |
| Full chain | `make m16c-verify HOSTCC=gcc` | **`M16C VERIFY OK`** |

`m16c-verify`'s dependency set (`Makefile:15830`):

```
m16c-verify: m16c-hostcc m13b-equiv m12c-equiv m16c-save-equiv r7-input-equiv \
             $(M16CELF)
```

`m16c-hostcc` exists so the save gate **can never silently degrade to a skip**
(`Makefile:15808-15811`). If no host compiler is found, the chain fails rather than
reporting success with the save equivalence unrun.

## 4. Expected measurements

A faithful rebuild should reproduce:

| Item | Value |
| --- | ---: |
| `.text` output section (`0x4E290`) | 320,144 |
| `.rela.dyn` | 5,520 (230 entries) |
| `__data_load` (`0x4F820`) | 325,664 |
| `.data` | 10,368 |
| `.bss` | 2,011,456 |
| `__data_start` (`0x60000`) | 393,216 |
| JIT footprint / budget / headroom | 393,216 / 524,288 / 131,072 |
| Binary size | 336,032 |
| SHA-256 | `16580c90…01efaf2` |

Every one of these is re-derivable **without building**, by reading
`binary/m16cgpsp.map` and `evidence/reloc.log` in this package.

## 5. Verifying the shipped artifacts

```sh
sha256sum -c SHA256SUMS          # from the release root
```

or on Windows:

```powershell
Get-FileHash -Algorithm SHA256 binary\m16cgpsp.bin
```

Expected: `16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2`.

## 6. Source identity

`evidence/source-manifest.sha256` pins every source file that participates in the
M16C shipping build. To check for drift, re-hash the working tree and compare.

`gpsp/` is additionally pinned by its own vendored git commit:

```
8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f   (branch master)
```

## 7. Destructive target — read before running

```make
m16c-clean:
	rm -rf $(M16CBUILD)
```
`Makefile:16662-16663`.

This **destroys the active `build/m16c` artifacts**: the shipping binary, loader,
ELF, map, relocation log and both host-gate binaries. The validated artifacts have
already been copied into this release package, which is why that copy is step one
of packaging and precedes any other operation.

`m16c-clean` was **not** run during release preparation.

## 8. Reproducibility caveat

v1 (M13C) demonstrated **measured** reproducibility: a forced regeneration
reproduced its established SHA-256 byte for byte (report §25.1). **No equivalent
forced-regeneration audit has been performed for v0.0.1.** Byte-identical
reproduction of `16580c90…01efaf2` from source is therefore *expected* but **not
yet measured**, and should not be stated as a property of this release until such
an audit is run.
