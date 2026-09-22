# LUAp0rt GBA

### GAME BOY ADVANCE FOR PLAYSTATION 5

**Created by NBT**

**Version: v0.0.1**

---

## What this is

**LUAp0rt GBA** is a Game Boy Advance emulator integration for the PlayStation 5.

It is built around **LUAp0rt** — a reusable runtime and platform architecture created
by NBT for running native code payloads on the PS5 — combined with the
**gpSP** Game Boy Advance emulator core.

In plain terms: LUAp0rt provides the platform layer (memory, video, audio, input,
file access and session lifecycle), and gpSP provides the GBA emulation. LUAp0rt GBA
is the integration of the two, plus a ROM picker and save system built for the
console.

This release, **v0.0.1**, is the first public version. Everything listed under
[Features](#features) was validated on real PlayStation 5 hardware.

> **A note on expectations.** This is an early release. The features below were
> tested on hardware with specific games, but **no claim is made that any particular
> game works, or that the GBA library is broadly compatible.** Some games will not
> run correctly. That is expected at v0.0.1.

---

## Quick Start

### You need to supply two things yourself

LUAp0rt GBA ships **no games and no console firmware**. Before launching, you must
put your own files on the console:

| What | Where it goes | Notes |
| --- | --- | --- |
| **GBA ROMs** | `/temp0` | Must end in **`.gba`** |
| **GBA BIOS** | `/temp0/gba_bios.bin` | Primary location |
| **GBA BIOS** (alternative) | `/savedata0/bios/gba_bios.bin` | Fallback, checked second |

**The BIOS must be exactly 16,384 bytes.**

The ROM picker lists up to **64 ROMs** from `/temp0`. Files that do not end in
`.gba` are ignored. The extension check is **case-insensitive** — `game.gba`,
`GAME.GBA` and `Game.Gba` are all accepted.

### How to get the files onto the console

Copy your ROMs and your BIOS into the locations above using **whatever compatible
PS5 file-transfer method you already use.**

> **This package does not include a ready-to-use ROM/BIOS upload tool.** The source
> snapshot contains an uploader script (`source-snapshot/tools/upload.py`), but the
> console-side receiver it requires is **not part of this release**, so it will not
> work as a self-contained workflow. See
> [Known limitations](docs/limitations.md). Use your existing file-transfer method
> instead.

### Legal note on the files you supply

**You are responsible for supplying only games and firmware you are legally
entitled to use.** LUAp0rt does not include them, does not link to them, and will
not help you obtain them. See [Legal and content policy](#legal-and-content-policy).

---

## Launching

Two files launch the emulator, and **both are required** — the binary cannot be sent
on its own:

| File | Role |
| --- | --- |
| `binary/m16cgpsp.lua` | Loader script |
| `binary/m16cgpsp.bin` | The payload |

From the root of this release package:

```sh
python source-snapshot/tools/send.py <PS5_IP> binary/m16cgpsp.lua binary/m16cgpsp.bin
```

Replace `<PS5_IP>` with your console's address on your local network.

**Before you send anything,** your compatible PS5 Lua/JIT environment must already be
running and its remote Lua loader must be available and listening. LUAp0rt GBA is the
payload — it does not create that environment, and this document does not describe
how to obtain or set one up. If you do not already have a working loader, this
command will simply fail to connect.

More detail: [`docs/deploy-ps5.md`](docs/deploy-ps5.md).

---

## Controls

| Physical input | GBA key |
| --- | --- |
| **( X )** / **( O )** | **A** |
| **Square** / **Triangle** | **B** |
| **D-pad** | **D-pad** |
| **L1** | **L** |
| **R1** | **R** |
| **OPTIONS** | **START** |
| **TOUCHPAD** | **SELECT** |

Two buttons are offered for each of A and B so you can hold the controller however
feels natural.

### Return to the ROM picker

**Hold `L1` + `R1` + `L2` + `R2`** — all four shoulder buttons together.

This ends the current game and returns you to the ROM picker, so you can choose
another game without relaunching. `L2` and `R2` are intentionally not mapped to any
GBA key, so this chord can never collide with gameplay input.

Full detail, including timing and the input architecture:
[`docs/controls.md`](docs/controls.md).

---

## Saves

**LUAp0rt GBA supports persistent in-game saves.** Your progress is written to the
console and survives returning to the picker and relaunching.

These save types were validated on real hardware:

| Save type |
| --- |
| SRAM |
| FLASH64 |
| FLASH128 |
| EEPROM512 |
| EEPROM8K |

That covers the save hardware used by the great majority of GBA cartridges.

> **This is not a promise that every game saves correctly.** It means each of those
> five save classes was exercised on hardware and passed. Individual games can still
> fail for reasons unrelated to save class.

---

## Features

Validated on PlayStation 5 hardware in v0.0.1:

- **ROM picker** — browse and launch from up to 64 ROMs in `/temp0`
- **GBA gameplay** — emulation via the gpSP core
- **DualSense controls** — full mapping, described above
- **Audio**
- **Framebuffer presentation** — video output to the console display
- **Large-ROM demand paging** — cartridges larger than the resident buffer stream in
  on demand rather than needing to fit in memory at once
- **Persistent saves** — the five save classes listed above
- **Return-to-picker session lifecycle** — end a game and pick another without
  relaunching the payload

### Not in this release

**Netplay is not included in v0.0.1.** No networked or multiplayer functionality
ships in this version.

Other known limitations are documented in
[`docs/limitations.md`](docs/limitations.md).

---

## Legal and content policy

- **LUAp0rt does not include commercial ROMs.**
- **LUAp0rt does not include Nintendo's proprietary GBA BIOS.**
- **LUAp0rt does not include user save data.**
- **No download links for ROMs or proprietary BIOS files are provided here, and none
  will be.**

**Users are responsible for supplying any games or firmware they are legally
entitled to use.**

---

## License and credits

**LUAp0rt — Created by NBT**

```
LUAp0rt
Copyright (C) 2026 NBT

Original LUAp0rt code is licensed under the GNU General Public
License version 2 or, at your option, any later version.
```

**SPDX-License-Identifier: `GPL-2.0-or-later`**

This covers **NBT's original LUAp0rt work only.**

### Third-party software

This project incorporates and derives from software created by other people.
**Those components remain the copyright of their respective authors and are
distributed under their own licenses.** NBT is not the author of gpSP, LuaPSX,
EmuC0re, libretro-common, Luac0re, mGBA, or any other upstream project — and none of
those projects or their developers is the creator of LUAp0rt.

| Document | What it covers |
| --- | --- |
| [`LICENSE`](LICENSE) | Canonical GNU GPL v2 text |
| [`COPYRIGHT`](COPYRIGHT) | LUAp0rt's own copyright and license notice |
| [`CREDITS.md`](CREDITS.md) | Who made what — LUAp0rt and every upstream project |
| [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) | Full provenance and incorporation record |
| [`licenses/`](licenses/) | Verbatim upstream license texts |

---

## What is in this package

| Path | Contents |
| --- | --- |
| `binary/` | The shipping payload, loader, ELF and link map |
| `source-snapshot/` | Complete corresponding source for the shipping binary |
| `docs/` | Controls, deployment, build reproduction, limitations |
| `evidence/` | Hardware validation records, host gate results, manifests |
| `licenses/` | Upstream license texts |
| `SHA256SUMS` | SHA-256 of every file in this package |

To check that your copy is intact:

```sh
sha256sum -c SHA256SUMS
```

Build instructions: [`docs/build-reproduction.md`](docs/build-reproduction.md).
Full release record: [`RELEASE.md`](RELEASE.md).
