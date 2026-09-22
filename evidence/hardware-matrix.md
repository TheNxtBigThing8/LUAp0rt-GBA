# Hardware validation matrices — LUAp0rt GBA v0.0.1

Validated on PS5 hardware against `m16cgpsp.bin`
SHA-256 `16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2`.

These matrices use the **five-term status vocabulary established in report §23.1**,
unchanged. Everything recorded below is **HARDWARE PASS** — exercised on the
console against the shipping artifact, not inferred from source.

---

## 1. Save-class matrix

**This section supersedes report §23.2** (`LUAport_GBA_Portability_Report.md:1173-1174`),
which recorded FLASH64 and EEPROM8K as *SOURCE-CHARACTERIZED, NOT
HARDWARE-EXERCISED*. Both are now hardware-exercised. §23.1's vocabulary and the
rest of §23 are unaffected.

| Logical save class | Declared save size | v0.0.1 status | Cartridge evidence |
| --- | ---: | --- | --- |
| **SRAM** | 32,768 | **HARDWARE PASS** | Metroid Fusion; Kingdom Hearts: Chain of Memories |
| **EEPROM512** | **512** | **HARDWARE PASS** | Tony Hawk's Pro Skater 2 |
| **EEPROM8K** | **8,192** | **HARDWARE PASS** *(was source-only in v1)* | Zelda: A Link to the Past + Four Swords (`AZLE`) |
| **FLASH64** | 65,536 | **HARDWARE PASS** *(was "not present in tested library" in v1)* | **Mother 3**; **The Sims 2** |
| **FLASH128** | 131,072 | **HARDWARE PASS** | Super Mario Advance 4 (`AX4E`) |

### 1.1 EEPROM subtypes — declared size is authoritative

The logical classes above are defined by their **declared save size**:
EEPROM512 = 512-byte declared save, EEPROM8K = 8,192-byte declared save.

Separately, at the implementation level, `gba_savehdr_region_of` returns **8192 for
both EEPROM subtypes**. That is a statement about *region allocation*, not about
save class.

**Consequence:** region size alone can never discriminate EEPROM512 from EEPROM8K.
Any code or diagnostic that tries to infer the subtype from the allocated region
will be wrong for one of the two. **The declared size is the authoritative
discriminator.** This caveat is carried forward unchanged from report §23.3.

---

## 2. FLASH64 lifecycle detail

FLASH64 was the defect class fixed in v0.0.1, so it was exercised most heavily.

| Scenario | Cartridge | Result |
| --- | --- | --- |
| Restore an existing ROM-bound LGS1 FLASH64 save on a fresh launch | Mother 3 | **PASS** — previously failed `SAVE CORRUPT / CLASS INCOMPATIBLE` |
| Full session lifecycle: load → play → save → return to picker | Mother 3 | **PASS** |
| Create a save where none existed | The Sims 2 | **PASS** |
| Relaunch and load the newly created save | The Sims 2 | **PASS** |

---

## 3. Controls, frontend and session matrix

| Area | Result | Notes |
| --- | --- | --- |
| R7 controls | **HARDWARE PASS** | Face-button aliasing; see `docs/controls.md` |
| Return-to-picker chord | **HARDWARE PASS** | Four shoulders, 15-frame hold |
| ROM picker | **HARDWARE PASS** | Navigation and paging |
| Details / frontend | **HARDWARE PASS** | |
| Gameplay | **HARDWARE PASS** | |
| Audio | **HARDWARE PASS** | |
| Presentation / scaling | **HARDWARE PASS** | |
| Demand paging | **HARDWARE PASS** | Cartridges over 2 MB with `ROM_BUFFER_SIZE = 2` |
| Session lifecycle | **HARDWARE PASS** | Repeated launch → play → exit → relaunch |

---

## 4. Session-boundary guarantees confirmed on hardware

The session boundary is what makes repeated play possible without relaunching the
payload. All three properties were observed to hold:

1. **Arena returns to its watermark** between sessions.
2. **The RFILE pool returns to baseline** — the demand-paging file handle is the
   one pool slot per session that must be released before the picker returns.
3. **The payload never restarts across sessions.** Frame identifiers in the
   operator log climb **monotonically** across session boundaries; a restart would
   reset them to zero.

---

## 5. What was NOT hardware-exercised

Recorded for honesty, and because a release record that only lists passes is not
evidence:

| Item | Status |
| --- | --- |
| Netplay / M18 | **Not shipped**, therefore not exercised |
| 240-minute watchdog expiry | **Not exercised to expiry** — reaching the cap is graded a failure by design and does not commit the session's save |
| M16-0 / M16-1 diagnostics | **Not shipping artifacts**; excluded from the release |
| Save classes beyond the five above | None exist for GBA |
