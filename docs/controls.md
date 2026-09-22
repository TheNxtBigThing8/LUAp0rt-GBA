# Controls — LUAp0rt GBA v0.0.1

The input path has **two layers**, and keeping them distinct matters: one is frozen
and defines meaning, the other routes onto it without redefining anything.

---

## 1. Layer 1 — the frozen M8 mapping table

`adapters/gba/gba_input.c` is the **sole authority** on what a pad bit means as a
GBA key. It was frozen at milestone M8 and v0.0.1 does not alter it.

Bit ordering is deliberately identical to mGBA's `GBAKey` enum (A=0 … L=9):

```
START 0x08   SELECT 0x04   B 0x02   A 0x01
```

Two invariants are asserted by `m8_input_selftest()`:

| Invariant | Meaning |
| --- | --- |
| `M8_ST_UNIQUE` | The mapping table is **injective** — no two pad bits map to the same GBA key |
| `M8_ST_UNMAPPED` | `SQUARE`, `TRIANGLE`, `L2`, `R2`, `L3`, `R3` map to **nothing** at this layer |

## 2. Layer 2 — R7 canonicalisation

`adapters/gba/gba_r7input.c` makes the controller comfortable to hold without
touching layer 1. It **routes** inputs onto the frozen table rather than redefining
the table.

```c
#define R7_CLASS_A  (M8_PAD_CROSS  | M8_PAD_CIRCLE)     /* -> GBA A */
#define R7_CLASS_B  (M8_PAD_SQUARE | M8_PAD_TRIANGLE)   /* -> GBA B */
```

`r7_input_from_pad()` collapses each class to the single pad bit the frozen table
already understands, then calls `m8_input_from_pad(p, connected)`.

### Effective player-facing mapping

| Physical input | GBA key |
| --- | --- |
| **Cross** or **Circle** | **A** |
| **Square** or **Triangle** | **B** |
| D-pad | D-pad |
| L1 | L |
| R1 | R |
| OPTIONS | START |
| TOUCHPAD | SELECT |
| L2, R2, L3, R3 | *(no GBA key — see below)* |

### Why L2/R2/L3/R3 stay unmapped

They are not an oversight. `M8_ST_UNMAPPED` asserts their silence, and L2/R2 are
needed by the return-to-picker chord (§3). Mapping them to GBA keys would make the
chord ambiguous with gameplay input.

### Anti-phantom guard

```c
if (!connected)
    return 0;
```

A disconnected pad reports **no** buttons rather than whatever was last latched.
This prevents a dropped controller from holding a direction or a chord.

## 3. Return to the ROM picker

| Property | Value | Source |
| --- | --- | --- |
| Chord | `PAD_CHORD = PAD_L1 \| PAD_R1 \| PAD_L2 \| PAD_R2` — **all four shoulders** | `runtime/platform.h:183` |
| Hold | **15 frames** (`M16C_EXIT_HOLD`) | `apps/m16cgpsp/main.c:570` |
| Effect | Ends the session with `end_reason = OPERATOR` | `apps/m16cgpsp/main.c` |

The 15-frame hold is inherited unchanged from M13C. What changed in the M16 line is
not the chord or the hold but **the meaning of the resulting break**: in M13C it
ended the payload, in M16C it returns to the picker.

### 3.1 Returning to the picker is NOT save permission

`apps/m16cgpsp/main.c:85-87`. Ending a session with the chord does not by itself
authorise committing that session's save. This is a **safety property**, not an
implementation detail: it is what stops an operator exit from being treated as a
clean, save-worthy end of play.

### 3.2 Release-gate debounce

```c
#define M16C_RELEASE_MASK  (PAD_CHORD | PAD_CROSS | PAD_CIRCLE)
```
`apps/m16cgpsp/main.c:597`, enforced at `:2658`.

Input is not consumed until every bit in `M16C_RELEASE_MASK` has been observed
released. Without it, the chord still being physically held on the frame the picker
re-appears would immediately page the list, or `CROSS`/`CIRCLE` left over from
relaunch would trigger a selection.

`PAD_CHORD` is **deliberately in** this mask — the picker is re-entered directly out
of a chord hold, so the chord itself is exactly what must be released first
(`apps/m16cgpsp/main.c:608-609`).

---

## 4. Verification

`adapters/gba/gba_r7input.c` is proved on the host by `tools/r7_input_equiv.c`:

| Property | Value |
| --- | --- |
| Gate | `make r7-input-equiv HOSTCC=gcc` |
| Result | **688 checks, 0 failures** |
| Coverage | **All 2,097,152 reportable pad words**, swept exhaustively |

Three things make this a real test rather than a restatement:

1. It compares the shipping function against a **specification written by hand from
   the R7 requirement**, not against the same source — comparing code to itself
   would be circular.
2. It **re-proves the frozen invariant first.** Section 1 asserts
   `m8_input_selftest() == 0`, so if R7 work had quietly edited the M8 table, this
   gate fails first and loudest.
3. Section 8 encodes the **permitted equivalence classes as data** and **rejects
   every other duplicate relationship** — which is precisely what `M8_ST_UNIQUE` can
   no longer express once aliasing is allowed at all.

**Not one byte of this test ships.** The harness `#include`s the production `.c`
files verbatim so the code under test is the code that ships, but
`r7_input_equiv` appears in no object list and no payload rule builds it. The image
has no self-test table linked into it — the size margin (16 bytes) would not permit
one.
