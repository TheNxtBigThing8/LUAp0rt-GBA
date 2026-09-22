# LUAport-GBA Portability Report

Status: HISTORICAL RECONNAISSANCE (Sections 1-22) + CURRENT VERIFIED STATE (Sections 23-25).
**GBA v1: FROZEN** — baseline M13C, declared 2026-09-17 after a passed final
forced-regeneration/verification audit. See Section 25.

> **READER NOTICE — DOCUMENT AUTHORITY.**
> **Sections 1-22 are the original pre-implementation reconnaissance, preserved unedited
> as a historical record.** They describe the project's position *before* any milestone ran.
> Their verdicts ("GO TO M0/M1 PLANNING"), their UNKNOWN evidence markers, and their
> "Not started" / "Not measured" entries are **historical, not current**, and several are
> now superseded by measurement.
>
> **For current project state, Sections 23, 24 and 25 are authoritative:**
> - **Section 23** — M15 compatibility evidence (authoritative for compatibility).
> - **Section 24** — verified baseline: gates, image measurements, SHA-256, configuration
>   invariants, and gameplay session bounding (authoritative for build state).
> - **Section 25** — the formal **GBA v1 — FROZEN** declaration and final audit evidence
>   (authoritative for freeze state; supersedes the freeze status in §24.1).

## 1. Executive Verdict

**Current recommendation: gpSP, using an INTERPRETER-FIRST architecture.**

**mGBA remains the fallback.**

Reasons for this recommendation:

- gpSP has a smaller practical porting surface. (LIKELY)
- gpSP exposes relatively simple video, audio, input and ROM/core boundaries. (LIKELY)
- gpSP has an interpreter path that does not require runtime-generated executable code. (VERIFIED)
- This allows initial emulator integration to proceed without depending on the dynarec. (VERIFIED — follows directly from the interpreter path existing)
- The primary unresolved risk is native image/JIT-backed size. (UNKNOWN — not yet measured)
- Therefore M1 will be an image-size GO/NO-GO gate placed *before* substantial emulator integration.

Choosing the interpreter path first deliberately trades away known performance for a
reduced set of runtime assumptions. It removes executable-memory generation from the
critical path of first bring-up, which is where the LuaPSX-derived runtime is most
constrained.

**Current project verdict: GO TO M0/M1 PLANNING.**

This verdict must **not** be interpreted as approval to begin implementation. It approves
planning work only. Implementation authorization is gated on M1 (see Section 16).

## 2. Project Architecture

The current target architecture is a layered stack in which the emulator core sits above a
thin adapter and never talks to the platform directly:

```
GBA ROM
   ↓
gpSP emulator core
   ↓
thin LUAport GBA adapter
   ↓
LUAport runtime
   ↓
LuaPSX-derived PS5 services
   ↓
native runtime environment
   ↓
PS5
```

Role of each layer:

- **LuaPSX** is being used primarily as the *proven runtime/platform reference*. Its value to
  this project is that it demonstrates a working path from a native binary to running code on
  the target, not that it emulates a PlayStation. The PSX emulation itself is out of scope.
- **gpSP** supplies GBA emulation. It is the source of CPU, video, audio, timer, DMA and save
  behaviour.
- **The LUAport GBA adapter** is intended to be thin. It should translate between gpSP's
  expectations (framebuffer format, audio sample delivery, key state, ROM/save I/O) and the
  LUAport runtime's platform services — and do little else.
- **LUAport runtime services** are the reusable layer: memory, timing, filesystem, video,
  audio, input, logging.

A core design principle for this project: **LUAport should separate emulator-specific code
from reusable platform services.** Emulator-specific logic belongs in the adapter, not in the
runtime.

The long-term goal is for the LUAport runtime services to potentially support other emulator
cores later. That goal is the reason the adapter boundary is being defined explicitly now
rather than allowed to emerge organically — but supporting a second core is a *future*
aspiration, not an M0/M1 requirement, and no work should be expanded to serve it yet.
(HYPOTHESIS — multi-core reuse is unproven and will remain so until at least one core ships.)

## 3. LuaPSX Runtime Findings

Previous reconnaissance identified reusable or potentially reusable LuaPSX functionality in
the following areas:

- native program bootstrap
- native binary delivery/loading
- linker/runtime arrangement
- relocation handling
- libc/runtime shim
- memory allocation
- video
- audio
- controller input
- timing
- filesystem
- `/temp0`
- `/savedata0`
- logging/UI support
- symbol resolution
- shutdown/cleanup

### Important constraint

**LuaPSX uses a constrained JIT-backed executable-memory arrangement.** (VERIFIED)

The current binary/linker architecture makes image layout important. This is not an
incidental implementation detail — it shapes what can be added to the image and how much
can be added, and it is the direct reason the image-size gate exists at M1 rather than later.
(See Sections 12 and 13.)

### Scope of the above list

The list above is a list of *areas where reusable functionality was identified*, not a list of
components certified as drop-in reusable. **Do not claim every LuaPSX component can be reused
unchanged.** Several of these areas are entangled with the PSX emulator to a degree that has
not yet been measured. (UNKNOWN)

**The purpose of M0 is to prove which runtime pieces can operate independently of the PSX
emulator.** M0 is a separation experiment, not an integration milestone. Until M0 reports,
the reuse status of each item above should be treated as LIKELY at best and never as VERIFIED.

## 4. LuaPSX Component Classification

The following is a **conceptual classification** of LuaPSX functionality, intended to guide
future extraction work.

| Category | Components |
| --- | --- |
| **REUSABLE RUNTIME** | basic libc/runtime shim; memory allocation; timing; filesystem primitives; video platform layer; audio platform layer; controller platform layer; logging; runtime/bootstrap concepts |
| **PSX-SPECIFIC ADAPTER** | PSX framebuffer handling; PSX-specific frame-production logic; PSX audio adaptation; PSX disc/media integration; PSX control mapping |
| **PCSX CORE** | PlayStation CPU emulation; GPU emulation; SPU/emulated audio; CD-ROM emulation; PSX memory/card behaviour |
| **BUILD/LOADER TOOLING** | native binary construction; linker layout; staged delivery/launcher concepts; host-side transfer tools |
| **VERIFY/TEST CODE** | relocation checks; libc compatibility checks; runtime verification; emulator-specific offline tests |

How to read this table:

- **REUSABLE RUNTIME** is the target of extraction — the layer LUAport wants to keep.
- **PSX-SPECIFIC ADAPTER** is the layer whose *shape* is instructive but whose *contents* will
  be replaced by the GBA adapter. It shows what an adapter has to do.
- **PCSX CORE** is out of scope entirely and is replaced by gpSP.
- **BUILD/LOADER TOOLING** is likely reusable with modification, and is on the critical path
  for M1 because it determines image layout.
- **VERIFY/TEST CODE** is partly reusable: the relocation and libc checks are generic; the
  emulator-specific offline tests are not.

**This is a conceptual classification for future extraction. No files are being moved yet.**
Boundaries in the table are provisional and are expected to shift once M0 measures actual
coupling. (HYPOTHESIS — the placement of individual components has not been validated by
extraction.)

## 5. gpSP Architecture

gpSP is currently preferred because its minimum core architecture is comparatively compact.
A smaller core means a smaller surface that must be understood, adapted and kept working
across the runtime boundary. (LIKELY)

Important subsystems:

- ARM7TDMI CPU execution
- GBA memory map
- cartridge ROM
- BIOS handling
- PPU/video
- audio
- timers
- DMA
- keypad/input
- cartridge save-memory support

### Frontend scope

The libretro frontend should be treated as an **architectural reference rather than something
LUAport needs to embed.** It is useful because it documents, in working form, the exact set of
services a frontend must provide to the core — video output, audio output, input polling, and
save/ROM handling. That service list is the specification for the LUAport adapter.

**Do not propose porting RetroArch.** The libretro frontend is read for its boundary
definition; it is not a dependency.

### Desired boundary

```
gpSP core
   → small LUAport adapter
   → platform runtime
```

The adapter should be the only place that knows both gpSP types and LUAport runtime types.
If emulator-specific knowledge leaks into the runtime layer, the multi-core goal in Section 2
is lost.

## 6. Interpreter Analysis

**Established finding: gpSP can execute using an interpreter path without requiring its
dynarec.** (VERIFIED)

This is strategically important. It means first bring-up does not have to solve the
executable-memory problem and the emulator-correctness problem at the same time.

Advantages of interpreter-first bring-up:

- no runtime-generated executable translation cache
- fewer executable-memory assumptions
- simpler debugging
- easier separation between core bugs and runtime bugs
- allows video/input/ROM integration to be proven before optimization

### Initial order of work

```
core correctness
   → interpreter execution
   → framebuffer
   → input
   → gameplay
   → performance measurement
```

Performance measurement is deliberately last. Measuring before the frame path and input path
are correct produces numbers that cannot be interpreted.

### Performance caveat

**Do not claim interpreter performance will be sufficient for final gameplay.** Whether the
interpreter can sustain acceptable GBA gameplay on this runtime is **UNKNOWN** until measured
on real hardware with a real ROM.

If interpreter performance is found to be inadequate, performance work becomes a **later
architecture decision** — one taken with measurements in hand. It is explicitly not a reason
to pull dynarec work forward into early milestones.

## 7. Dynarec Analysis

**Established finding: gpSP contains an x86_64 dynarec path.** (VERIFIED)

However, the existing dynarec expects runtime-generated executable translation blocks and a
larger / more flexible executable code-cache model than the current LuaPSX-derived runtime
provides. (LIKELY — the mismatch follows from the constrained JIT-backed executable-memory
arrangement described in Section 3, and has not been tested directly.)

Classification:

| Question | Status |
| --- | --- |
| **Existence of x86_64 dynarec** | **VERIFIED** |
| **Suitability for initial LUAport** | **NOT SUPPORTED / HIGH RISK** |
| **Initial decision** | **DO NOT USE** |

### Consequences of this classification

- **Do not make dynarec integration a normal milestone.** It must not appear in the M0–M15
  roadmap as ordinary scheduled work, because scheduling it implies a solved executable-memory
  model that does not currently exist.
- **The dynarec is not being deleted.** No removal work is authorized. Deleting it would
  discard a working implementation for no present benefit and would foreclose future options.
- It remains a **potential future research topic** if the LUAport runtime architecture later
  gains an appropriate executable-memory / code-cache mechanism. Such a mechanism is not
  planned and is not assumed. (HYPOTHESIS)

**Interpreter-first is the current architecture.**

---

*Evidence labels used in this report: **VERIFIED** (established by reconnaissance),
**LIKELY** (strongly supported inference), **HYPOTHESIS** (plausible, untested),
**UNKNOWN** (not determined; requires measurement).*

---

## 8. Video Boundary

**Established finding: gpSP's GBA framebuffer boundary is comparatively simple.** (LIKELY)

Native GBA display characteristics:

| Property | Value | Evidence |
| --- | --- | --- |
| Display resolution | 240 × 160 | VERIFIED |
| Framebuffer element type | `u16` | VERIFIED |

### Conceptual path

```
GBA PPU
   ↓
gpSP framebuffer
   ↓
thin LUAport video adapter
   ↓
LuaPSX-derived PS5 video layer
   ↓
PS5 framebuffer/presentation
```

### Initial scope

Initial implementation should prioritize **correctness**. The following are explicitly **not**
requirements for initial bring-up:

- shaders
- filters
- advanced scaling
- color correction
- presentation enhancements

The first goal is simply: **produce a valid GBA framebuffer → make it visible correctly on
PS5.** A correct, ugly frame is a success at this stage. A pretty frame that cannot be proven
correct is not.

### Open items

**Exact pixel-format conversion and scaling requirements still need to be established during
implementation.** (UNKNOWN)

**Do not assume `u16` means the PS5 backend accepts the same pixel layout directly.** The
element *width* being 16 bits says nothing about channel order, channel widths, alpha
handling, or endianness on the presentation side. Any of the following may require a
conversion step in the adapter, and none of them have been determined:

- channel ordering and bit allocation on each side
- whether a per-frame conversion pass is needed, and its cost
- scaling from 240 × 160 to the presented output size
- pixel-format negotiation with the LuaPSX-derived video layer

Resolving these belongs to implementation, not to this report. The adapter is the correct
place for any conversion, because conversion is emulator-boundary work rather than platform
work (see Section 2).

## 9. Audio Boundary

**Established finding: gpSP audio generation operates at approximately 65536 Hz.** (VERIFIED)

**This is not a typical host output rate.** Some adaptation between the core's rate and the
platform's accepted rate should therefore be expected. (LIKELY)

### Conceptual path

```
GBA APU
   ↓
gpSP audio buffer
   ↓
LUAport audio adapter
   ↓
buffering/resampling if required
   ↓
LuaPSX-derived PS5 audio backend
```

### Sequencing

**Audio is not required for the first emulator hardware test.** Audio belongs after the
following are stable:

- CPU execution
- video
- input
- basic gameplay

Attempting audio before the frame path is stable conflates two independent timing problems and
makes both harder to diagnose.

### Potential requirements

- sample-format adaptation
- sample-rate conversion
- buffering
- synchronization

**Do not choose a resampling algorithm yet.** (UNKNOWN) The choice depends on the output rate
and buffer model the LuaPSX-derived audio backend actually exposes, on measured CPU headroom
after interpreter costs are known, and on the acceptable quality/cost tradeoff — none of which
are determined. Selecting an algorithm now would be a guess presented as a decision.

## 10. Input Boundary

GBA controls:

- D-Pad
- A
- B
- L
- R
- Start
- Select

### Conceptual path

```
DualSense
   ↓
LUAport platform input state
   ↓
GBA keypad state
   ↓
gpSP
```

### Initial conceptual mapping

| DualSense | GBA |
| --- | --- |
| D-Pad / Left Stick | D-Pad |
| Cross | A |
| Circle | B |
| L1 | L |
| R1 | R |
| Options | Start |
| Create/Share | Select |

**Treat this mapping as configurable policy, not core architecture.** (HYPOTHESIS — the
mapping is a proposed default and has not been validated in use.) It is expected to change
based on play testing, and changing it must never require touching emulator code.

### Layering rule

**PS5-specific button constants should remain outside gpSP core logic.** The adapter converts
platform input state into GBA keypad state; gpSP should have no knowledge that a DualSense
exists. This is the same separation principle stated in Section 2, applied to input: if
platform button constants reach the core, both the multi-core goal and the remappability of
the table above are lost.

## 11. Memory Model

This section distinguishes memory categories that are frequently and damagingly conflated.
Each category has a different scarcity profile, and a constraint on one is not a constraint on
the others.

### A. Transferred native image

Includes loadable native payload contents. **Its size matters for the current LuaPSX/Luac0re
delivery/JIT architecture.** This is the quantity that Section 12's risk is about.

### B. JIT-backed / executable region

Under the current LuaPSX design, executable image content through `__data_start` is relevant to
the constrained JIT-backed mapping. (VERIFIED — current implementation behavior.) This is the
scarcest category in the current architecture.

### C. `.rodata`

**LuaPSX currently places `.rodata` in the text PHDR.** (VERIFIED) Therefore current `.rodata`
contributes to the constrained region. This is an architectural consequence significant enough
to warrant its own treatment — see Section 13.

### D. `.data`

Initialized writable data. Documented separately from executable and read-only image data
because it has a different placement and a different cost profile, even though it does
contribute to the transferred image.

### E. `.bss` / NOLOAD

**Established gpSP finding: large interpreter/runtime arrays exist in `.bss` / NOLOAD.**
(VERIFIED)

These **require runtime memory reservation but do NOT contribute equivalent bytes to the
transferred binary blob.** This distinction is important when reading M1 results: a large
`.bss` figure is not evidence of an image-size problem, and treating it as one would trigger
optimization work against the wrong target.

### F. GBA emulated memory

Separate from native program image size. Includes conceptually:

- GBA EWRAM
- GBA IWRAM
- VRAM
- palette/OAM state
- emulator working memory

### G. ROM

**GBA ROM storage must also be treated separately from executable image size.**

**Do not assume the ROM needs to be embedded into the native payload.** (UNKNOWN — the
delivery mechanism for ROM content has not been decided.) If ROM content can be loaded from
the filesystem rather than linked into the image, it does not interact with the Section 12
constraint at all. This is a consequential open question, not a detail.

### H. Framebuffer / audio buffers

Ordinary runtime memory requirements. These are ordinary allocations and are not expected to
interact with the constrained region.

### Central point

> **BINARY/JIT SIZE is NOT the same as TOTAL RUNTIME MEMORY.**

Categories A, B, C and D are image-size concerns. Categories E, F, G and H are runtime-memory
concerns. The 512 KB constraint discussed in Section 12 applies to the former. Conflating the
two produces both false alarms (counting `.bss` and emulated GBA RAM against a binary budget)
and false comfort (assuming ample system RAM resolves an image constraint). Every figure
reported by M1 must state which category it belongs to.

## 12. Image-Size Risk

**This is currently one of the highest project risks. Rated HIGH until measured.**

**Established finding: reconnaissance identified an approximately 512 KB constraint associated
with the current LuaPSX JIT-backed native payload architecture.** (VERIFIED — as an identified
constraint of the current architecture.)

**The exact minimal gpSP interpreter image size has NOT been measured.** (UNKNOWN)

**Therefore M1 exists specifically to measure it.**

### What M1 must establish

- `.text` size
- `.rodata` size
- `.data` size
- `.bss` size
- relocation data
- final native binary size
- JIT-backed portion
- ordinary RW-memory requirement

Each figure must be attributed to a Section 11 category. A total without that attribution is
not an answer to this risk.

### Major contributors to identify

- `gba_cc_lut`
- video template expansion
- interpreter implementation
- optional features
- libretro-only code
- debugging/logging
- unused platform backends

### Required sequence

```
BASELINE
   → MEASURE
   → CLASSIFY
   → IDENTIFY LARGE CONTRIBUTORS
   → DECIDE WHETHER OPTIMIZATION IS NECESSARY
```

**Do not optimize before obtaining a baseline.** Optimization performed before measurement
targets guesses, cannot be shown to have helped, and risks spending effort on contributors that
turn out to be negligible. The decision of *whether to optimize at all* is itself an output of
this sequence, not an assumption going into it.

### Severity framing

**Do not call the 512 KB issue a project-killer yet.** Its severity depends on actual M1
measurements. The plausible outcomes span a wide range — the interpreter build may fit
comfortably, may fit after identifiable trimming, or may exceed the constraint by a margin that
forces an architectural response. Which of these applies is exactly what is unknown, and the
gate exists to find out before integration effort is committed. (UNKNOWN)

## 13. `.rodata` / JIT Architectural Question

**This section is critical.**

### Established current behavior

LuaPSX's current linker arrangement places `.rodata` inside the **text PHDR**. (VERIFIED)

The current loader/JIT arrangement maps the native image through `__data_start`. (VERIFIED)

**Therefore, under the CURRENT architecture, `.rodata` contributes to the scarce JIT-backed
image.** (VERIFIED — as current implementation behavior.)

### Important distinction

This proves **CURRENT IMPLEMENTATION BEHAVIOR**.

It does **NOT** prove: *"all immutable data fundamentally requires executable/JIT-backed
storage."*

The observed behavior is a property of the linker script and loader arrangement that LuaPSX
happens to use. Treating it as a fundamental law of the platform would rule out an entire class
of solutions without evidence.

### Open architectural question

> Could future LUAport place suitable immutable data in ordinary readable memory instead of the
> scarce JIT-backed region?

**Status: UNKNOWN.** No existing evidence directly proves this is possible, and none proves it
impossible. It is recorded here as an open question precisely because its answer would
materially change the Section 12 risk profile.

**Do not redesign the linker yet.** Investigating this before M1 produces a baseline would mean
redesigning against an unmeasured problem.

### `gba_cc_lut`

**Established gpSP finding:** (VERIFIED)

| Property | Value |
| --- | --- |
| Contents | 32768 × `u16` |
| Size | 65536 bytes (64 KB) |

The table is generated from deterministic mathematical calculations. **It therefore represents
a potentially interesting future size optimization** — deterministic generation means the bytes
are reconstructible rather than irreducible. (LIKELY)

Possible strategies to investigate **after M1**:

| Option | Strategy |
| --- | --- |
| **A** | Keep it in `.rodata`. |
| **B** | Generate it at runtime into ordinary RW memory. |
| **C** | Place it in a different readable non-executable segment, if the LUAport runtime eventually supports such a layout. |

**These are FUTURE OPTIONS. Do not recommend one yet.** Each carries costs that are not yet
quantified — startup time and RW footprint for B, and dependence on the unresolved question
above for C.

**The baseline M1 build should first measure the existing cost.** Option A is the baseline by
definition, and the value of B and C cannot be assessed until the cost of A is known.

## 14. gpSP vs mGBA

**Current reconnaissance favors gpSP.**

### Comparison

| Dimension | gpSP | mGBA |
| --- | --- | --- |
| Minimum practical source surface | Smaller (LIKELY) | Larger (LIKELY) |
| Core architecture complexity | Comparatively compact | Modular and layered; more structure to traverse |
| Frontend/platform dependency surface | Relatively contained | Broader; more platform abstraction to satisfy |
| Interpreter availability | Available (VERIFIED) | Available (LIKELY) |
| Dynarec considerations | x86_64 dynarec exists but is unsuitable for the initial runtime (see Section 7) | Not evaluated for this project (UNKNOWN) |
| Video boundary | Comparatively direct; 240 × 160 `u16` (Section 8) | Viable; mediated by more framework structure (LIKELY) |
| Audio boundary | Direct buffer, but ~65536 Hz requires adaptation (Section 9) | Viable; not evaluated in detail (UNKNOWN) |
| Input boundary | Simple keypad state (Section 10) | Viable; not evaluated in detail (UNKNOWN) |
| Filesystem/ROM integration | Comparatively direct | More VFS infrastructure to account for (LIKELY) |
| Supporting framework size | Less supporting infrastructure to adapt | Larger supporting framework (LIKELY) |
| Expected initial port effort | Lower (LIKELY) | Higher (LIKELY) |
| Major risk | Native image size UNKNOWN; interpreter performance UNKNOWN | Porting surface and framework adaptation cost |

### gpSP

**Advantages:**

- smaller practical porting surface
- relatively direct framebuffer/audio/input boundaries
- interpreter available
- suitable for interpreter-first bring-up
- less supporting infrastructure to adapt

**Risks:**

- exact native image size unknown
- interpreter performance unknown
- existing dynarec unsuitable/high-risk for initial runtime
- audio requires adaptation

### mGBA

**Advantages:**

- mature
- modular
- strong emulator architecture
- viable fallback

**Risks for this project:**

- larger supporting framework
- larger practical porting surface
- more VFS/utility/config infrastructure to account for

Note that mGBA's principal advantages and its principal risks arise from the same property.
Its maturity and modularity are real engineering strengths; they also imply more framework to
adapt across a constrained runtime boundary. The preference for gpSP is a judgment about
porting cost under this project's specific constraints, not a judgment about emulator quality.

### Current recommendation

**gpSP.**

### Fallback trigger

Reevaluate mGBA if **any** of the following occur:

1. M1 gives gpSP a **RED** size result, **OR**
2. gpSP interpreter performance later proves impractical, **OR**
3. an unexpected runtime dependency makes gpSP substantially harder than current
   reconnaissance indicates.

**Do NOT automatically switch to mGBA merely because gpSP requires some optimization.**
Needing optimization is an expected and ordinary outcome of M1; it is not a fallback trigger.
Switching cores discards the reconnaissance investment already made and buys an unmeasured
porting surface in exchange for a measured one. The triggers above are deliberately narrow.

## 15. M0 Design

**M0 is: GENERIC LUAport DIAGNOSTIC RUNTIME.**

**M0 contains NO GBA emulator.**

### Purpose

Prove that the reusable platform/runtime pieces can operate **independently of the PSX
emulator** before gpSP is introduced. M0 is the separation experiment promised in Section 3.
Its output is evidence about which items in the Section 3 list are genuinely reusable, which
is currently LIKELY at best and never VERIFIED.

Introducing gpSP before this is proven would mean debugging an unproven runtime and an
unintegrated emulator simultaneously, with no way to attribute a failure to either.

### Conceptual path

```
Luac0re
   ↓
LUAport native binary
   ↓
runtime/bootstrap initialization
   ↓
memory allocation
   ↓
timing
   ↓
video initialization
   ↓
controller initialization
   ↓
diagnostic screen
   ↓
visual response to controller input
   ↓
clean shutdown
```

### Minimum test coverage

M0 should test at minimum:

- native entry/bootstrap
- relocations required by the runtime
- libc/runtime shim essentials
- heap allocation/free
- timing
- video initialization
- framebuffer presentation
- controller polling
- basic logging/diagnostics
- filesystem availability, if needed for later milestones
- clean shutdown

### Suggested diagnostic behavior

Display:

```
LUAport Runtime
M0 Diagnostic
```

Then show:

- frame counter
- elapsed time
- controller state
- heap/allocation status
- runtime status

**Controller presses should visibly change diagnostic state on screen.** This matters more
than it appears: a visible input-to-pixel response proves the input path, the frame path and
the presentation path are simultaneously live, which no static screen can demonstrate.

### M0 success criteria

- native LUAport binary launches
- diagnostic framebuffer is visible
- frame counter advances
- timing behaves consistently
- DualSense state is detected
- input visibly changes the diagnostic display
- basic allocations succeed
- runtime exits/returns cleanly
- **no PCSX/PSX emulator core is required**

The final criterion is the load-bearing one. If M0 only runs with PSX emulator components
present, the separation has not been demonstrated regardless of what appears on screen.

### M0 failure

**Do not proceed to gpSP integration. Fix the reusable runtime first.**

A runtime defect carried into M1 or later will present as an emulator bug and will be
diagnosed against the wrong subsystem.

## 16. M1 GO/NO-GO Gate

**M1 is: MINIMAL gpSP INTERPRETER BUILD + EXACT IMAGE-SIZE MEASUREMENT.**

**M1 is NOT yet a gameplay milestone.** No frame is expected. No ROM is expected to run.

### Purpose

Determine whether a minimum viable gpSP interpreter configuration fits the current LUAport
runtime architecture **before substantial integration work is committed.** This is the gate
referenced in Sections 1, 3, 12 and 13.

### Required measurements

M1 must produce exact measurements for:

- `.text`
- `.rodata`
- `.data`
- `.bss`
- relocation data
- final native binary size
- JIT-backed portion
- ordinary RW-memory requirement

Per Section 11, **each figure must state which memory category it belongs to.** An unattributed
total cannot be compared against the constraint.

### Major contributors to identify

- `gba_cc_lut`
- video template expansion
- interpreter implementation
- optional features
- libretro-only code
- debugging/logging code
- unused platform backends

### Measurement order

```
1. BASELINE SIZE
2. MINIMUM REQUIRED SIZE
3. JIT-BUDGET REQUIREMENT
4. ORDINARY-RW-MEMORY REQUIREMENT
5. LARGE-CONTRIBUTOR BREAKDOWN
```

**Do NOT optimize before obtaining the baseline.** Without a baseline there is nothing to
compare a reduction against, and no way to show that an optimization helped.

### GREEN

Minimal gpSP interpreter configuration fits within the current runtime/JIT constraints with a
useful safety margin.

**Result: GO — continue with gpSP.**

**Do not invent a numeric safety-margin threshold until actual measurements exist.** The
margin required depends on what still has to be added after M1 — adapter code, save handling,
ROM access — whose sizes are not yet known.

### YELLOW

Minimum gpSP is close to or exceeds the practical target, but appears viable after reasonable
architectural cleanup.

Examples of such cleanup:

- remove unused frontend/platform code
- remove debugging-only code
- exclude unused optional functionality
- avoid libretro-only infrastructure
- investigate large immutable-data placement (Section 13)
- investigate runtime generation of suitable deterministic tables (Section 13, option B)

**Result: CONDITIONAL GO.** Perform targeted size work, then **repeat M1 measurements.** The
repeat measurement is mandatory — cleanup that has not been re-measured has not been shown to
have moved the number.

### RED

Minimum viable gpSP interpreter configuration cannot realistically fit the current
architecture without major redesign.

**Result: NO-GO for gpSP under the current architecture.**

**Do NOT force gpSP.** Trigger a focused mGBA minimum-image evaluation — this is fallback
trigger 1 in Section 14, and the UNKNOWN cells in that section's comparison table become the
work list for the reevaluation.

### Important

**Do not assign GREEN/YELLOW/RED now. M1 has not been executed.**

**Current M1 status: UNKNOWN / NOT YET MEASURED.**

## 17. First GBA Hardware Test

This occurs only after **M0 succeeds AND M1 produces an acceptable result** (GREEN, or YELLOW
resolved by re-measurement per Section 16).

Use a **legally distributable GBA homebrew/test ROM.**

### Minimum path

```
GBA test ROM
   ↓
ROM loader
   ↓
gpSP initialization
   ↓
BIOS/setup as required
   ↓
interpreter CPU execution
   ↓
GBA PPU
   ↓
valid 240×160 framebuffer
   ↓
LUAport video adapter
   ↓
PS5 presentation
```

### First test success

**A valid emulator-generated GBA framebuffer appears on the PS5 and updates as the emulated
program executes.**

The word *updates* carries the weight here. A single correct frame can result from a
partially working path; a frame that changes in response to emulated execution demonstrates
that CPU execution, PPU output and presentation are all live together.

### Not required yet

- audio
- save persistence
- ROM picker
- dynarec
- advanced scaling
- broad game compatibility

**Prefer a small diagnostic/homebrew ROM that gives visually obvious output.** A commercial
title exercises far more of the core at once and turns a first-light test into a compatibility
investigation.

## 18. Revised M0-M15 Roadmap

| Milestone | Goal | Primary success criterion | Gate / dependency |
| --- | --- | --- | --- |
| **M0** | Generic LUAport diagnostic runtime | Diagnostic screen runs and responds to input with **no PSX emulator core required** | None — entry point |
| **M1** | Minimal gpSP interpreter build + exact image-size measurement | All Section 16 figures measured and category-attributed; GREEN/YELLOW/RED assigned | M0 success |
| **M2** | Core initialization | gpSP core initializes under the LUAport runtime without fault | M1 not RED |
| **M3** | BIOS | BIOS handling satisfied per gpSP's requirements | M2 |
| **M4** | ROM loading | ROM content reaches the core via the adapter | M3 |
| **M5** | CPU execution | Interpreter executes emulated instructions correctly | M4 |
| **M6** | First valid framebuffer | Core produces a valid 240 × 160 frame | M5 |
| **M7** | PS5 presentation | Frame is visible and updating on PS5 (Section 17 first-light test) | M6 |
| **M8** | DualSense input | Input reaches GBA keypad state and affects emulated execution | M7 |
| **M9** | Stable gameplay / performance measurement | Sustained gameplay observed; **performance measured, not assumed** | M8 |
| **M10** | Audio | Audio output present and synchronized | M9 stable |
| **M11** | Save-memory detection | Cartridge save type correctly detected | M9 |
| **M12** | Persistent saves | Saves persist across sessions via platform storage | M11 |
| **M13** | ROM picker | User can select a ROM at runtime | M12 |
| **M14** | Profiling/optimization | Measured bottlenecks identified and addressed | M9 measurements exist |
| **M15** | Compatibility testing | **COMPLETE** — behaviour characterized across a ROM set; evidence recorded in Section 23 | M13, M14 |

**Roadmap status (current).** M0-M13 are delivered and hardware-validated; **M14
(profiling/optimization) is COMPLETE**; M15 is **COMPLETE** (Section 23). The measured
outcomes that closed the M1 size gate and the M9 performance measurement are recorded in
**Section 24**, which supersedes the UNKNOWN markers carried in Sections 12, 16 and 19.
This table's per-row criteria are retained as written for historical accuracy.

### Dynarec is not on this roadmap

**Dynarec enablement is NOT a normal roadmap milestone.** Per Section 7, listing it as
scheduled work would imply a solved executable-memory model that does not currently exist.

If interpreter performance is inadequate, **optimization architecture is reconsidered after
measurement (M9/M14) rather than assuming dynarec is the answer.** Measurement may well point
at the frame path, the conversion step, or a specific interpreter hot path instead. Choosing
the remedy before seeing the measurement is the error this roadmap is structured to avoid.

## 19. Risk Register

| # | Risk | Current rating | Evidence status | Impact | Mitigation / decision point |
| --- | --- | --- | --- | --- | --- |
| 1 | Native image / JIT budget | **HIGH** | 512 KB constraint VERIFIED; required size UNKNOWN | Could invalidate gpSP under current architecture | Measure at **M1**; GREEN/YELLOW/RED per Section 16 |
| 2 | `.rodata` placement | **HIGH** | Current placement VERIFIED; alternative placement UNKNOWN | Consumes scarce JIT-backed region | Investigate after M1 baseline; Section 13 |
| 3 | gpSP interpreter performance | **HIGH** | UNKNOWN until hardware measurement | May make gameplay impractical | Measure at **M9**; remedy chosen after measurement |
| 4 | gpSP dynarec incompatibility | **HIGH** | Existence VERIFIED; mismatch LIKELY | Removes the obvious performance remedy | **Initial mitigation: exclude dynarec**; interpreter-first (Section 7) |
| 5 | libc/runtime shim gaps | **MEDIUM/HIGH** | UNKNOWN — coverage not enumerated | Blocks core bring-up | Exercise at **M0**; confirm at **M2** |
| 6 | Relocation/linker assumptions | **HIGH** | Arrangement identified; adequacy for gpSP UNKNOWN | Binary may not load or run correctly | **M0/M1** |
| 7 | Heap / ordinary RW-memory requirements | **MEDIUM** | UNKNOWN — gpSP RW needs unmeasured | Runtime allocation failure | **M0/M1**; category E–H per Section 11 |
| 8 | Alignment assumptions | **MEDIUM** | UNKNOWN | Faults or silent corruption | Validate during bring-up (M0–M2) |
| 9 | Framebuffer pixel-format conversion | **MEDIUM** | UNKNOWN — layouts not determined (Section 8) | Wrong colors, or per-frame conversion cost | **M6/M7** |
| 10 | Framebuffer scaling/presentation | **LOW/MEDIUM** | UNKNOWN | Visual quality; modest cost | **M7** |
| 11 | Audio sample-rate adaptation | **MEDIUM** | ~65536 Hz VERIFIED; target rate UNKNOWN | Audio unusable without conversion | **M10** |
| 12 | Audio synchronization | **MEDIUM/HIGH** | UNKNOWN | Drift, underruns, or pacing conflicts with video | **M10**, after video timing stable |
| 13 | Emulator timing | **HIGH** | UNKNOWN | Incorrect speed; input latency; audio/video desync | **M9** |
| 14 | Filesystem/ROM access | **MEDIUM** | Primitives identified; ROM delivery UNKNOWN (Section 11G) | Blocks ROM loading | **M4** |
| 15 | BIOS handling | **MEDIUM** | UNKNOWN — requirements not enumerated | Blocks correct execution of some titles | **M3** |
| 16 | Save-memory detection | **MEDIUM** | UNKNOWN | Wrong save type; data loss | **M11** |
| 17 | Persistent save integration | **MEDIUM/HIGH** | UNKNOWN — `/savedata0` behaviour unproven for this use | User-visible data loss | **M12** |
| 18 | C/C++ runtime assumptions | **MEDIUM/HIGH** | UNKNOWN | Init-order or runtime-feature failures at startup | **M1/M2** |
| 19 | mGBA fallback uncertainty | **MEDIUM** | Several comparison areas remain UNKNOWN (Section 14) | Fallback cost cannot currently be estimated | Resolve only if a Section 14 trigger fires |

**Do not manufacture certainty.** Most entries above are UNKNOWN by evidence status, and that
is the accurate state of the project at the end of reconnaissance. Ratings express *concern
weighted by consequence*, not measured probability. They are expected to move — in both
directions — as milestones report.

## 20. Files Likely Modified Later

**Do NOT modify them now.** This section describes future *categories*, not a change list.

Likely future LUAport-owned files may include:

- LUAport runtime source
- runtime headers
- GBA adapter
- video adapter
- audio adapter
- input adapter
- filesystem adapter
- runtime shim
- linker configuration
- build configuration
- host-side launcher/transfer tooling
- diagnostic/test code

### Architectural preference

**Avoid modifying upstream gpSP source unnecessarily.**

Prefer:

```
upstream gpSP
   +
thin LUAport adaptation layer
```

where practical.

This preserves the ability to track upstream, keeps the emulator/platform boundary legible,
and directly serves the multi-core goal in Section 2 — a core that has been edited into
LUAport-specific shape is harder to replace or re-source than one held behind an adapter.

**If upstream changes become necessary later, document them explicitly and keep them minimal.**
Undocumented upstream edits accumulate into a private fork that nobody planned to maintain.

## 21. Files To Leave Untouched Initially

Until implementation planning is separately approved:

**DO NOT MODIFY:**

- `LuaPSX/`
- `gpsp/`
- `mgba/`

**Treat all three as reference/upstream source trees.**

When implementation begins, prefer creating a **separate LUAport-owned source structure**
rather than immediately editing the reference repositories.

### Possible conceptual future layout

```
LUAport/
├── LuaPSX/
├── gpsp/
├── mgba/
├── runtime/
├── adapters/
│   └── gba/
├── tests/
└── build/
```

**This is conceptual only. Do NOT create these directories during this report task.**
The layout is shown to illustrate the ownership boundary — reference trees on one side,
LUAport-owned code on the other — not to authorize scaffolding.

## 22. GO / NO-GO

**Current project decision: GO TO M0/M1 PLANNING.**

**NOT: GO directly to emulator implementation.**

### Reasons

- gpSP remains the preferred candidate. (Section 14)
- Interpreter-first provides the simplest initial CPU execution strategy. (Section 6)
- Reusable LuaPSX runtime concepts have been identified. (Section 3)
- Major architecture risks are now isolated into measurable milestones. (Sections 18, 19)
- **Image size remains unresolved and is explicitly gated by M1.** (Sections 12, 16)
- **Interpreter performance remains unresolved and is explicitly measured later.** (M9)

The last two points are why this is a planning verdict rather than an implementation verdict.
Both unknowns are consequential enough to change the architecture, and both now have a
defined place where they get answered.

### Immediate next activity, AFTER REPORT REVIEW

**Design M0 implementation.**

Then:

```
execute M0
   → review results
   → design M1
   → execute M1
   → assign GREEN/YELLOW/RED
```

**Do not begin these activities during this task.**

### Final current status

> **The table below is HISTORICAL.** It records the pre-implementation position and is
> retained unedited. For current status see **Section 24**.

| Item | Status |
| --- | --- |
| **Core candidate** | gpSP |
| **CPU strategy** | Interpreter first |
| **Dynarec** | Excluded from initial architecture |
| **M0** | Not started |
| **M1** | Not measured |
| **Project** | **GO TO M0/M1 PLANNING** |

**Sections 1-22 above are the original pre-implementation reconnaissance and are preserved
unedited as a historical record.** They state the project's position *before* any milestone
ran, including the stale "Not started" entries in the table immediately above. **Section 23
records M15 outcomes and is the authority for compatibility evidence; Section 24 records the
verified freeze-candidate baseline and is the authority for build, gate and image state.**

## 23. M15 Compatibility Evidence

**M15 status: COMPLETE.** Criterion (Section 18): *"Behaviour characterized across a ROM set."*
Characterization — not universal compatibility — is the bar. A documented defect and a
documented untested class both satisfy it, provided each is recorded with its evidence.

### 23.1 Status vocabulary — these five terms are not interchangeable

| Term | Meaning |
| --- | --- |
| **HARDWARE PASS** | Observed on PS5 hardware. |
| **HARDWARE PASS WITH CORE COMPATIBILITY ISSUE** | Ran on hardware; a defect attributable to the emulator core, not the LUAport runtime. |
| **SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED** | Behaviour derived from source with citations; never observed on hardware. |
| **NOT PRESENT IN CURRENT TESTED LIBRARY** | No cartridge tested to date reaches this path. |
| **EVIDENCE NOT RECORDED** | Not captured. **Not** a pass and **not** a failure. |

**Source-derived behaviour is never recorded as a hardware PASS.** Where a claim rests on
reading code rather than observing hardware, it is labelled as such and cited.

### 23.2 Save-class matrix

| Class | Declared / region | Status | Evidence |
| --- | --- | --- | --- |
| **SRAM** | 32768 / 32768 | **HARDWARE PASS ×2** | Metroid Fusion; Kingdom Hearts: Chain of Memories (§23.4) |
| **FLASH128** | 131072 / 131072 | **HARDWARE PASS** | Super Mario Advance 4 (`AX4E`), database-typed cartridge |
| **EEPROM512** | 512 / 8192 | **HARDWARE PASS** | Tony Hawk's Pro Skater 2 — full create/commit/relaunch/restore cycle |
| **EEPROM family, subtype unresolved** | — / 8192 | **HARDWARE PASS (family)**; subtype **EVIDENCE NOT RECORDED** | Zelda `AZLE` (§23.3) |
| **FLASH64** | 65536 / 65536 | **SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED** + **NOT PRESENT IN CURRENT TESTED LIBRARY** | §23.5 |
| **EEPROM8K** | 8192 / 8192 | **SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED** | §23.6 |

### 23.3 Zelda: A Link to the Past / Four Swords (`AZLE`) — restore-only session

Observed on PS5, evidence capture only; no new save was intentionally written.

| Field | Observed |
| --- | --- |
| ROM size | **8 MB** |
| Launch `SAVE:` | **UNKNOWN / UNKNOWN** |
| `SAVE FILE:` | **FOUND** |
| `RESTORE LOADED` | **8192 bytes** |
| In-game | existing save visibly recognized; `NBT` slot present and correct |
| `DIRTY` | **NO** |
| `SAVE UNCHANGED` | **0 bytes** |
| Performance | 1818 frames / 30665 ms / **59.28 fps** |

**What this proves:** EEPROM-family persistence and restore on hardware, plus a
**restore-only session with a correctly declined commit** — a path no other tested title had
exercised. Restore applied, the game recognized prior progress, nothing was written.

**Subtype is NOT resolved by this evidence, and the 8192 must not be read as EEPROM8K.**
`gba_savehdr_region_of` returns 8192 for **both** EEPROM512 and EEPROM8K
(`adapters/gba/gba_savehdr.c:114-120`), because 512 is a strict prefix of the 8 KB EEPROM
window. `RESTORE LOADED` reports `gba_restore_applied()`, which counts bytes copied into the
backup region (`apps/m13cgpsp/main.c:2639`; `adapters/gba/gba_restore.c:213`) — a **region**
figure, which cannot discriminate the two subtypes. The subtype discriminator is the
*declared* size (512 vs 8192, `gba_savehdr.c:96-105`), which the on-screen panel does not show.

**What it does rule out, arithmetically rather than by inference:** a validated header's region
must equal `gba_savehdr_region_of(cls)` exactly or the file is rejected. The restore was
accepted at 8192, so the class is in {EEPROM512, EEPROM8K}. **SRAM (32768), FLASH64 (65536)
and FLASH128 (131072) are excluded.** Zelda is **not** FLASH64 — a FLASH64 restore reports
65536.

`AZLE` is **absent from `gpsp/gba_over.h`**, so no database entry asserted a type; the
`UNKNOWN / UNKNOWN` panel reads the **live** class, which collapses only once the running game
touches the backup region. The header on disk carries the class resolved in an earlier session.
There is no contradiction between a `UNKNOWN / UNKNOWN` launch and a valid EEPROM-family save.

### 23.4 Kingdom Hearts: Chain of Memories (`B8CE`) — UNKNOWN-collapse persistence

| Field | Observed |
| --- | --- |
| ROM size | **32 MB** |
| Launch `SAVE:` | **UNKNOWN / UNKNOWN** |
| First session persisted | **32768 bytes** |
| Host relaunch | performed |
| `SAVE FILE` | **FOUND** |
| `RESTORE LOADED` | **32768 bytes** |
| In-game | persistent progress restored successfully |
| Performance | ~**59.3 fps** |

**SRAM / UNKNOWN-collapse persistence path: HARDWARE PASS.** `B8CE` is absent from
`gba_over.h`; an unresolved class collapses to SRAM, and this is the hardware confirmation of
that path end to end. It also supplies the 32 MB demand-paging data point.

### 23.5 FLASH64 — not exercised, not present

**SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED / NOT PRESENT IN CURRENT TESTED LIBRARY.**

No cartridge tested to date has reported `FLASH 64K` at the launch boundary. Every occurrence
of that string in the tree is a display-string table, a host equivalence-test assertion, or a
design comment — none is a hardware observation. A scan-detected FLASH64 cartridge *would*
print `FLASH 64K` at launch, so screening for this class is a log read rather than a
hardware run. Zelda, the last tested cartridge whose class was resolved solely by signature
scan, resolved to the EEPROM family instead.

**This status was not inferred** from bank-count defaults, ROM size, franchise, database
absence, a live UNKNOWN reading, or save-file size.

**Latent defect, characterized and retained:** a cartridge that the signature scan misses but
which asserts flash at runtime (`gpsp/gba_memory.c:1148`) commits 65536 bytes, and
`gba_savehdr.c:232-238` then refuses FLASH64 absolutely under a live UNKNOWN — the widening
granted to FLASH128 is deliberately *not* extended to the FLASH family. No tested title
carries this combination. Widening that refusal is a **post-v1 policy decision**, not an M15
change, and no such change was made.

### 23.6 EEPROM8K — not reachable by ROM selection

**SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED.**

Promotion to `EEPROM_8_KBYTE` occurs at exactly one site (`gpsp/gba_memory.c:841-843`) and
requires the **running game** to issue a DMA3 transfer to `0x0D` with `length & 0x1F == 17`.
The size re-defaults to 512 on every reset (`gba_memory.c:531`). The class therefore cannot be
reached by choosing a ROM — only by a specific runtime access pattern.

**This is not upgraded by Zelda's 8192-byte restore** (§23.3). No cartridge candidate is
asserted for this class.

An unexercised discriminator exists for future opportunistic capture: the debug stream already
emits `header class %u` (`apps/m13cgpsp/main.c:2642-2644`), printing 4 for EEPROM512 or 5 for
EEPROM8K, where the on-screen panel prints neither.

### 23.7 Functional matrix

| Category | Status | Evidence |
| --- | --- | --- |
| Boot | **HARDWARE PASS** | 10 titles |
| Sustained gameplay | **HARDWARE PASS** | Zelda 59.28 fps; Kingdom Hearts ~59.3 fps |
| Input | **HARDWARE PASS** | across tested titles |
| Audio | **HARDWARE PASS** | across tested titles |
| Presentation | **HARDWARE PASS** | across tested titles |
| Graphics | **HARDWARE PASS WITH CORE COMPATIBILITY ISSUE** | `CORE-COMPAT-001` (§23.8) |
| Demand paging | **HARDWARE PASS** | 8 MB (Zelda, Metroid Fusion) and 32 MB (Kingdom Hearts) |
| Save creation / commit | **HARDWARE PASS** | 4 titles |
| Host relaunch / save restore | **HARDWARE PASS** | 5 titles |
| Restore-only, commit correctly declined | **HARDWARE PASS** | Zelda (§23.3) |
| 8 MB ROM | **HARDWARE PASS** | Zelda (observed), Metroid Fusion |
| 16 MB ROM | **EVIDENCE NOT RECORDED** | bracketed by 8 MB and 32 MB passes |
| 32 MB ROM | **HARDWARE PASS** | Kingdom Hearts |

No 16 MB test is outstanding as a requirement: demand paging is size-parametric, and every
cartridge above the buffer threshold traverses one identical path that 8 MB and 32 MB already
bracket.

### 23.8 Core-compatibility register

| ID | Title | Status | Attribution |
| --- | --- | --- | --- |
| **CORE-COMPAT-001** | The Sims 2 | **HARDWARE PASS WITH CORE COMPATIBILITY ISSUE** | gpSP-attributed rendering behaviour |

**`CORE-COMPAT-001` is not a demonstrated LUAp0rt runtime regression.** The title boots, runs
and responds; the defect is rendering behaviour originating in the emulator core.

**Classification rule.** A defect is recorded as `CORE-COMPAT` when it reproduces in
gpSP-family emulation, involves no LUAport-owned adapter, and shows no `.bss`, JIT or
relocation anomaly. Modifying upstream gpSP for a single title remains out of scope per
Section 20.

## 24. GBA v1 Freeze-Candidate Baseline

**This section is authoritative for build, gate, image and configuration state.** Every
figure below was measured from the current tree and the regenerated artifact, not carried
forward from an earlier milestone.

### 24.1 Freeze status

| Item | State |
| --- | --- |
| **M15** | **COMPLETE** (evidence: Section 23) |
| **M14** | **COMPLETE** |
| **Frozen baseline** | **M13C** |
| **GBA v1 freeze** | **FROZEN** (declared 2026-09-17 — see **§25**) |

**GBA v1 is FROZEN; see §25 for the declaration and its evidence.** This section was written
while M13C was still a *freeze candidate*, and the condition it set out has since been met:
freezing required a separate, explicit operator declaration issued after a final freeze audit
that re-proved every figure in §24.2-§24.4 against a freshly regenerated binary. That audit
ran on **2026-09-17**, the forced regeneration reproduced the recorded SHA-256 exactly, and
the declaration was issued. The measurements in §24.2-§24.5 stand unchanged and now describe
the frozen baseline rather than a candidate.

### 24.2 Regression gates

| Gate | Result |
| --- | --- |
| `make m13c-check` | **PASS** |
| `make m13b-equiv` | **283 checks, 0 failures** — `M13B EQUIV OK` |
| `make m12c-equiv` | **850 checks, 0 failures** — `M12C EQUIV OK` |
| `make m13c-verify` | **`M13C VERIFY OK`** |
| Relocations | **230** `R_X86_64_RELATIVE` |
| Relocation verdict | **safe to apply** |
| Closure | **clean** |
| Genuinely unresolved symbols | **0** |

### 24.3 Image measurements

| Item | Measured | Limit / budget | Headroom |
| --- | --- | --- | --- |
| `.text` | 256,560 bytes | — | — |
| `.rodata` | 36,332 bytes | — | — |
| Relocation data | 5,520 bytes | — | — |
| `.data` | 10,368 bytes | — | — |
| `.bss` | **2,011,328 bytes** | 2,097,152 bytes | **85,824 bytes** |
| `__data_start` | **393,216 bytes / 0x60000** | — | — |
| JIT footprint | **393,216 bytes** | 524,288 bytes | **131,072 bytes** |
| Final binary | **308,784 bytes** | — | — |

### 24.4 Artifact identity

```
build/m13c/m13cgpsp.bin
  size       : 308,784 bytes
  SHA-256    : e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a
  regenerated: 2026-09-16 20:50:05  (explicit `make m13c`, post-Phase-2)
```

This hash is **byte-for-byte identical** to the pre-cleanup, hardware-passed baseline
recorded during M15 (Section 23), and was reproduced again by the 2026-09-17 final forced
regeneration that established the freeze (**§25.2**).

### 24.5 Configuration invariants

| Invariant | Value | Source of truth |
| --- | --- | --- |
| ROM picker capacity | **64 ROMs** | `adapters/gba/gba_library.h` — `GBA_LIB_MAX_ENTRIES 64` |
| ROM buffer size | **`ROM_BUFFER_SIZE = 2`** | `Makefile` — `M13C_GPSPFLAGS` |

Both are cited to their defining site rather than restated as prose, so either can be
re-verified directly against the tree.

### 24.6 Compiler-warning cleanup

Cleanup was performed in two narrowly scoped phases, each documentation/redundancy only.

| Phase | Change | Scope |
| --- | --- | --- |
| **Phase 1** | 30 comment/documentation source-line corrections across 22 files, clearing 31 warning occurrences | Category 1 (`-Wcomment` from `/**` glob text) and Category 2 (`BACKUP_*/` closing a comment early) |
| **Phase 2** | One redundant unsigned lower-bound conjunct removed | `adapters/gba/gba_sramobs.c`, LOW\_FNV range check in `m12c_sramobs_read()` |

The Phase 1 occurrence count (31) exceeds its source-line count (30) because
`adapters/gba/gba_m10map.h` carries two glob patterns on a single line, producing two
diagnostics from one line.

Phase 2 removed only a tautological comparison: the selector base `GBA_SRAMOBS_RD_LOW_FNV`
is `0u` and `sel` is `unsigned int`, so the lower-bound test was true for all 2^32 inputs.
The surviving upper bound preserves the effective range and the array index remains bounded.
The other four selector range checks have nonzero bases and were left untouched.

**Warning cleanup is COMPLETE for the current shipping and verification path.**

**Deliberately deferred — outside that path.** `adapters/gba/gba_m11save.c` and
`gba_m11save.h` retain historical warnings. These files are **M11-only by construction**:
they are not on the M13C link line, do not appear in `build/m13c/m13cgpsp.map`, and are not
among the translation units checked by `make m13c-check`. They therefore participate in
neither the shipping image nor the verification chain, and their cleanup is **not** a
pre-freeze requirement.

### 24.7 Byte-neutrality finding

Both cleanup phases were followed by an explicit `make m13c` regeneration. In both cases the
rebuilt payload was byte-identical to the hardware-passed baseline.

**The shipping PS5 payload did not change from the hardware-passed baseline as a result of
compiler-warning cleanup.** The M15 hardware evidence in Section 23 therefore applies
without qualification to the current freeze candidate.

**Methodological note, recorded because it produced a false result once.** A hash comparison
is valid only against an artifact that has actually been regenerated. An unchanged hash read
from a binary whose timestamp predates the edits proves nothing — it is an identity statement
about an untouched file. Any future byte-identity claim must cite a build timestamp that
advanced past the edit, as §24.4 does.

### 24.8 Gameplay session bounding

This subsection records how a GBA gameplay session actually ends, because the freeze
evidence previously carried a stale description of it. All facts below were read directly
from `apps/m13cgpsp/main.c` in the freeze-candidate tree.

**There is no wall-clock gameplay or session terminator.** Nothing in the frame loop reads
the clock in order to decide whether to exit. `make m13c-verify` enforces this at the source
by failing if `M13C_SESSION_SECONDS`, `run_max_us` or `M13C_END_SESSION` reappears in
`apps/m13cgpsp/main.c`. Wall-clock time is still *measured* for reporting, but it is
compared to nothing and can never end a session.

**The loop is nevertheless finite, bounded by an iteration count rather than by time.**

| Constant | Value | Site |
| --- | --- | --- |
| `M13C_WD_MINUTES` | **240** | `apps/m13cgpsp/main.c:347` |
| `M13C_WD_SECONDS` | **14,400** (240 × 60) | `apps/m13cgpsp/main.c:348` |
| `M13C_WD_FPS_CEIL` | **60** (integer ceiling of 59.7275 Hz) | `apps/m13cgpsp/main.c:349` |
| `M13C_PLAY_MAX_ITERS` | **864,000** (14,400 s × 60 Hz) | `apps/m13cgpsp/main.c:350-351` |

**The value is compile-time pinned.** Three `_Static_assert`s at `apps/m13cgpsp/main.c:356-364`
fix `M13C_WD_SECONDS == 14400u` and `M13C_PLAY_MAX_ITERS == 864000u`, and assert a floor of
ten minutes of gameplay. The arithmetic is therefore checked by the compiler rather than by
the surrounding comment: if any term drifts, the **build fails** instead of the console
quietly shipping a backstop of an undocumented length.

**One gameplay-loop iteration corresponds to one emulated frame.** The bounded loop is
`for (iter = 0; iter < M13C_PLAY_MAX_ITERS; iter++)` at `apps/m13cgpsp/main.c:2761`, inside
`_start()`. `m5_exec_run()` is called exactly once per iteration and `execute_arm()` returns
only on frame wrap, so iterations convert to wall time solely through the emulated frame rate.

**The counter is monotonic and has no reset path during a session.** The only assignment to
`iter` in the file is the `for`-initialiser; the loop body contains no reset, decrement or
reload. It advances strictly upward for the life of the session.

**At normal GBA rates the cap corresponds to roughly 241–243 minutes of real play** —
864,000 / 59.7275 Hz = 14,465.7 s = **241.1 min** at the spec rate, and 864,000 / 59.29 Hz =
14,572.5 s = **242.9 min** at the rate M13B-5 measured on hardware.

**It is an emergency frame-count / spin backstop, not a progress-based stall detector.**
It carries no progress signal, so **healthy gameplay and idle gameplay advance the counter
identically** — the quantity it counts, emulated frames, is exactly the quantity healthy
gameplay produces, and a wedged loop is indistinguishable from a long, happy session. One
limitation follows directly and is recorded deliberately: because the counter advances only
when the loop *iterates*, it bounds a fast-spinning loop but would never fire on a loop
wedged inside a single `m5_exec_run()` call. It is a spin bound, not a hang bound.

**Progress-based hang detection is deferred and is not implemented pre-freeze.** The source
scopes a detector that watches for frames failing to *advance* to M15/M16 and states it is
deliberately not implemented here. That is new executable logic and is out of scope for the
GBA v1 freeze.

**If the limit is reached, the session is graded a watchdog failure and the save is not
committed.** `end_reason` is pre-loaded with `M13C_END_WATCHDOG`, so falling out of the loop
naturally is recorded as a watchdog exit rather than as a completed session. The `clean`
conjunction requires `end_reason == M13C_END_OPERATOR`, so it is false; the run reports
`THIS IS NOT A USER EXIT AND IT IS NOT A PASS`; **nothing is committed and the save on disk
is left untouched**; and the payload exits through a full-screen `WATCHDOG EXIT` screen after
a CIRCLE acknowledgement. It is not a return to the picker and not a silent break.

**Consequence for long sessions.** Uninterrupted play continues normally to ≈241–243 minutes.
Beyond that the session is terminated and graded a failure, and that session's progress is
**not** written to disk. Overnight or all-day idle sessions are therefore not survivable,
since idle frames accumulate at the same rate as active play.

**Why "no session-duration terminator" and "the emergency watchdog still bounds the loop"
are both true.** They describe different axes and are verified by two separate checks in
`m13c-verify`. *Bounded* is not *timed*: a timed exit would fire after N seconds regardless
of whether the emulator made progress, whereas an iteration cap is reached only after 864,000
frames were actually emulated. A stalled console never reaches it; a healthy one reaches it
only after about four hours.

#### 24.8.1 Verification-output defect (corrected)

The `m13c-verify` recipe printed a **stale** human-readable figure that did not describe the
shipping runtime:

| | Before | After |
| --- | --- | --- |
| `Makefile:12439` | `the emergency watchdog still bounds the loop (54,000 iters = 15 min)` | `the emergency watchdog still bounds the loop (864,000 iters = 240 min)` |

The literal `54,000` was a leftover from the pre-Task-7A watchdog, produced by the identical
formula against the old minute count (15 min × 60 s × 60 Hz = 54,000). When `M13C_WD_MINUTES`
was raised from 15 to 240, the constant and its assertions were updated in source but this one
`echo` string was not. That the divergence was isolated to this single line is corroborated by
the sibling gates, which already printed the correct figure: `Makefile:13075` (M14A) and
`Makefile:13768` (M14B) both read `(864,000 iters = 240 min)`. The corrected M13C line is now
character-identical to both.

**This was a verification-output defect only.** The literal `54000`/`54,000` appeared nowhere
in `apps/m13cgpsp/main.c`, in any header, or in the payload's logic — it existed solely inside
a `make` `echo` string. The *check* itself was always correct: it greps for
`iter < M13C_PLAY_MAX_ITERS` and fails if the watchdog no longer bounds the loop, and it never
tested the number it printed. A payload built with a 54,000-iteration watchdog could not link,
because `_Static_assert(M13C_PLAY_MAX_ITERS == 864000u)` would fail the build first.

The correction changed one `echo` string and nothing else. **The shipping runtime watchdog was
preserved unchanged**: `M13C_WD_MINUTES`, `M13C_WD_SECONDS`, `M13C_WD_FPS_CEIL`,
`M13C_PLAY_MAX_ITERS`, every `_Static_assert`, the gameplay loop, the watchdog's behaviour and
the save-commit path are all untouched. Because the edit is confined to a verification
recipe's console output, it cannot affect `build/m13c/m13cgpsp.bin`; the artifact recorded in
§24.4 remains the applicable one. The 2026-09-17 forced regeneration confirmed this directly:
the rebuilt binary reproduced the established SHA-256 byte for byte (§25.2).

## 25. GBA v1 — FROZEN

**GBA v1 is FROZEN.** This section is the formal freeze declaration. It supersedes the
`NOT FROZEN` status previously carried in §24.1 and is **authoritative for freeze state**.

### 25.1 Declaration

| Item | State |
| --- | --- |
| **GBA v1** | **FROZEN** |
| Frozen baseline | **M13C** — `build/m13c/m13cgpsp.bin` |
| Declared | **2026-09-17**, by explicit operator declaration |
| Basis | Final freeze audit **PASSED** — forced regeneration reproduced the established hardware-passed artifact exactly |

**The freeze became authoritative only after the successful 2026-09-17 forced-regeneration
and final-verification audit.** Until that audit ran, the M13C artifact was a *freeze
candidate* only (§24), and no declaration existed. The freeze rests on that audit's outcome
rather than on the artifact's age: the audit forced a regeneration and the rebuild reproduced
the established, hardware-passed SHA-256 byte for byte. Reproducibility of this baseline from
source is therefore a **measured property, not an assumption**.

### 25.2 Frozen artifact identity

```
build/m13c/m13cgpsp.bin
  size        : 308,784 bytes
  SHA-256     : e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a
  regenerated : 2026-09-17 00:36:23.337973700 -0400   (final forced regeneration)
```

This is the same hash recorded in §24.4 for the 2026-09-16 20:50:05 post-Phase-2 build, and
the same hash as the pre-cleanup, hardware-passed M15 baseline (Section 23). Independent
regenerations spanning the entire compiler-warning cleanup produced **identical bytes**, which
is what makes the cleanup provably non-functional with respect to the shipping image.

### 25.3 Final verification evidence

| Gate | Result |
| --- | --- |
| `make m13c-check` | **PASS** |
| M13B equivalence | **283 checks, 0 failures** |
| M12C equivalence | **850 checks, 0 failures** |
| `make m13c-verify` | **`M13C VERIFY OK`** |
| Relocations | **230** `R_X86_64_RELATIVE` |
| Relocation verdict | **safe to apply** |
| Closure | **clean** |
| Genuinely unresolved symbols | **0** |
| Forced regeneration | **reproduced the exact established SHA-256** |
| Compiler-warning cleanup | **complete for the shipping/verification path** |
| Watchdog verification output | **correctly reports 864,000 iterations / 240 min** |

### 25.4 Final resource measurements

| Item | Measured | Limit / budget | Headroom |
| --- | --- | --- | --- |
| `.bss` | **2,011,328 bytes** | 2,097,152 bytes | **85,824 bytes** |
| `__data_start` | **393,216 bytes / 0x60000** | — | — |
| JIT footprint | **393,216 bytes** | 524,288 bytes | **131,072 bytes** |
| Final binary | **308,784 bytes** | — | — |

Both hard constraints that governed the entire port — the `.bss` ceiling and the JIT budget —
are met with headroom in the frozen image.

### 25.5 Watchdog disposition

**The 240-minute emergency watchdog is part of frozen v1 behaviour and was preserved
unchanged.** Its full characterization is §24.8. In summary: there is no wall-clock session
terminator; the gameplay loop is bounded by `M13C_PLAY_MAX_ITERS = 864000` monotonic
iterations (`M13C_WD_MINUTES = 240`), compile-time pinned by `_Static_assert`; one iteration
is one emulated frame; reaching the cap is graded a watchdog failure and **does not commit
that session's save**.

The only change made in this area before the freeze was to a `make` `echo` string
(§24.8.1) — a verification-output correction that does not describe or alter runtime
behaviour and cannot affect the binary, as the reproduced SHA-256 confirms. No runtime
constant, assertion, loop or save path was touched. Progress-based hang detection remains
**deferred** and was deliberately not implemented pre-freeze.

### 25.6 What the freeze means

1. **This freezes the GBA v1 baseline.** The artifact, gate results, resource measurements
   and configuration invariants recorded in §24 and §25 define v1.
2. **Future work must not silently modify this baseline.** Any change reaching the shipping
   image invalidates the recorded SHA-256 and must be declared, not absorbed.
3. **Any future runtime or core change is post-v1 work and must be validated separately.** It
   does not inherit this freeze's evidence; it requires its own gate run, its own regeneration
   and its own hardware validation.
4. **Return-to-LUAp0rt menu is post-freeze work.** It is not implemented in v1.
5. **Netplay is post-freeze work.** It is not implemented in v1.
6. **The existing 240-minute watchdog is frozen v1 behaviour** and is not to be removed,
   raised or converted to a progress detector as part of v1.

### 25.7 Explicitly outside the frozen shipping baseline

| Item | Status |
| --- | --- |
| **Phase 1b** warning cleanup (`gba_m11save.c`) | **Outside** the frozen baseline — the object is deliberately unlinked and absent from `m13c-check` |
| Optional cleanups **O1–O7** | **Outside** the frozen baseline — documentation and non-shipping cleanups only |

Neither was a freeze prerequisite and neither is part of v1. They may be undertaken later as
separately scoped work; if any of it ever reaches the shipping image, it is post-v1 by
rule 3 above and requires independent validation.

### 25.8 Configuration invariants frozen with v1

| Invariant | Value |
| --- | --- |
| ROM picker capacity | **64 ROMs** |
| ROM buffer size | **`ROM_BUFFER_SIZE = 2`** |
| Gameplay watchdog | **864,000 iterations / 240 minutes** |
| Save/restore policy | as recorded in Section 23 |

## 26. GBA v0.0.1 — FROZEN

**LUAp0rt GBA v0.0.1 is FROZEN**, declared **2026-09-22**. Basis: hardware
validation complete across all five save classes, plus controls, picker and session
lifecycle.

This section is a **pointer and declaration only**. The authoritative, self-contained
record is the standalone release document:

```
release/luap0rt-gba-v0.0.1/RELEASE.md
```

### 26.1 Artifact identity

```
build/m16c/m16cgpsp.bin
  size    : 336,032 bytes
  SHA-256 : 16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2
  built   : 2026-09-22 12:09:33

build/m16c/m16cgpsp.lua        (loader — ships WITH the binary)
  size    : 38,801 bytes
  SHA-256 : 1857705345f543fb78ac624211cbcb3a08f3cb48edeef7d80808de1eef87f624
```

**M16C is the sole shipping artifact.** M16-0 and M16-1 are diagnostics
(`Makefile:15190-15203`) and cannot write saves.

### 26.2 Status

| Item | Result |
| --- | --- |
| All five save classes on hardware | **HARDWARE PASS** |
| R7 controls, return-to-picker chord, picker/frontend | **HARDWARE PASS** |
| Gameplay, audio, presentation, demand paging, session lifecycle | **HARDWARE PASS** |
| `make m16c-verify` | **`M16C VERIFY OK`** |
| Host gates | M12C **958/0** · M16C save **1013/0** · R7 **688/0** · M13B **283/0** |

### 26.3 Relationship to v1

**GBA v1 (M13C) remains FROZEN and is NOT superseded.** §25 stands unchanged. v0.0.1
is a separate, additive shipping artifact.

M13C was verified **byte-identical after the entire FLASH64 fix**
(`e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a`), which is the
measured proof that every v0.0.1 edit compiled out of the pre-session-reuse
milestones.

### 26.4 What v0.0.1 changes in this report

**§23.2 is superseded** for **FLASH64** and **EEPROM8K**, which it recorded as
*SOURCE-CHARACTERIZED, NOT HARDWARE-EXERCISED*. Both are now **HARDWARE PASS** —
FLASH64 on Mother 3 and The Sims 2, EEPROM8K on Zelda: A Link to the Past + Four
Swords (`AZLE`). The five-term status vocabulary of §23.1 is retained unchanged, and
§23.3's caveat still holds: region size alone does not discriminate the EEPROM
subtypes, so the **declared** size is authoritative.

The superseding matrix is `release/luap0rt-gba-v0.0.1/evidence/hardware-matrix.md`.

### 26.5 The defect this release closes

Valid ROM-bound LGS1 **FLASH64** saves were rejected `SAVE CORRUPT / CLASS
INCOMPATIBLE` on a fresh launch. Root cause: `adapters/gba/gba_savehdr.c:249-252`,
a class-compatibility gate with no evidence path for a legitimately-`BACKUP_UNKN`
live class at preboot. Fixed by keyed evidence with late activation across five
production files, **every addition behind `#ifdef LUAPORT_SESSION_REUSE`**.
Test-first evidence: **RED 1013/36 → GREEN 1013/0**, tests unmodified.

Full closure record:
`release/luap0rt-gba-v0.0.1/evidence/flash64-closure.md`.

### 26.6 Governing constraint

| Item | Value |
| --- | ---: |
| `__data_load` (`0x4F820`) | 325,664 |
| Ceiling (`0x50000`) | 327,680 |
| **Margin** | **2,016 bytes** |
| Hard floor | 2,000 bytes |
| **Spare** | **16 bytes** |

**There is no v0.0.1 shipping-source edit that is safe by inspection.** Rule 3 of
§25.6 applies unchanged: future work does not inherit this release's evidence and
requires its own gate run, its own regeneration and its own hardware validation.
