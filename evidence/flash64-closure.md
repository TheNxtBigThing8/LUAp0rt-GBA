# FLASH64 save-restore defect — closure record

This is the defect that v0.0.1 exists to fix.

---

## 1. Symptom

A valid, ROM-bound LGS1 FLASH64 save was rejected on a **fresh launch** with:

```
SAVE CORRUPT / CLASS INCOMPATIBLE
```

The same save would load correctly if the cartridge had already been booted once in
the same payload session. The defect therefore only presented on the first launch
after the payload started — the case that matters most to a user.

## 2. Root cause

`adapters/gba/gba_savehdr.c:249-252` — the class-compatibility gate.

At preboot the live backup class is `BACKUP_UNKN`, because nothing has yet probed
the cartridge's save hardware. The gate compared the save file's recorded class
against the live class and rejected any mismatch. It had **no evidence path for a
legitimately-UNKNOWN live class**, so a correct FLASH64 save compared against
`BACKUP_UNKN` and was graded incompatible.

The bug was not in the FLASH64 code. It was in the gate's inability to express
"the live class is not yet known, and that is not the same as wrong."

## 3. Fix architecture — Option 2, keyed evidence with late activation

Five production files were edited. **Every addition sits behind
`#ifdef LUAPORT_SESSION_REUSE`.**

| File | Change | Location |
| --- | --- | --- |
| `gba_savehdr.h` | `EV_F64_ACTIVATE = (1u<<1)` — unconditional macro, **0 bytes** of image cost; three stale comments corrected | — |
| `gba_savehdr.c` | Guarded FLASH64 evidence cell at the live-UNKNOWN branch | `:281-296` |
| `gba_restore.c` | Guarded `gba_restore_flash64(int activate)`; writes **only** `backup_type = BACKUP_FLASH` | `:171-239` |
| `gba_restore.h` | Declaration placed inside the existing M16-only block | `:249` |
| `gba_restorefile.c` | Observation → `compatible_ev` evidence; activation only after PASS 2 re-verify; `rf_dbtrust` narrowed with `&& rf_dbflash128` | `:509-541`, `:590-592`, `:597-614`, `:636-667` |

The shape of the fix is the important part:

- **Observe early, activate late.** The FLASH64 observation is recorded as
  *evidence* during the first pass. It does not mutate the backup type.
- **Activation happens only after the PASS 2 re-verify succeeds.** A save that
  fails re-verification never activates anything, so a corrupt file cannot steer
  the live backup class.
- **`gba_restore_flash64` writes exactly one field**, `backup_type = BACKUP_FLASH`.
  It deliberately does not touch save size, region, or any other state, so its blast
  radius is one enumeration value.
- **`rf_dbtrust` was narrowed** with `&& rf_dbflash128`, so database trust cannot be
  claimed for a FLASH64 cartridge on the strength of a FLASH128 database entry.

## 4. Evidence

### 4.1 Test-first, RED → GREEN, tests unmodified

Phase 1 added harness sections `[10f]` (FLASH64) and `[10g]` (EEPROM8K / `AZLE`
control) to `tools/gba_restore_equiv.c`, plus a `Makefile` target
`m16c-save-equiv` that builds a **separate binary** with `-DLUAPORT_SESSION_REUSE`
and is wired into `m16c-verify`.

| Stage | Result |
| --- | --- |
| Phase 1 — tests written, fix not applied | **1013 checks, 36 failures (RED)** |
| Phase 2 — production fix applied | **1013 checks, 0 failures (GREEN)** |

The check count is **identical**. The tests were not touched between the two runs,
so the transition is attributable to the production change and nothing else.

The `[10g]` EEPROM8K section is a **control**: it exists to show that the fix did
not simply make the gate permissive. EEPROM8K saves must still be graded on their
own terms, and they are.

### 4.2 Non-regression

| Check | Result |
| --- | --- |
| M12C equivalence, before and after | **958 / 0 — unchanged** |
| M13C shipping binary, rebuilt after the fix | **byte-identical** (`e9925161…d824a`) |

The M13C result is the strongest non-regression evidence available: M13C does not
compile with `LUAPORT_SESSION_REUSE`, so if any edit had escaped its guard, M13C's
SHA would have moved.

### 4.3 Hardware closure

| Scenario | Cartridge | Result |
| --- | --- | --- |
| Restore existing FLASH64 save, fresh launch | Mother 3 | **PASS** |
| Full lifecycle load → play → save → picker | Mother 3 | **PASS** |
| Create save where none existed | The Sims 2 | **PASS** |
| Relaunch and load the new save | The Sims 2 | **PASS** |

## 5. Scope audit — why one macro, not two

A read-only audit asked whether `LUAPORT_SESSION_REUSE` was the right guard, or
whether the FLASH64 work needed its own macro.

Findings:

- M16-0 and M16-1 compile **only** `gba_restore.c` with the define.
- `M16_FORBIDDEN_OBJS` (`Makefile:14062`) and `M16B_FORBIDDEN_OBJS`
  (`Makefile:14681`) **exclude `gba_restorefile.o`**.
- `gba_restorefile.c` holds the **only two callers** of the new path
  (`:532`, `:666`). With that object excluded, the path never links in M16-0/M16-1.

**Result:** approximately **48 bytes of unreachable dead code** in two *unpinned
diagnostic* milestones. Neither has a frozen SHA — **only M13C does** — so nothing
measurable is disturbed.

**Recommendation A was accepted: keep `LUAPORT_SESSION_REUSE` as-is.** Introducing
a second macro would decouple the test harness from the shipped configuration and
weaken the guard counter at `Makefile:16561`, which is the mechanism that keeps
every guarded file accounted for. Trading a real verification guarantee for 48
bytes in a non-shipping diagnostic is a bad exchange.

## 6. Plan-versus-implementation note

Phase 1's harness never referenced `EV_F64_ACTIVATE`, although the written plan
implied it would. The implementation was therefore written against **the harness's
actual contract**, not against the plan's description of it, and the tests were left
unmodified. This is recorded because the discrepancy is real and a future reader
comparing the plan to the code will otherwise find it and wonder which is
authoritative. **The harness is authoritative.**
