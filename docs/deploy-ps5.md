# PS5 deployment — LUAp0rt GBA v0.0.1

> Deployment is **not authorised by the existence of a target**
> (`Makefile:16637`). This document describes how delivery works; it does not
> instruct anyone to perform it.

---

## 0. The launcher (recommended since v0.1.0)

`launcher/LUAp0rt-Launcher.exe` performs everything below for you: it verifies the
payload pair against the frozen SHA-256, checks that the loader is listening, sends
the BIOS and the checked games with the unmodified `upload.py`, and then sends the
payload with the unmodified `send.py`, showing the console's own log as it boots.
See `launcher/README.md`. The rest of this document describes what the launcher
does underneath, for anyone who prefers the command line.

## 1. What ships

A **pair** of files. The console cannot be fed the binary alone.

| File | Size | Role |
| --- | ---: | --- |
| `binary/m16cgpsp.lua` | 38,801 | Loader script — carries the JIT reservation |
| `binary/m16cgpsp.bin` | 336,032 | The payload blob |

## 2. The `make` path

```sh
make m16c-send PS5_IP=<console address>
```

- Errors out if `PS5_IP` is unset (`Makefile:16657-16659`).
- Depends on **`m16c-verify`** *and* **`m16c-lua`** (`Makefile:16656`), so
  **nothing can ship that has not passed the gates** and the loader is always
  regenerated from the binary it will accompany.

## 3. The manual path

If `make` is unavailable, the underlying invocation is:

**From the project tree** (paths as they exist in the development checkout):

```sh
python tools/send.py <PS5_IP> build/m16c/m16cgpsp.lua build/m16c/m16cgpsp.bin
```

**From the root of this release package:**

```sh
python source-snapshot/tools/send.py <PS5_IP> binary/m16cgpsp.lua binary/m16cgpsp.bin
```

> **Path note.** In this package the sender lives at
> **`source-snapshot/tools/send.py`**, not `tools/send.py` — there is no `tools/`
> directory at the release root. The shipping artifacts live in `binary/`, not
> `build/m16c/`. Both paths differ from the project-tree form above; use the release
> form when working from the distributed package.

The transport itself is unchanged — this is the same script invoked the same way,
only with the paths the package actually uses.

**This bypasses the verify gate.** Only use it with artifacts whose SHA-256 you have
checked against `SHA256SUMS`.

## 4. Transport

Two stages, from `tools/send.py`:

| Stage | Port | Notes |
| --- | --- | --- |
| Script | **TCP 9026** (`PAYLOAD_PORT`) | The `.lua` is sent here first |
| Blob | **TCP 9028–9045** (`BLOB_PORT_LO`/`BLOB_PORT_HI`) | The `.bin` goes to the first ACK-responding port in the range |

The blob port is a **range rather than a fixed port** because the running script
binds a port in that range and waits; a previous script that died can leave its
listener behind, so the sender probes for the one that is actually alive. The live
receiver is identified by the **`K` ACK byte**.

**UDP 9027 is the log port.** It does not clash with the TCP ports above — the two
are different protocols and different sockets.

## 5. Operational notes

| Item | Value |
| --- | --- |
| ROM location on console | **`/temp0`** |
| Picker capacity | **64 ROMs** |
| After a session ends | The launcher returns to **its own ROM picker**, not to the system menu |

### 5.1 Confirming the payload did not restart

Frame identifiers in the operator log climb **monotonically across session
boundaries**. A payload restart resets them. This is the observable that
distinguishes "returned to the picker" from "relaunched from scratch", and it was
used during hardware validation to confirm the session boundary holds.

## 6. Ending a session

Hold **all four shoulder buttons** (`L1 + R1 + L2 + R2`) for **15 frames**. See
`docs/controls.md` §3.

Returning to the picker **does not authorise committing that session's save**
(`apps/m16cgpsp/main.c:85-87`).
