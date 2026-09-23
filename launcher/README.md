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

The Browse buttons open the native Windows file and folder pickers; they open
in front of every other window.

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
are copied into your ROM folder (if none is set yet, `Documents\LUAp0rt
Launcher\ROMs` is created and used) and **sent to the console right away**, with
the usual loader check and progress window (a file that is already there asks
before it is replaced; anything that is not `.gba` is refused). Or use the **Open
folder** and **Rescan** buttons in that same note: put the files in yourself,
then rescan. **Change ROMs folder…**
points the library at a different folder; it asks first, because the list is
replaced by that folder's contents (nothing is deleted).

**The library is a checklist.** Select the games to include (all are selected
to begin with; the box in the table header selects or deselects every row,
including the grey console-only ones). When you press **Launch**, the checked games are sent to the
console first with the progress window showing, and the window closes by
itself the moment the last game is in and the payload starts, switching to the
console log so you can watch it boot; the button reads "Send N games + Launch"
when there is something to send. A game
is skipped automatically if this launcher already sent that exact file (same
size and modification time) or the console reported it present with the same
size in this session, so nothing is uploaded twice. Unchecked games are left
alone. The verified BIOS follows the same rule and goes first, so there is no
separate BIOS upload button. **Send selected ROMs** sends the selected games
that are not on the console yet without launching (games only: the BIOS is sent by Launch, and only when the console
does not have it), useful when loading up a console you will play later or
another console. "Already sent" is remembered **per console address**: change
the PS5 IP to a second console and everything counts as not sent there, while
the first console's record is kept for when you switch back. Changing the
address also clears everything the launcher knew about the previous console
(its report, the loader check, the emulator state). If the button shows no
count, everything checked is recorded as already on that console; clicking it
then offers to send it all again anyway, for a console that was wiped. There is no per-game send button: the checklist and the skip rule replace it, and if the
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
   **Probe** checks once on demand. **auto-probe is off by default**: one
   console was seen refusing every script (accepting the connection, then
   dropping it) after such empty connections had been made to its loader
   port, so the launcher no longer touches that port before a send. Turn
   auto-probe on only if your loader is known to tolerate it. If a console does
   get into that state, close the host game, relaunch it and re-arm the loader. Once a send succeeds, the row
   stays green on that evidence alone: the loader has proven itself, so no
   further probing happens until the next session.

   **While the emulator runs, the loader is not listening**, so the row is
   red with "the emulator is running ... exit it and re-arm the loader". That
   is normal: the loader handed control to the payload. The launcher follows the
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

- Since launcher 2.0.0 every send goes through the launcher's **batch
  receiver**: one launcher-owned Lua script (`BATCH_LUA` in
  `luap0rt_launcher.py`, the same socket and write loops as `lua/upload.lua`)
  is sent to the loader once per batch, and every file then travels over a
  single connection: a small header per file (path and size), the bytes, an
  acknowledgement when the console has written and closed the file. Every
  file is written from scratch. This matters because **every script sent to
  the loader consumes JIT mappings in the host game**: with one script per
  game (plus a cleanup before each) a long batch could crash the host game;
  with one script per batch it cannot. The receiver also posts a PS5
  notification per file, "LUAp0rt GBA / Receiving <name>", so the console
  shows which game is arriving instead of the loader's generic message. The
  frozen `tools/upload.py` and `lua/upload.lua` are untouched and still work
  from the command line. **Send selected ROMs** sends the selected games that
  are not on the console yet, in the picker's order, up to the 64-entry
  limit; **Launch** does the same and then starts the payload. The batch
  stops at the first failure and reports how many were sent.
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

### If sends get slow

Every upload runs the console-side receiver `lua/upload.lua`, which binds the
next free port in 9028-9045. On some consoles the listener stays bound after
the receiver returns, and `tools/upload.py` then waits up to 10 s on each
leaked port for an acknowledgement before it finds the live receiver: file N
of a batch waits about 10 x (N-1) seconds. Since launcher 1.8.0 the launcher
closes those leftovers itself before every batch it sends: a small script
through the loader closes every socket in the loader whose local port is in
that range and nothing else (the loader's own port 9026, the log socket and
any game socket are outside it). The tool output says "closed N leaked upload
listeners on the console first" when it had to. Switch it off with
`"clear_leaks": false` in the config if a console misbehaves with it. If the
loader has no `getsockname`, the launcher says so and the old remedy applies:
relaunch the host game and re-arm the loader.

### If a send fails with "loader did not take the script"

The loader is answering on its port but drops every connection as soon as data
arrives; the uploader's own notes call this a wedged loader, and retrying does
not help. The launcher stops after three such resets and says so. On the
console: close the host game, launch it again, re-arm the loader, then send
again.

### Verifying what is actually on the console

Two things tell the launcher what is in `/temp0`, and both come from the
console itself:

1. **A listing through the loader (Rescan, and after every send).** While
   the Lua loader is armed, the launcher sends it a small read-only script of
   its own (`VERIFY_LUA` in `luap0rt_launcher.py`, built from the same
   environment calls as `lua/upload.lua` and the directory walk of
   `LuaPSX/tools/temp0.py`). The script lists `/temp0` with `getdents`, opens
   each entry with `O_RDONLY` only (never `O_CREAT`, so nothing is created) to
   read its size, reports everything on the UDP log port, and returns. It binds
   no listener, so nothing can leak. If the loader has no `getdents`, it checks
   your folder's names one by one instead. **Rescan** re-reads your folder and
   then runs this listing; the launcher also runs it by itself after every
   successful send, so a game you just dropped shows **verified** within a few
   seconds. Games the console has that are not in your folder appear as grey
   rows ("on console only"), so the list is populated even with no folder
   selected. Changing the PS5 address drops the old console's report and lists
   the new console, so the list repopulates for it.
2. **The payload's own boot report.** Every time the payload starts it scans
   `/temp0` and prints the result to its operator log (one `NAME`/`PATH` pair
   per file, then an `ENTRY` line with the exact size and whether the picker
   accepts it, then the BIOS candidates). That report replaces the check.

Both are parsed from the UDP log and mark each game in the library:

| Badge | Meaning |
| --- | --- |
| **✓ verified** | reported by the console, size identical to your local file |
| **▲ verified: different size** | present, but the console copy's size differs (partial or old upload) |
| **▲ verified: EMPTY / TOO BIG / …** | present, but the picker rejects it (from a boot report), or a zero-byte file (from a check) |
| **✕ verified: not on console** | the console did not have it |
| **○ will verify** | nothing from the console yet in this session, or sent after it last answered; press Rescan with the loader armed, or launch |

A note under the table gives the time and kind of the last answer, the
counts, the BIOS result, and anything else in `/temp0` that the picker ignores
(folders, other files). **Nothing is assumed.** The console's answer lives only
in memory for the session in which it was given, it is discarded the moment a
new launch is sent or the address changes, and it is never restored from an
earlier session.

The check can only run while the loader is listening: if the emulator is
running, or nothing answers on TCP 9026, Rescan still re-reads the folder and
says why the console was not asked.

### Deleting games from the console

**Clear selected ROMs** (red, in the toolbar, shown once the console has
answered) removes files from `/temp0`. It acts on the selected games the
console reported present, plus any grey console-only rows you select. It always
asks first and names every file. The deletion is one more launcher-owned
script through the loader (`DELETE_LUA`: `unlink` on each exact path, the
call `LuaPSX/tools/temp0.py --rm` uses), never anything but `.gba` paths under
the ROM directory. Afterwards the launcher forgets its upload record for those
files, **deselects them** so the next launch does not send them back, and lists
the console again. Your PC copies are never touched.

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
