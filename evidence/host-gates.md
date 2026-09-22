# Host verification gates — LUAp0rt GBA v0.0.1

All gates were run by the operator under **WSL** with `HOSTCC=gcc`
(`/usr/bin/make` 4.4.1, `/usr/bin/gcc` 15.2.0). Windows-side `make`/`gcc`/`clang`
are not present on the development host.

## 1. Results

| Gate | Command | Result |
| --- | --- | --- |
| M12C equivalence | `make m12c-equiv HOSTCC=gcc` | **958 checks, 0 failures** |
| M16C save / session reuse | `make m16c-save-equiv HOSTCC=gcc` | **1013 checks, 0 failures** |
| R7 input equivalence | `make r7-input-equiv HOSTCC=gcc` | **688 checks, 0 failures** |
| M13B equivalence | `make m13b-equiv HOSTCC=gcc` | **283 checks, 0 failures** |
| Full verify chain | `make m16c-verify HOSTCC=gcc` | **`M16C VERIFY OK`** |

## 2. Composition of `m16c-verify`

`Makefile:15830`:

```
m16c-verify: m16c-hostcc m13b-equiv m12c-equiv m16c-save-equiv r7-input-equiv \
             $(M16CELF)
```

It then runs, in order:

1. **image position independence** — `tools/check_image.sh` against the ELF.
2. **relocations, non-masking pipeline** — `tools/verify_reloc.py` over the ELF and
   every object in `$(M16COBJS)`, writing `build/m16c/reloc.log`. The pipeline is
   deliberately non-masking: the exit status of the Python step is captured in `st`
   and re-raised, so a failure cannot be hidden by the `tail` that prints the log.

`m16c-hostcc` exists specifically so the save gate **can never silently degrade to
a skip** (`Makefile:15808-15811`). If no host compiler is available the verify
chain fails rather than reporting success with the save equivalence unrun.

## 3. Relocation evidence

Preserved verbatim at `evidence/reloc.log`:

```
image layout
  __rela_start  0x0004E290
  __rela_end    0x0004F820  (5520 bytes, 230 entries)
  __data_start  0x00060000   <- writable region begins here
  __bss_end     0x0024D9C0

checks
  entries parsed            : 230
  all R_X86_64_RELATIVE     : yes
  all targets writable      : yes (>= __data_start)
  all targets in range      : yes (< __bss_end)
  all addends inside image  : yes

target distribution
  .data                    230

VERDICT: safe to apply
```

An image with **no** relocations would mean `.rela.dyn` had been dropped and every
function-pointer table would read back as a link-time offset. The check is
therefore inverted — **zero is the failure**.

## 4. Ordering requirement

The equivalence gates **must be run sequentially — never with `-j`**. Several
harnesses share fixed scratch filenames (`m12c_equiv_test.sav`, `.hdr`) in the
repository root, so parallel invocation races on the same files and produces
spurious results.

## 5. Guard-count gate

The M16C guard-count loop in the `Makefile` was widened from 4 files to 6 as part
of the FLASH64 work, so that every production file carrying
`#ifdef LUAPORT_SESSION_REUSE` additions is counted. This is what keeps the
shipping configuration and the harness configuration from drifting apart — see
`evidence/flash64-closure.md` §5.

## 6. Test-first record

The save gate is the direct evidence that the FLASH64 defect is fixed:

| Stage | Result |
| --- | --- |
| Phase 1, tests written, fix not yet applied | **1013 checks, 36 failures (RED)** |
| Phase 2, production fix applied | **1013 checks, 0 failures (GREEN)** |

The check count is identical across both runs: **the tests were not modified
between RED and GREEN.**
