# LUAp0rt Launcher

A desktop dashboard for **LUAp0rt GBA v0.0.1** — pick a game folder, check your
BIOS, verify the payload, press **Launch**, and watch the console's own log
stream in. It runs on your PC and talks to the PS5 only through the tools that
already ship with LUAp0rt.

**LUAp0rt — Created by NBT.** Copyright (C) 2026 NBT, GPL-2.0-or-later.
The launcher is part of NBT's original LUAp0rt work; it is *not* part of the
frozen, hardware-validated v0.0.1 payload.

---

## Start it

**Windows, no Python needed:** double-click **`LUAp0rt-Launcher.exe`**. It starts
the local server and opens the dashboard in your default browser. The exe must
stay inside the `launcher/` folder, because it finds `tools/send.py`,
`tools/upload.py` and the payload pairs relative to its own location. It runs the
frozen tools from the project tree, unmodified, using its bundled interpreter.

**Windows with Python installed:** double-click **`LUAp0rt-Launcher.cmd`**.

Anywhere with Python 3:

```bash
python launcher/luap0rt_launcher.py
```

The dashboard opens in your browser at `http://127.0.0.1:8765/`. It binds to
localhost only, so nothing on your network can drive it. Close the terminal
window (or press Ctrl+C) to quit.

Options: `--port N` (UI port), `--no-browser` (don't open a tab), `--verbose`
(log HTTP requests), `--no-window`, and for testing `--config FILE` and
`--log-port N`. The exe accepts the same options.

While it runs, a small **status window** with a taskbar button shows the
console status, the dashboard address and the latest console-log lines, with
**Open dashboard**, **Check console** and **Quit launcher** buttons. Closing that
window with X warns that the launcher is still running in the tray and asks:
**Yes** quits everything, **No** closes only the window (the tray keeps the
server alive; use the tray's *Show status window* to bring it back), **Cancel**
keeps the window. Start with `--no-window` for tray-and-dashboard only.

A **tray icon** (the mascot with a status dot) also sits in the Windows
notification area. Its tooltip and menu show the console status; the
menu offers **Open dashboard**, **Show status window**, **Check console now** and
**Quit launcher**. Windows 11 places new tray icons behind the `^` overflow
chevron by default; drag it onto the taskbar once to keep it visible.
Double-clicking the icon opens the dashboard. Starting the exe a second time
does not start a second server: it opens the running dashboard and exits.

To stop the launcher use the tray menu, the **Quit** button in the top bar, or
Ctrl+C in the terminal when started from the `.py`. Quitting only stops the
dashboard; a payload already running on the console is unaffected. When the
server goes away the dashboard tab notices within a couple of seconds and
closes itself; browsers only allow that for a tab opened directly to the page
(which is how the launcher opens it), so if yours refuses, the tab shows a
"Launcher stopped" screen instead.

**Launch checks.** Pressing **Launch LUAp0rt GBA** while any checklist item is
red does not send anything. A dialog lists the four checks with their current
state; each red one has a **Fix this** button that opens the relevant setup
step (address, payload choice, arming the loader, or files on the console).
**Launch anyway** overrides the checks, since they can be stale.

**Your settings persist.** PS5 address, ROM folder, BIOS path, payload choice
and the record of what was sent are saved on every change to
`Documents\LUAp0rt Launcher\launcher_config.json` (Linux/macOS: the usual
per-user config directory), alongside `launcher.log`. Documents is used rather
than AppData because Windows gives processes started from inside a packaged
app a private, virtualized copy of AppData, which would split the settings. Nothing is copied into the project tree, and
the BIOS itself is never duplicated, only its path is remembered.

**Console status is kept fresh automatically.** The launcher pings the console
every 15 seconds while the payload is not logging, so the green mark in the tile,
the top-bar pill and the tray icon reflect reachability without pressing Ping.
Ping is ICMP only; the loader port is never touched.

When run from the `.py`, no third-party packages are required. The tray icon
needs `pystray` and `Pillow` (`pip install pystray pillow`) and is skipped
without them; the exe bundles both. The native **Browse…** buttons use Tkinter,
which ships with the standard Windows Python installer; without it you can still
type paths by hand.

---

## First run: the guided setup

The first time the dashboard opens (or whenever you press **Setup** in the top
bar) a six-step guide walks through everything in order:

1. **Console address**: enter the PS5's IP and **Test** it (ping).
2. **GBA BIOS**: browse to your `gba_bios.bin`; it is verified immediately
   (exactly 16,384 bytes, FNV-1a shown). Any region's BIOS is accepted: the
   size is the console's own requirement and no hash is enforced. Next is
   disabled until it passes, with a skip link if the BIOS is already on the
   console. Nothing is uploaded at this step; the BIOS goes to the console
   with the first launch if needed.
3. **ROM folder**: browse to your `.gba` folder; the count found is shown.
4. **Arm the loader**: on the console run the Luac0re listener (launch the
   host game, then OPTIONS, then HALL OF FAME), then press **Check the
   loader**. The probe result is shown, and Next unlocks when the loader
   answers on port 9026.
5. **Games to include**: nothing is uploaded here. **Select all games for
   upload at launch** checks every game in the library; **Pick games myself**
   unchecks them all so you tick the ones you want. Either way the checked
   games (and the BIOS) are sent when you press Launch, skipping what is
   already on the console.
6. **Ready**: press **Launch LUAp0rt GBA** on the right to run the emulator.

Values already set are pre-filled, so rerunning the guide is quick. Skipping
or finishing marks setup as done in the settings file.

## What the dashboard does

| Panel | What it shows / does |
| --- | --- |
| **Status tiles** | Console reachability, payload integrity, BIOS size gate, ROM count vs. the 64-entry picker limit |
| **Game library** | Your local `.gba` folder (case-insensitive extension, same rule as the console), internal title and game code from each ROM header, a checkbox per game for what to include with a launch, and the on-console status |
| **GBA BIOS** | Checks the file is exactly **16,384 bytes** and shows its FNV-1a hash (the same hash the console prints, so you can compare); the BIOS goes to the console with the next launch if it is not there yet |
| **Launch** | Chooses the `m16cgpsp.lua` + `m16cgpsp.bin` pair, verifies the `.bin` SHA-256 against the frozen v0.0.1 golden value, and runs `tools/send.py` |
| **Console log** | Live view of the payload's operator log (UDP 9027), with filter, follow, clear and save; collapsed by default, it opens when a job starts or when you press the ▸ toggle |
| **Tool output** | The exact command that was run and everything it printed |

### Launching

**Adding games.** Drag and drop `.gba` files anywhere on the library and they
are copied into your ROM folder (a file that is already there asks before it
is replaced; anything that is not `.gba` is refused). Or use the **Open
folder** and **Rescan** buttons in that same note: put the files in yourself,
then rescan. **Change ROMs folder…**
points the library at a different folder; it asks first, because the list is
replaced by that folder's contents (nothing is deleted).

**The library is a checklist.** Tick the games to include (all are ticked to
begin with; **Check all** / **Uncheck all** and the header checkbox switch the
whole list). When you press **Launch**, the checked games are sent to the
console first with the progress window showing, and the window closes by
itself the moment the last game is in and the payload starts, switching to the
console log so you can watch it boot; the button reads "Send N games + Launch"
when there is something to send. A game
is skipped automatically if this launcher already sent that exact file (same
size and modification time) or the console reported it present with the same
size in this session, so nothing is uploaded twice. Unchecked games are left
alone. The verified BIOS follows the same rule and goes first, so there is no
separate BIOS upload button either, and no send button of any kind: Launch is
the only way files reach the console. There is no per-game send button: the checklist and the skip rule replace it, and if the
console's report says a game is missing or has a different size, it is sent
again at the next launch regardless of what this launcher recorded. The library
card states this rule in a note, and the launch card shows "N of M games
checked · K will be sent to the console first" before you press the button.

1. Enter the console's IP in the top bar. **Ping** checks reachability with ICMP;
   the launcher also pings on its own every 15 s.
2. Make sure your own PS5 Lua/JIT environment is running with its remote Lua
   loader listening. The launcher cannot create that for you, and this
   documentation does not describe how to set it up. The checklist row
   **"Lua loader listening on TCP 9026"** shows whether something answers on the
   loader port: the **loader probe** opens a TCP connection, sends **no bytes**,
   and closes it with a reset. While nothing answers it retries every 20 s; once
   the loader answers it is left alone for 5 minutes. It never runs while a
   script is being sent, for 90 s afterwards, or while the payload is logging.
   **Probe** checks once on demand; **auto-probe** switches the background check
   off if your loader reacts badly to an empty connection (for example by
   showing an error or by no longer accepting the next script). Whether a given
   loader tolerates this could not be verified here; the first time you arm the
   loader, watch that the row turns green and that the launch still goes
   through. Once a send succeeds, or while the payload is logging, the row
   stays green on that evidence alone: the loader has proven itself, so no
   further probing happens until the next session.

   **While the emulator runs, the loader is not listening.** That is normal:
   the loader handed control to the payload. The launcher follows the
   emulator's own log to know where it is (booting, at the ROM picker,
   playing a session, or exited via `Done. status=` / `verdict:`), and the
   Launch dialog, the checklist and setup step 4 word their advice
   accordingly: either keep playing, or exit the emulator (hold
   L1 + R1 + L2 + R2 to return to the picker, then CIRCLE) and re-arm the
   loader (Star Wars Racer Revenge, OPTIONS, HALL OF FAME) before launching
   again. After an exit report the loader port is probed again right away.
   If the launcher was not running when the emulator started, it cannot
   know, and the "not listening" text offers both possibilities. If the console stops
   answering ping, that evidence is discarded: the row reports "console not
   answering ping, loader status unknown" and is probed afresh once ping
   returns.
3. Press **Launch LUAp0rt GBA**. This runs, unchanged:

   ```
   python tools/send.py <PS5_IP> <m16cgpsp.lua> <m16cgpsp.bin>
   ```

   The script goes to TCP 9026, the blob to the first ACKing port in 9028–9045.
   The UDP log listener is already running before the send, so the bring-up
   lines are captured.

### Payload integrity gate

The launcher refuses to send a `.bin` whose SHA-256 does not equal the frozen
release value

```
16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2
```

unless you tick **Allow an unverified payload**. That switch exists for
development builds only. Both the release copy (`release/luap0rt-gba-v0.0.1/binary/`)
and the build copy (`build/m16c/`) are detected automatically; a custom pair can
be set in `launcher_config.json`.

### Getting ROMs and the BIOS onto the console

The console reads ROMs from **`/temp0`** and the BIOS from
**`/temp0/gba_bios.bin`** (fallback `/savedata0/bios/gba_bios.bin`).

- **In the project tree**, where `tools/upload.py` and its receiver
  `lua/upload.lua` both exist, the **Send** / **Upload BIOS** buttons run
  `tools/upload.py` for you. Every send replaces the console copy from scratch;
  the uploader's resume mode exists for multi-gigabyte disc images and is not
  offered here, since a GBA ROM finishes in seconds. **Send checked** uploads
  the checked games that are not on the console yet, one after another, in the
  picker's order, up to the 64-entry limit; **Launch** does the same and then
  starts the payload. The batch stops at the first failure and reports how
  many were sent.
  **Uploads only run while the loader is listening.** Every send (a game,
  Resume, Send all, the BIOS, or the setup guide's send step) first checks the
  loader port. If the emulator is running, the launcher refuses and explains:
  quit the emulator (hold L1 + R1 + L2 + R2 to return to the picker, then
  CIRCLE), re-arm the loader (Star Wars Racer Revenge, OPTIONS, HALL OF FAME),
  and send again. If nothing answers on 9026, or the console does not answer
  ping, it refuses likewise, with a **Check again** button in the dialog.
  Every upload (single game, BIOS or Send all) opens a **progress window** in
  the dashboard: an overall bar, then one row per file with bytes sent, rate
  and percent, taken from the uploader's own output. **Cancel** stops the
  current transfer and marks the rest as not sent; **Show tool output** jumps
  to the raw command output. Reloading the page while an upload runs reopens
  the window. Every script sent to the loader
  consumes JIT mappings, so relaunching the host environment between uploads
  and the payload send is recommended, as `lua/upload.lua` notes.
- **In the release package**, the receiver is not included, so the launcher
  shows the buttons as unavailable and you copy files with whatever compatible
  PS5 file-transfer method you already use.

### Verifying what is actually on the console

The console cannot be listed remotely, but it does not need to be: every time
the payload starts it scans `/temp0` and prints the result to its operator log
(one `NAME`/`PATH` pair per file, then an `ENTRY` line with the exact size and
whether the picker accepts it, then the BIOS candidates). The launcher parses
that scan from the UDP log and marks each game in the library:

| Badge | Meaning |
| --- | --- |
| **✓ verified** | listed by the console's own report, size identical to your local file |
| **▲ verified: different size** | present, but the console copy's size differs (partial or old upload) |
| **▲ verified: EMPTY / TOO BIG / …** | present, but the picker rejects it, with the console's reason |
| **✕ verified: not on console** | absent from the report |
| **○ will verify** | no report yet in this session, or sent after the last report; verified at the next launch (the tooltip notes when this launcher sent it) |

A note under the table gives the scan time, the counts, the BIOS result, and
any ROMs that are on the console but not in your folder. **Nothing is
assumed.** The report lives only in memory for the session in which the
console gave it, it is discarded the moment a new launch is sent, and it is
never restored from an earlier session: a fresh launcher start shows every game
as "not verified" until the emulator boots and reports again.

---

## What it never does

- It does not modify, rebuild, or regenerate any LUAp0rt source or binary.
- It does not include, link to, or help obtain any ROM or BIOS. **Supply only
  games and firmware you are legally entitled to use.**
- It does not accept connections from other machines.
- It does not bypass the integrity gate silently.
- It never sends anything to the loader port except the real script, via
  `tools/send.py` or `tools/upload.py`. The probe connects and resets only.

---

## Files

```
launcher/
  luap0rt_launcher.py     backend: HTTP API on 127.0.0.1, UDP 9027 listener, job runner
  LUAp0rt-Launcher.exe    standalone Windows build of the above (PyInstaller, no Python needed)
  LUAp0rt-Launcher.cmd    Windows double-click starter for the .py
  build_exe.cmd           rebuilds the exe (python.org Python + PyInstaller, pystray, Pillow)
  ui/index.html           the dashboard
  ui/app.css
  ui/app.js
  ui/mascot.png           the picker's dog, from assets/dogpreview_rgba.png
  ui/mascot.ico           the same, as the exe's icon
```

Settings live in `Documents\LUAp0rt Launcher\launcher_config.json`, not in
this folder. Older files (`%LOCALAPPDATA%\LUAp0rt Launcher\` from v0.3-0.6, or
`launcher/launcher_config.json` from v0.1-0.2) are migrated automatically on
first start and can then be deleted.
