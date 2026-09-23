#!/usr/bin/env python3
"""LUAp0rt Launcher -- a desktop dashboard that wraps the shipping delivery tools.

    python launcher/luap0rt_launcher.py              # opens the dashboard in your browser
    python launcher/luap0rt_launcher.py --port 8765  # choose the local UI port
    python launcher/luap0rt_launcher.py --no-browser # just start the server

WHAT THIS IS
    A PC-side wrapper UI. It does NOT modify, rebuild or replace any part of the
    frozen LUAp0rt GBA v0.0.1 implementation. Every action it performs is one of
    the existing, hardware-validated tools invoked exactly as documented:

        tools/send.py    <PS5_IP> <m16cgpsp.lua> <m16cgpsp.bin>     (launch)
        tools/upload.py  <PS5_IP> <local file> <remote path> [...]  (ROM / BIOS upload,
                                                                    only when the receiver
                                                                    script lua/upload.lua
                                                                    is present)

    plus a passive listener on UDP 9027, which is where the payload broadcasts
    its operator log. The listener is started before any send so the bring-up
    lines are not lost.

WHAT IT NEVER DOES
    - Reachability is checked with ICMP ping. The optional loader probe (on by
      default, switchable in the checklist) opens a TCP connection to 9026 and
      closes it with a reset without ever sending a byte, so the loader is
      never handed a script by the probe. It is paused while a script is being
      sent and while the payload is logging.
    - It never ships, links to, or embeds a ROM or a BIOS. Files come from
      folders the user points it at.
    - It never bypasses the payload integrity gate silently: the shipping
      binary's SHA-256 is compared to the frozen golden value before every
      launch, and an unverified payload is refused unless the user has
      explicitly enabled "allow unverified payload" for development builds.

The HTTP server binds 127.0.0.1 only. Nothing on the network can drive it.

LUAp0rt -- Created by NBT. Copyright (C) 2026 NBT. GPL-2.0-or-later.
"""

import argparse
import hashlib
import re
import json
import os
import platform
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

# --------------------------------------------------------------------------
# Frozen facts (verified against the shipping source, see CLAUDE.md §4 / §7)
# --------------------------------------------------------------------------
GOLDEN_M16C_BIN_SHA256 = (
    "16580c9060a9b172732c5781f467a6006963620813ef0588ad04dc7e801efaf2")
BIOS_SIZE = 16384                 # adapters/gba/gba_bios.h  M3_BIOS_SIZE
ROM_PICKER_CAPACITY = 64          # adapters/gba/gba_library.h  GBA_LIB_MAX_ENTRIES
ROM_EXT = ".gba"                  # adapters/gba/gba_library.c  case-insensitive
CONSOLE_ROM_DIR = "/temp0"        # apps/m16cgpsp/main.c  M16C_ROM_DIR
CONSOLE_BIOS_PATH = "/temp0/gba_bios.bin"
CONSOLE_BIOS_FALLBACK = "/savedata0/bios/gba_bios.bin"

LOG_PORT = 9027                   # UDP, payload -> subnet broadcast
LOADER_PORT = 9026                # TCP, never probed by this tool
BLOB_PORT_LO, BLOB_PORT_HI = 9028, 9045

APP_NAME = "LUAp0rt Launcher"
APP_VERSION = "1.0.0"

FROZEN = bool(getattr(sys, "frozen", False))          # running as a PyInstaller exe
if FROZEN:
    HERE = os.path.dirname(os.path.abspath(sys.executable))
    UI_DIR = os.path.join(getattr(sys, "_MEIPASS", HERE), "ui")
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
    UI_DIR = os.path.join(HERE, "ui")
ROOT = os.path.dirname(HERE)


def user_config_dir():
    """Per-user, outside the project tree, and outside AppData.

    AppData/Local is virtualized for processes started from inside a packaged
    (MSIX) application on Windows: such a process silently reads and writes a
    private copy, so two launchers started different ways would see two
    different settings files. The Documents folder is never virtualized."""
    home = os.path.expanduser("~")
    if platform.system().lower().startswith("win"):
        docs = os.path.join(home, "Documents")
        return os.path.join(docs if os.path.isdir(docs) else home, "LUAp0rt Launcher")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/LUAp0rt Launcher")
    return os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"),
                        "luap0rt-launcher")


CONFIG_PATH = os.path.join(user_config_dir(), "launcher_config.json")
LOG_FILE = os.path.join(user_config_dir(), "launcher.log")
CONFIG_OVERRIDDEN = False          # set by --config
# Older locations, read once and migrated if the current file does not exist.
LEGACY_CONFIG_PATHS = [
    os.path.join(os.environ.get("LOCALAPPDATA") or "", "LUAp0rt Launcher", "launcher_config.json"),  # v0.3-0.6
    os.path.join(HERE, "launcher_config.json"),                                                        # v0.1-0.2
]
AUTO_PING_INTERVAL = 15.0          # seconds between background reachability checks
LOADER_PROBE_INTERVAL = 20.0       # retry interval while the loader is NOT answering
LOADER_PROBE_RECHECK = 300.0       # once it answers, re-touch it only this rarely
LOADER_PROBE_HOLDOFF = 90.0        # no probes for this long after a script was sent


def say(msg):
    """print() that survives a windowed (no-console) exe, where stdout may be None.
    Everything also goes to launcher.log next to the config file."""
    try:
        if sys.stdout:
            sys.stdout.write(msg + "\n")
            sys.stdout.flush()
    except (OSError, ValueError):
        pass
    try:
        os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S  ") + msg + "\n")
    except OSError:
        pass


def tool_argv(script, args):
    """How to run one of the frozen Python tools as a child process.

    From source: the same interpreter, unbuffered. From the exe: the exe itself
    in --run-tool mode, which executes the script with the bundled interpreter,
    so a user of the exe does not need Python installed at all."""
    if FROZEN:
        return [sys.executable, "--run-tool", script] + list(args)
    return [sys.executable, "-u", script] + list(args)

# Where the frozen tools and payload pairs may live, relative to ROOT.
# The launcher works both inside the development checkout and when dropped
# next to an extracted release package.
SEND_CANDIDATES = [
    ("tools/send.py", "project tree"),
    ("launcher/tools/send.py", "release package (copy shipped with the launcher)"),
    ("source-snapshot/tools/send.py", "release package"),
    ("release/luap0rt-gba-v0.0.1/source-snapshot/tools/send.py",
     "release package (inside project tree)"),
]
UPLOAD_CANDIDATES = [
    ("tools/upload.py", "lua/upload.lua"),                      # development checkout
    ("launcher/tools/upload.py", "launcher/lua/upload.lua"),    # release package: copies shipped with the launcher
]
PAYLOAD_CANDIDATES = [
    ("release", "Release package v0.0.1",
     "release/luap0rt-gba-v0.0.1/binary"),
    ("release-root", "Release package (this folder)", "binary"),
    ("build", "Development build (build/m16c)", "build/m16c"),
]


class Blocked(ValueError):
    """A refused action with a code the UI can act on."""

    def __init__(self, code, msg):
        ValueError.__init__(self, msg)
        self.code = code


REARM_TEXT = ("exit the emulator (hold L1 + R1 + L2 + R2 to return to the ROM picker, then press "
              "CIRCLE), then re-arm the loader (Star Wars Racer Revenge > OPTIONS > HALL OF FAME)")


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------
def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def fnv1a32(data):
    """FNV-1a 32 -- byte-identical to adapters/gba/gba_bios.c:bios_fnv1a."""
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def human_size(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return "%d %s" % (n, unit) if unit == "B" else "%.1f %s" % (n, unit)
        n /= 1024.0
    return "%d B" % n


def valid_ipv4(s):
    parts = s.strip().split(".")
    if len(parts) != 4:
        return False
    try:
        return all(0 <= int(p) <= 255 and p == str(int(p)) for p in parts)
    except ValueError:
        return False


def rel(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


# --------------------------------------------------------------------------
# Config
# --------------------------------------------------------------------------
DEFAULT_CONFIG = {
    "ps5_ip": "",
    "rom_dir": "",
    "bios_path": "",
    "payload": "",              # candidate key, or "custom"
    "custom_lua": "",
    "custom_bin": "",
    "allow_unverified": False,
    "probe_loader": True,       # TCP connect (no bytes, RST close) to 9026 to see if the loader is armed
    "uploads": {},
    # NOTE: the console's /temp0 scan is deliberately NOT stored here. It is
    # only ever known from the payload's boot log, so it lives in memory for
    # the session in which it was seen and is cleared on every new launch.
    "setup_done": False,        # the first-run wizard was completed or skipped
    "deselected": [],           # ROM file names NOT included with a launch (everything else is)
    "payload_state": {},        # running / exited, as told by the payload's own log              # remote path -> {size, sha256, mtime, when, name}
}


class Config:
    def __init__(self, path, migrate=True):
        self.path = path
        self.migrate = migrate
        self.lock = threading.Lock()
        self.data = dict(DEFAULT_CONFIG)
        self.load()

    def load(self):
        src = self.path
        if not os.path.isfile(src) and self.migrate:
            for legacy in LEGACY_CONFIG_PATHS:
                if legacy and os.path.isfile(legacy):
                    src = legacy              # migrate from an older location
                    break
        try:
            with open(src, "r", encoding="utf-8-sig") as f:   # tolerate a BOM
                stored = json.load(f)
            if isinstance(stored, dict):
                for k in DEFAULT_CONFIG:
                    if k in stored:
                        self.data[k] = stored[k]
                # a v0.7-0.11 file may carry a saved scan: it is stale by definition
                if "console_scan" in stored:
                    try:
                        self.save()
                    except OSError:
                        pass
        except (OSError, ValueError):
            return
        if src != self.path:
            try:
                self.save()
            except OSError:
                pass

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2, sort_keys=True)
        os.replace(tmp, self.path)

    def update(self, patch):
        with self.lock:
            for k, v in patch.items():
                if k in DEFAULT_CONFIG and k != "uploads":
                    self.data[k] = v
            self.save()

    def set_payload_state(self, st):
        with self.lock:
            self.data["payload_state"] = st
            self.save()

    def record_upload(self, remote, info):
        with self.lock:
            self.data["uploads"][remote] = info
            self.save()

    def forget_upload(self, remote):
        with self.lock:
            self.data["uploads"].pop(remote, None)
            self.save()

    def snapshot(self):
        with self.lock:
            return json.loads(json.dumps(self.data))


# --------------------------------------------------------------------------
# UDP operator log listener (payload -> broadcast :9027)
# --------------------------------------------------------------------------
class LogBuffer:
    MAX = 8000

    def __init__(self):
        self.lock = threading.Lock()
        self.lines = []           # list of (index, t, text)
        self.next_index = 0
        self.last_rx = 0.0
        self.last_from = ""
        self.bind_error = ""
        self.total = 0

    on_line = None            # optional callback fed every log line

    def push(self, text, src):
        now = time.time()
        new = []
        with self.lock:
            for ln in text.replace("\r", "").split("\n"):
                if ln == "":
                    continue
                self.lines.append((self.next_index, now, ln))
                self.next_index += 1
                self.total += 1
                new.append(ln)
            if len(self.lines) > self.MAX:
                del self.lines[: len(self.lines) - self.MAX]
            self.last_rx = now
            self.last_from = src
        if self.on_line:
            for ln in new:
                try:
                    self.on_line(ln)
                except Exception:
                    pass

    def since(self, idx):
        with self.lock:
            out = [l for l in self.lines if l[0] >= idx]
            return out, self.next_index

    def clear(self):
        with self.lock:
            self.lines = []

    def dump(self):
        with self.lock:
            return "\n".join("%s  %s" % (time.strftime("%H:%M:%S", time.localtime(t)), s)
                             for _, t, s in self.lines) + "\n"

    def status(self):
        with self.lock:
            age = (time.time() - self.last_rx) if self.last_rx else None
            return {
                "bind_error": self.bind_error,
                "last_rx_age": age,
                "last_from": self.last_from,
                "total": self.total,
                "next_index": self.next_index,
            }


class ConsoleScan:
    """Rebuilds the console's own view of /temp0 from the payload's boot log.

    apps/m16cgpsp/main.c prints, on every payload start (step 445/447):
        M16C: scanning /temp0
        M16C: ROM <i>  /  M16C:   NAME <file>  /  M16C:   PATH /temp0/<file>
        M16C: ENTRY <i> <file> -- <bytes> bytes, <OK | reason>
        M16C: <a> available, <b> unavailable of <n> listed
    plus the BIOS candidates. Those lines are the console's authoritative
    answer to "what is in /temp0", so nothing has to be probed remotely."""

    RE_SCAN = re.compile(r"^M16C: scanning (\S+)")
    RE_NAME = re.compile(r"^M16C:\s+NAME (.+)$")
    RE_PATH = re.compile(r"^M16C:\s+PATH (.+)$")
    RE_ENTRY = re.compile(r"^M16C: ENTRY (\d+) (.+) -- (\d+) bytes, (.+)$")
    RE_DONE = re.compile(r"^M16C: (\d+) available, (\d+) unavailable of (\d+) listed")
    RE_FAIL = re.compile(r"^M16C: SCAN FAILED")
    RE_BIOS_OK = re.compile(r"^M16C: BIOS candidate \d+ '([^']+)' OPENED, (\d+) bytes")
    RE_BIOS_NO = re.compile(r"^M16C: BIOS candidate \d+ '([^']+)' not present")
    RE_BIOS_BAD = re.compile(r"^M16C: BIOS SIZE WRONG")
    RE_BIOS_GOOD = re.compile(r"^M16C: BIOS content OK")

    def __init__(self, on_commit):
        self.lock = threading.Lock()
        self.pending = None      # scan in progress
        self.on_commit = on_commit

    def feed(self, line):
        with self.lock:
            m = self.RE_SCAN.match(line)
            if m:
                self.pending = {"dir": m.group(1), "started": time.time(), "files": {},
                                "order": [], "bios": {}}
                return
            pend = self.pending
            if pend is None:
                return
            m = self.RE_NAME.match(line)
            if m:
                name = m.group(1).strip()
                pend["files"].setdefault(name, {"name": name, "size": None, "reason": "", "ok": None})
                pend["order"].append(name)
                return
            m = self.RE_PATH.match(line)
            if m and pend["order"]:
                pend["files"][pend["order"][-1]]["path"] = m.group(1).strip()
                return
            m = self.RE_ENTRY.match(line)
            if m:
                name, size, reason = m.group(2).strip(), int(m.group(3)), m.group(4).strip()
                f = pend["files"].setdefault(name, {"name": name, "size": None, "reason": "", "ok": None})
                f["size"], f["reason"], f["ok"] = size, reason, (reason == "OK")
                return
            m = self.RE_BIOS_OK.match(line)
            if m:
                pend["bios"] = {"path": m.group(1), "size": int(m.group(2)), "present": True}
                return
            m = self.RE_BIOS_NO.match(line)
            if m and not pend["bios"].get("present"):
                pend["bios"].setdefault("missing", []).append(m.group(1))
                return
            if self.RE_BIOS_BAD.match(line):
                pend["bios"]["size_ok"] = False
                return
            if self.RE_BIOS_GOOD.match(line):
                pend["bios"]["size_ok"] = True
                pend["bios"]["content_ok"] = True
                return
            m = self.RE_DONE.match(line)
            if m:
                scan = {"when": time.time(), "dir": pend["dir"],
                        "available": int(m.group(1)), "unavailable": int(m.group(2)),
                        "listed": int(m.group(3)), "files": pend["files"], "bios": pend["bios"],
                        "failed": False}
                # keep collecting BIOS lines, which come after the listing
                pend["committed"] = scan
                self.on_commit(scan)
                return
            if self.RE_FAIL.match(line):
                self.on_commit({"when": time.time(), "dir": pend["dir"], "failed": True,
                                "files": {}, "bios": {}, "listed": 0, "available": 0, "unavailable": 0})
                self.pending = None

    def bios_update(self):
        """BIOS lines arrive after the listing commit; re-commit with them."""
        with self.lock:
            pend = self.pending
            if pend and pend.get("committed") and pend["bios"]:
                scan = dict(pend["committed"])
                scan["bios"] = pend["bios"]
                return scan
        return None


class PayloadTracker:
    """Is the emulator running on the console? The payload cannot be asked
    (it is frozen), but its log says so: `M16C: scanning` at boot, `SESSION n
    IDENTITY` when a game starts, `picker entry` when it returns to the
    picker, and lua/m16c.lua.in prints `Done. status=` / `verdict:` once the
    payload has returned. While it runs, the Lua loader is NOT listening -- a
    failed probe then means "exit the emulator", not "arm the loader"."""

    RE_BOOT = re.compile(r"^M16C: scanning ")
    RE_SESSION = re.compile(r"^M16C: SESSION (\d+) IDENTITY -- CODE '([^']*)'")
    RE_PICKER = re.compile(r"^M16C: picker entry")
    RE_DONE = re.compile(r"^Done\. status=(-?\d+) step=(\d+)")
    RE_VERDICT = re.compile(r"^verdict: (.+)$")
    RE_LEDGER = re.compile(r"^M16C: SESSIONS STARTED ")

    def __init__(self, initial, on_change):
        self.state = dict(initial or {})
        self.on_change = on_change
        self.lock = threading.Lock()

    def _set(self, **kw):
        st = dict(self.state)
        st.update(kw)
        st["when"] = time.time()
        self.state = st
        self.on_change(st)

    def mark_sent(self):
        with self.lock:
            self._set(state="starting", detail="script sent, waiting for the payload to boot", verdict="")

    def mark_gone(self):
        """A successful loader probe means nothing is running any more."""
        with self.lock:
            if self.state.get("state") in ("running", "starting"):
                self._set(state="exited", detail="loader is listening again", verdict=self.state.get("verdict", ""))

    def feed(self, line):
        with self.lock:
            if self.RE_BOOT.match(line):
                self._set(state="running", detail="at the ROM picker", verdict="")
                return
            m = self.RE_SESSION.match(line)
            if m:
                self._set(state="running", detail="playing session %s (%s)" % (m.group(1), m.group(2)))
                return
            if self.RE_PICKER.match(line):
                self._set(state="running", detail="at the ROM picker")
                return
            m = self.RE_DONE.match(line)
            if m:
                self._set(state="exited", detail="payload returned (status %s)" % m.group(1))
                return
            m = self.RE_VERDICT.match(line)
            if m:
                self._set(state="exited", detail="payload returned", verdict=m.group(1).strip())
                return
            if self.RE_LEDGER.match(line):
                self._set(state="exited", detail="payload returned")


def udp_log_thread(buf, stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # No SO_REUSEADDR on purpose: a second launcher (or nc) already on 9027
    # must FAIL to bind so the UI can say so, instead of the two silently
    # splitting the datagrams between them.
    try:
        s.bind(("0.0.0.0", LOG_PORT))
    except OSError as e:
        buf.bind_error = "cannot bind udp/%d: %s" % (LOG_PORT, e)
        return
    s.settimeout(0.5)
    while not stop.is_set():
        try:
            data, addr = s.recvfrom(65535)
        except socket.timeout:
            continue
        except OSError:
            break
        buf.push(data.decode("utf-8", "replace"), addr[0])
    s.close()


# --------------------------------------------------------------------------
# Job runner: exactly one frozen tool at a time, output streamed to the UI
# --------------------------------------------------------------------------
class Job:
    """One unit of work for the UI: a single tool invocation, or an ordered
    batch of them (Send all). Each step is {"argv", "title", "result"}."""

    def __init__(self, kind, title, steps, cwd):
        self.kind = kind
        self.title = title
        self.steps = steps
        self.cwd = cwd
        self.lines = []
        self.started = time.time()
        self.ended = None
        self.rc = None
        self.proc = None
        self.cancelled = False
        self.step_index = 0
        self.steps_done = 0
        self.steps_failed = 0
        self.result = steps[0]["result"] if steps else {}
        # One progress record per step, filled from the tool's own output.
        self.progress = []
        for st in steps:
            r = st.get("result", {})
            self.progress.append({
                "name": r.get("name") or os.path.basename(r.get("local", "")) or st["title"],
                "remote": r.get("remote", ""),
                "kind": r.get("kind", kind),
                "size": r.get("size", 0),
                "sent": 0, "rate": None,
                "state": "pending",       # pending / running / done / failed / cancelled
                "note": "",
            })

    def public(self, since=0):
        return {
            "kind": self.kind,
            "title": self.title,
            "running": self.rc is None and not self.cancelled,
            "rc": self.rc,
            "cancelled": self.cancelled,
            "started": self.started,
            "ended": self.ended,
            "lines": self.lines[since:],
            "next": len(self.lines),
            "result": self.result,
            "steps": len(self.steps),
            "step": min(self.step_index + 1, len(self.steps)),
            "steps_done": self.steps_done,
            "steps_failed": self.steps_failed,
            "progress": self.progress,
        }


# What tools/upload.py prints, unchanged, and what it means for the bar.
_RE_PROGRESS = re.compile(r"^\s*(\d+) / (\d+) bytes\s+([\d.]+) MB/s")
_RE_HAVE = re.compile(r"port \d+: console has (\d+) bytes")
_RE_COMPLETE = re.compile(r"^complete: (\d+) bytes")
_RE_DROPPED = re.compile(r"dropped at (\d+) bytes")
_RE_SIZE = re.compile(r"^\s+(\d+) bytes, fnv1a=")
_RE_ATTEMPT = re.compile(r"^\s*attempt (\d+)")
_RE_SENT_SCRIPT = re.compile(r"^script: (\d+) bytes")
_RE_SENT_BLOB = re.compile(r"^blob\s*: (\d+) bytes")


def parse_progress(prog, line):
    """Update one step's progress record from one output line."""
    m = _RE_PROGRESS.match(line)
    if m:
        prog["sent"], prog["size"], prog["rate"] = int(m.group(1)), int(m.group(2)), float(m.group(3))
        prog["note"] = ""
        return
    m = _RE_HAVE.search(line)
    if m:
        have = int(m.group(1))
        prog["sent"] = have
        prog["note"] = "resuming from %s" % human_size(have) if have else ""
        return
    m = _RE_COMPLETE.match(line)
    if m:
        prog["sent"] = prog["size"] = int(m.group(1)) or prog["size"]
        prog["state"] = "done"
        prog["note"] = ""
        return
    m = _RE_DROPPED.search(line)
    if m:
        prog["sent"] = int(m.group(1))
        prog["note"] = "connection dropped, retrying"
        return
    m = _RE_SIZE.match(line)
    if m and not prog["size"]:
        prog["size"] = int(m.group(1))
        return
    m = _RE_ATTEMPT.match(line)
    if m:
        prog["note"] = line.strip()
        return
    if "no live receiver" in line or "loader did not take the script" in line:
        prog["note"] = line.strip()
        return
    if line.startswith("gave up"):
        prog["note"] = "gave up short of the full file"
        return
    # send.py (launch) lines: two stages, treat as 0 / 50 / 100 %
    m = _RE_SENT_SCRIPT.match(line)
    if m:
        prog["note"] = "script sent, streaming payload"
        prog["sent"] = prog["size"] // 2 if prog["size"] else 0
        return
    m = _RE_SENT_BLOB.match(line)
    if m:
        prog["sent"] = prog["size"] or int(m.group(1))
        prog["note"] = ""


class JobRunner:
    def __init__(self):
        self.lock = threading.Lock()
        self.current = None
        self.on_done = None
        self.on_start = None
        self.on_step_start = None

    def busy(self):
        with self.lock:
            return self.current is not None and self.current.rc is None \
                and not self.current.cancelled

    def start(self, job):
        with self.lock:
            if self.current and self.current.rc is None and not self.current.cancelled:
                raise RuntimeError("another job is still running")
            self.current = job
        if self.on_start:
            self.on_start(job)
        t = threading.Thread(target=self._run, args=(job,), daemon=True)
        t.start()
        return job

    def _run(self, job):
        total = len(job.steps)
        final_rc = 0
        for i, step in enumerate(job.steps):
            job.step_index = i
            if job.cancelled:
                break
            if total > 1:
                job.lines.append("=== [%d/%d] %s" % (i + 1, total, step["title"]))
            prog = job.progress[i]
            prog["state"] = "running"
            if self.on_step_start:
                try:
                    self.on_step_start(job, step)
                except Exception:
                    pass
            rc = self._run_step(job, step)
            if job.cancelled:
                prog["state"] = "cancelled"
            elif rc == 0:
                if prog["state"] != "done":
                    prog["state"] = "done"
                    prog["sent"] = prog["size"]
            else:
                prog["state"] = "failed"
                if not prog["note"]:
                    prog["note"] = "exit code %s" % rc
            if rc == 0 and not job.cancelled:
                job.steps_done += 1
                if self.on_done:
                    try:
                        self.on_done(job, step)
                    except Exception as e:  # never let bookkeeping kill the runner
                        job.lines.append("launcher: post-step hook failed: %s" % e)
            else:
                job.steps_failed += 1
                final_rc = rc if rc else 1
                if total > 1 and not job.cancelled:
                    remaining = total - i - 1
                    job.lines.append("launcher: stopping the batch here; %d not sent" % remaining)
                for later in job.progress[i + 1:]:
                    later["state"] = "cancelled" if job.cancelled else "skipped"
                break
        job.ended = time.time()
        job.rc = final_rc
        if job.cancelled:
            job.lines.append("launcher: cancelled")
        elif total > 1:
            job.lines.append("launcher: batch finished, %d of %d sent" % (job.steps_done, total))
        elif final_rc == 0:
            job.lines.append("launcher: finished OK")
        else:
            job.lines.append("launcher: exited with code %d" % final_rc)

    def _run_step(self, job, step):
        argv = step["argv"]
        job.lines.append("$ " + " ".join(quote_arg(a) for a in argv))
        env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8")
        try:
            job.proc = subprocess.Popen(
                argv, cwd=job.cwd, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                text=True, bufsize=1, universal_newlines=True,
                encoding="utf-8", errors="replace", env=env,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except OSError as e:
            job.lines.append("launcher: could not start %s (%s)" % (argv[0], e))
            return -1
        prog = job.progress[job.step_index]
        for line in job.proc.stdout:
            line = line.rstrip("\n")
            job.lines.append(line)
            try:
                parse_progress(prog, line)
            except (ValueError, KeyError):
                pass
        job.proc.wait()
        return job.proc.returncode

    def cancel(self):
        with self.lock:
            job = self.current
        if job and job.proc and job.rc is None:
            job.cancelled = True
            try:
                job.proc.terminate()
            except OSError:
                pass
            return True
        return False

    def public(self, since=0):
        with self.lock:
            job = self.current
        return job.public(since) if job else None


def quote_arg(a):
    return '"%s"' % a if " " in a else a


# --------------------------------------------------------------------------
# Native folder / file dialogs, run on the MAIN thread via a queue
# --------------------------------------------------------------------------
class Dialogs:
    def __init__(self):
        self.q = queue.Queue()
        self.available = False
        try:
            import tkinter  # noqa: F401
            from tkinter import filedialog  # noqa: F401
            self.available = True
        except Exception:
            self.available = False

    window = None      # StatusWindow, when one owns the Tk main loop

    def request(self, kind, title, initial):
        if not self.available:
            return None
        done = threading.Event()
        box = {}
        if self.window is not None:
            def on_tk():
                try:
                    box["result"] = self._show(kind, title, initial, parent=self.window.root)
                except Exception as e:
                    box["result"] = None
                    box["error"] = str(e)
                done.set()
            self.window.call_soon(on_tk)
        else:
            self.q.put((kind, title, initial, done, box))
        done.wait(timeout=600)
        return box.get("result")

    def pump(self, stop):
        """Blocks the calling (main) thread, servicing dialog requests."""
        while not stop.is_set():
            try:
                kind, title, initial, done, box = self.q.get(timeout=0.5)
            except queue.Empty:
                continue
            try:
                box["result"] = self._show(kind, title, initial)
            except Exception as e:
                box["result"] = None
                box["error"] = str(e)
            done.set()

    def _show(self, kind, title, initial, parent=None):
        import tkinter
        from tkinter import filedialog
        own_root = parent is None
        root = parent
        if own_root:
            root = tkinter.Tk()
            root.withdraw()
            try:
                root.attributes("-topmost", True)
            except tkinter.TclError:
                pass
        opts = {"title": title, "parent": root}
        if initial and os.path.isdir(initial):
            opts["initialdir"] = initial
        elif initial and os.path.isfile(initial):
            opts["initialdir"] = os.path.dirname(initial)
        try:
            if kind == "dir":
                return filedialog.askdirectory(**opts) or None
            if kind == "bios":
                return filedialog.askopenfilename(
                    filetypes=[("GBA BIOS", "*.bin"), ("All files", "*.*")], **opts) or None
            return filedialog.askopenfilename(**opts) or None
        finally:
            if own_root:
                root.destroy()


# --------------------------------------------------------------------------
# Status window: a real window, so the launcher has a taskbar button
# --------------------------------------------------------------------------
WIN_BG, WIN_BG2, WIN_LINE = "#11112a", "#181838", "#2a2a5a"
WIN_INK, WIN_INK2, WIN_INK3, WIN_ACCENT = "#ecebff", "#b6b3d6", "#7d7aa3", "#a99cff"
STATUS_HEX = {"good": "#3ecf8e", "warn": "#f5c451", "bad": "#ff6b7a", "muted": "#7d7aa3"}


class StatusWindow:
    """Small native window (Tkinter) owned by the main thread. Shows the
    console status, the dashboard address and the last operator-log lines,
    with Open / Check / Quit buttons. Closing it hides it to the tray when a
    tray icon exists; otherwise closing quits."""

    def __init__(self, app, url):
        import tkinter as tk
        self.app, self.url, self.tk = app, url, tk
        self.log_idx = 0
        self.kind = None
        if platform.system().lower().startswith("win"):
            try:
                import ctypes
                ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("NBT.LUAp0rt.Launcher")
            except Exception:
                pass
        root = self.root = tk.Tk()
        root.title(APP_NAME)
        root.configure(bg=WIN_BG)
        root.resizable(False, False)
        self._set_icon()

        head = tk.Frame(root, bg=WIN_BG)
        head.pack(fill="x", padx=16, pady=(14, 6))
        self.mascot = None
        try:
            self.mascot = tk.PhotoImage(file=os.path.join(UI_DIR, "mascot.png")).subsample(6, 6)
            tk.Label(head, image=self.mascot, bg=WIN_BG).pack(side="left", padx=(0, 12))
        except Exception:
            pass
        tbox = tk.Frame(head, bg=WIN_BG)
        tbox.pack(side="left", fill="x")
        tk.Label(tbox, text="LUAp0rt GBA", font=("Segoe UI", 15, "bold"), fg=WIN_INK, bg=WIN_BG,
                 anchor="w").pack(anchor="w")
        tk.Label(tbox, text="GAME BOY ADVANCE FOR PLAYSTATION 5  \u00b7  Created by NBT",
                 font=("Segoe UI", 8), fg=WIN_INK3, bg=WIN_BG, anchor="w").pack(anchor="w")

        srow = tk.Frame(root, bg=WIN_BG2, highlightbackground=WIN_LINE, highlightthickness=1)
        srow.pack(fill="x", padx=16, pady=6)
        self.dot = tk.Canvas(srow, width=14, height=14, bg=WIN_BG2, highlightthickness=0)
        self.dot.pack(side="left", padx=(10, 8), pady=8)
        self.dot_id = self.dot.create_oval(2, 2, 12, 12, fill=STATUS_HEX["muted"], outline="")
        self.status = tk.Label(srow, text="starting\u2026", font=("Segoe UI", 10), fg=WIN_INK,
                               bg=WIN_BG2, anchor="w")
        self.status.pack(side="left", fill="x", expand=True, pady=8)

        urow = tk.Frame(root, bg=WIN_BG)
        urow.pack(fill="x", padx=16)
        tk.Label(urow, text="Dashboard", font=("Segoe UI", 8), fg=WIN_INK3, bg=WIN_BG).pack(side="left")
        link = tk.Label(urow, text=url, font=("Consolas", 10), fg=WIN_ACCENT, bg=WIN_BG, cursor="hand2")
        link.pack(side="left", padx=8)
        link.bind("<Button-1>", lambda e: self.open_dashboard())

        tk.Label(root, text="Console log (UDP %d)" % LOG_PORT, font=("Segoe UI", 8), fg=WIN_INK3,
                 bg=WIN_BG, anchor="w").pack(fill="x", padx=16, pady=(8, 2))
        self.log = tk.Text(root, height=7, width=64, font=("Consolas", 9), fg="#cfcde8", bg="#06060f",
                           relief="flat", highlightbackground=WIN_LINE, highlightthickness=1,
                           state="disabled", wrap="none")
        self.log.pack(fill="x", padx=16)

        brow = tk.Frame(root, bg=WIN_BG)
        brow.pack(fill="x", padx=16, pady=(12, 8))
        self._button(brow, "Open dashboard", self.open_dashboard, primary=True).pack(side="left")
        self._button(brow, "Check console", self.check_now).pack(side="left", padx=8)
        self._button(brow, "Quit launcher", self.quit).pack(side="right")
        self.hint = tk.Label(root, text="", font=("Segoe UI", 8), fg=WIN_INK3, bg=WIN_BG, anchor="w")
        self.hint.pack(fill="x", padx=16, pady=(0, 12))

        root.protocol("WM_DELETE_WINDOW", self.on_close)
        root.after(300, self.tick)

    def _button(self, parent, text, cmd, primary=False):
        tk = self.tk
        return tk.Button(parent, text=text, command=cmd, font=("Segoe UI", 9, "bold" if primary else "normal"),
                         fg="#ffffff" if primary else WIN_INK, bg="#5a4fcf" if primary else WIN_BG2,
                         activebackground="#6a5fe0" if primary else "#22224c", activeforeground=WIN_INK,
                         relief="flat", padx=12, pady=5, cursor="hand2", bd=0,
                         highlightbackground=WIN_LINE, highlightthickness=1)

    def _set_icon(self):
        ico = os.path.join(UI_DIR, "mascot.ico")
        try:
            if os.path.isfile(ico) and platform.system().lower().startswith("win"):
                self.root.iconbitmap(default=ico)
        except Exception:
            pass
        try:
            self._iconphoto = self.tk.PhotoImage(file=os.path.join(UI_DIR, "mascot.png"))
            self.root.iconphoto(True, self._iconphoto)
        except Exception:
            pass

    # -- main-thread plumbing
    def call_soon(self, fn):
        try:
            self.root.after(0, fn)
        except Exception:
            pass

    def run(self):
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            pass

    def tick(self):
        if self.app.stop.is_set():
            try:
                self.root.destroy()
            except Exception:
                pass
            return
        kind, text = self.app.console_summary()
        if kind != self.kind:
            self.kind = kind
            self.dot.itemconfigure(self.dot_id, fill=STATUS_HEX.get(kind, STATUS_HEX["muted"]))
        if self.status.cget("text") != text:
            self.status.configure(text=text)
        lines, nxt = self.app.log.since(self.log_idx)
        if lines:
            self.log_idx = nxt
            self.log.configure(state="normal")
            for _, t, ln in lines[-200:]:
                self.log.insert("end", time.strftime("%H:%M:%S  ", time.localtime(t)) + ln + "\n")
            n = int(self.log.index("end-1c").split(".")[0])
            if n > 400:
                self.log.delete("1.0", "%d.0" % (n - 400))
            self.log.see("end")
            self.log.configure(state="disabled")
        tray = self.app.tray and self.app.tray.icon
        self.hint.configure(text="Closing this window asks whether to quit or keep running in the tray."
                            if tray else "Closing this window quits the launcher.")
        self.root.after(1000, self.tick)

    # -- actions
    def open_dashboard(self):
        webbrowser.open(self.url)

    def check_now(self):
        threading.Thread(target=self._check, daemon=True).start()

    def _check(self):
        try:
            self.app.do_ping()
        except ValueError:
            pass

    def show(self):
        try:
            self.root.deiconify()
            self.root.lift()
            self.root.focus_force()
        except Exception:
            pass

    def hide(self):
        try:
            self.root.withdraw()
        except Exception:
            pass

    def on_close(self):
        """The window's X button. With a tray icon the launcher would keep
        running invisibly, so say so and ask what the user actually wants."""
        if not (self.app.tray and self.app.tray.icon):
            self.quit()
            return
        from tkinter import messagebox
        answer = messagebox.askyesnocancel(
            APP_NAME,
            "The launcher is still running in the system tray.\n\n"
            "Quit completely (stop the dashboard server and remove the tray icon)?\n\n"
            "Yes = quit everything\n"
            "No = close this window only, keep running in the tray\n"
            "Cancel = keep this window open",
            icon="warning", default="no", parent=self.root)
        if answer is True:
            self.quit()
        elif answer is False:
            self.hide()

    def quit(self):
        self.app.stop.set()


# --------------------------------------------------------------------------
# System tray icon (optional: needs pystray + Pillow; the exe bundles them)
# --------------------------------------------------------------------------
STATUS_RGB = {"good": (62, 207, 142), "warn": (245, 196, 81),
              "bad": (255, 107, 122), "muted": (125, 122, 163)}


class Tray:
    def __init__(self, app, url):
        self.app = app
        self.url = url
        self.icon = None
        self.kind = None
        self.text = "starting"
        self.available = False
        try:
            import pystray  # noqa: F401
            from PIL import Image  # noqa: F401
            self.available = True
        except Exception:
            self.available = False

    # -- image: the mascot with a status dot in the corner
    def _image(self, kind):
        from PIL import Image, ImageDraw
        size = 64
        try:
            base = Image.open(os.path.join(UI_DIR, "mascot.png")).convert("RGBA")
            base = base.resize((size, size), Image.NEAREST)
        except Exception:
            base = Image.new("RGBA", (size, size), (17, 17, 42, 255))
        d = ImageDraw.Draw(base)
        r = 13
        x1, y1 = size - 2, size - 2
        d.ellipse((x1 - 2 * r - 2, y1 - 2 * r - 2, x1, y1), fill=(11, 11, 26, 255))
        d.ellipse((x1 - 2 * r, y1 - 2 * r, x1 - 2, y1 - 2), fill=STATUS_RGB.get(kind, STATUS_RGB["muted"]))
        return base

    def start(self):
        if not self.available:
            return False
        import pystray
        menu = pystray.Menu(
            pystray.MenuItem(lambda item: "%s v%s  -  %s" % (APP_NAME, APP_VERSION, self.url),
                             None, enabled=False),
            pystray.MenuItem(lambda item: self.text, None, enabled=False),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Open dashboard", self._open, default=True),
            pystray.MenuItem("Show status window", self._show_window,
                             visible=lambda item: self.app.window is not None),
            pystray.MenuItem("Check console now", self._ping),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Quit launcher", self._quit),
        )
        self.icon = pystray.Icon("luap0rt-launcher", self._image("muted"), APP_NAME, menu)
        try:
            self.icon.run_detached()
        except Exception as e:
            say("tray: unavailable (%s)" % e)
            self.icon = None
            self.available = False
            return False
        return True

    def update(self, kind, text):
        if not self.icon:
            return
        if kind != self.kind:
            self.kind = kind
            try:
                self.icon.icon = self._image(kind)
            except Exception:
                pass
        if text != self.text:
            self.text = text
            try:
                self.icon.title = "%s - %s" % (APP_NAME, text)
                self.icon.update_menu()
            except Exception:
                pass

    def stop(self):
        if self.icon:
            try:
                self.icon.stop()
            except Exception:
                pass
            self.icon = None

    def _open(self, icon=None, item=None):
        webbrowser.open(self.url)

    def _ping(self, icon=None, item=None):
        threading.Thread(target=self._ping_now, daemon=True).start()

    def _ping_now(self):
        try:
            self.app.do_ping()
        except ValueError:
            pass
        self.update(*self.app.console_summary())

    def _show_window(self, icon=None, item=None):
        w = self.app.window
        if w is not None:
            w.call_soon(w.show)

    def _quit(self, icon=None, item=None):
        self.app.stop.set()


# --------------------------------------------------------------------------
# Project introspection
# --------------------------------------------------------------------------
class Project:
    def __init__(self):
        self.hash_cache = {}     # path -> (size, mtime, sha)

    def cached_sha(self, path):
        st = os.stat(path)
        key = (st.st_size, st.st_mtime_ns)
        hit = self.hash_cache.get(path)
        if hit and hit[0] == key:
            return hit[1]
        sha = sha256_file(path)
        self.hash_cache[path] = (key, sha)
        return sha

    def find_send(self):
        for relpath, label in SEND_CANDIDATES:
            p = os.path.normpath(os.path.join(ROOT, relpath))
            if os.path.isfile(p):
                return {"path": p, "rel": relpath, "label": label}
        return None

    def find_upload(self):
        for relpath, receiver in UPLOAD_CANDIDATES:
            p = os.path.normpath(os.path.join(ROOT, relpath))
            r = os.path.normpath(os.path.join(ROOT, receiver))
            if os.path.isfile(p):
                return {
                    "path": p, "rel": relpath, "receiver": receiver,
                    "available": os.path.isfile(r),
                    "reason": "" if os.path.isfile(r) else
                    "receiver script %s is not present in this package; "
                    "use your own compatible PS5 file-transfer method" % receiver,
                }
        return {"path": "", "rel": "", "receiver": "", "available": False,
                "reason": "tools/upload.py is not present next to this launcher"}

    def payload_candidates(self, cfg):
        out = []
        for key, label, d in PAYLOAD_CANDIDATES:
            lua = os.path.normpath(os.path.join(ROOT, d, "m16cgpsp.lua"))
            binp = os.path.normpath(os.path.join(ROOT, d, "m16cgpsp.bin"))
            if os.path.isfile(lua) and os.path.isfile(binp):
                out.append(self.describe_payload(key, label, lua, binp))
        if cfg.get("custom_lua") or cfg.get("custom_bin"):
            out.append(self.describe_payload("custom", "Custom pair",
                                             cfg.get("custom_lua", ""),
                                             cfg.get("custom_bin", "")))
        return out

    def describe_payload(self, key, label, lua, binp):
        d = {"key": key, "label": label, "lua": lua, "bin": binp,
             "lua_rel": rel(lua) if lua else "", "bin_rel": rel(binp) if binp else "",
             "ok": False, "verified": False, "problems": []}
        for name, p in (("loader script", lua), ("payload blob", binp)):
            if not p:
                d["problems"].append("%s not set" % name)
            elif not os.path.isfile(p):
                d["problems"].append("%s missing: %s" % (name, p))
        if d["problems"]:
            return d
        try:
            d["lua_size"] = os.path.getsize(lua)
            d["bin_size"] = os.path.getsize(binp)
            d["bin_sha256"] = self.cached_sha(binp)
            d["lua_sha256"] = self.cached_sha(lua)
        except OSError as e:
            d["problems"].append(str(e))
            return d
        d["ok"] = True
        d["verified"] = d["bin_sha256"] == GOLDEN_M16C_BIN_SHA256
        d["golden"] = GOLDEN_M16C_BIN_SHA256
        return d

    def select_payload(self, cfg):
        cands = self.payload_candidates(cfg)
        want = cfg.get("payload") or ""
        for c in cands:
            if c["key"] == want:
                return c, cands
        # default preference: the first verified candidate, else the first one
        for c in cands:
            if c["verified"]:
                return c, cands
        return (cands[0] if cands else None), cands

    def check_bios(self, path):
        d = {"path": path, "ok": False, "state": "unset", "message": "No BIOS file selected"}
        if not path:
            return d
        if not os.path.isfile(path):
            d.update(state="missing", message="File not found")
            return d
        size = os.path.getsize(path)
        d["size"] = size
        if size != BIOS_SIZE:
            d.update(state="badsize",
                     message="Wrong size: %d bytes, a GBA BIOS is exactly %d bytes"
                     % (size, BIOS_SIZE))
            return d
        with open(path, "rb") as f:
            data = f.read()
        d.update(ok=True, state="ok", fnv1a="0x%08X" % fnv1a32(data),
                 sha256=hashlib.sha256(data).hexdigest(),
                 message="16,384 bytes - size gate passes (the console reports the same FNV-1a)")
        return d

    def scan_roms(self, rom_dir, uploads, console_scan=None, deselected=None):
        d = {"dir": rom_dir, "ok": False, "roms": [], "ignored": 0, "message": ""}
        desel = set(deselected or [])
        cs = console_scan or {}
        cs_files = cs.get("files", {}) if not cs.get("failed") else {}
        d["console"] = {"when": cs.get("when"), "listed": cs.get("listed", 0),
                        "available": cs.get("available", 0), "failed": bool(cs.get("failed")),
                        "bios": cs.get("bios", {}), "have_scan": bool(cs.get("when"))}
        d["console_only"] = []
        if not rom_dir:
            d["message"] = "No ROM folder selected"
            return d
        if not os.path.isdir(rom_dir):
            d["message"] = "Folder not found"
            return d
        try:
            names = sorted(os.listdir(rom_dir), key=str.lower)
        except OSError as e:
            d["message"] = str(e)
            return d
        for name in names:
            p = os.path.join(rom_dir, name)
            if not os.path.isfile(p):
                continue
            if not name.lower().endswith(ROM_EXT):
                d["ignored"] += 1
                continue
            st = os.stat(p)
            entry = {"name": name, "path": p, "size": st.st_size,
                     "size_h": human_size(st.st_size), "mtime": st.st_mtime,
                     "title": "", "code": "", "remote": CONSOLE_ROM_DIR + "/" + name}
            entry.update(read_gba_header(p))
            up = uploads.get(entry["remote"])
            if up:
                entry["uploaded"] = up
                entry["upload_state"] = "current" if (
                    up.get("size") == st.st_size and
                    abs(up.get("mtime", 0) - st.st_mtime) < 1.0) else "stale"
            cf = cs_files.get(name)
            sent_after_scan = bool(up and cs.get("when") and up.get("when", 0) > cs["when"])
            if cf is not None and not sent_after_scan:
                entry["console"] = {"present": True, "size": cf.get("size"),
                                    "size_match": cf.get("size") in (None, st.st_size),
                                    "ok": cf.get("ok"), "reason": cf.get("reason", "")}
            elif cs.get("when") and not cs.get("failed"):
                # sent since the console last reported: unknown until the next boot
                entry["console"] = {"present": None, "pending": True} if sent_after_scan else {"present": False}
            entry["checked"] = name not in desel
            # Skip rule. When the console has reported in this session, its
            # word wins: present with the same size -> skip; absent or a
            # different size -> send (even if our own record says it was
            # sent); sent after the report -> skip until the next report.
            # Without a report, skip only what this launcher sent unchanged.
            con = entry.get("console")
            if con:
                if con.get("pending"):
                    entry["needs_send"] = False
                else:
                    entry["needs_send"] = not (con.get("present") and con.get("size_match"))
            else:
                entry["needs_send"] = entry.get("upload_state") != "current"
            d["roms"].append(entry)
        local_names = {r["name"] for r in d["roms"]}
        d["console_only"] = [f for n, f in cs_files.items() if n not in local_names]
        d["ok"] = True
        d["count"] = len(d["roms"])
        d["checked"] = sum(1 for r in d["roms"] if r["checked"])
        d["to_send"] = sum(1 for r in d["roms"][:ROM_PICKER_CAPACITY] if r["checked"] and r["needs_send"])
        d["capacity"] = ROM_PICKER_CAPACITY
        d["over_capacity"] = max(0, d["count"] - ROM_PICKER_CAPACITY)
        return d


def read_gba_header(path):
    """Internal title (0xA0, 12 bytes) and game code (0xAC, 4 bytes)."""
    try:
        with open(path, "rb") as f:
            hdr = f.read(0xC0)
        if len(hdr) < 0xB0:
            return {}
        title = hdr[0xA0:0xAC].split(b"\0", 1)[0].decode("ascii", "replace").strip()
        code = hdr[0xAC:0xB0].decode("ascii", "replace").strip("\0 ")
        maker = hdr[0xB0:0xB2].decode("ascii", "replace").strip("\0 ")
        return {"title": title, "code": code, "maker": maker}
    except OSError:
        return {}


# --------------------------------------------------------------------------
# Ping (ICMP only -- never touch the loader port)
# --------------------------------------------------------------------------
def ping(host):
    if platform.system().lower().startswith("win"):
        argv = ["ping", "-n", "1", "-w", "1500", host]
    else:
        argv = ["ping", "-c", "1", "-W", "2", host]
    t0 = time.time()
    try:
        r = subprocess.run(argv, capture_output=True, text=True, timeout=6,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        ms = (time.time() - t0) * 1000.0
        ok = r.returncode == 0 and ("TTL=" in r.stdout or "ttl=" in r.stdout)
        return {"ok": ok, "ms": round(ms, 1), "output": r.stdout.strip()[-400:]}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {"ok": False, "ms": None, "output": str(e)}


def probe_loader(host, timeout=1.5):
    """Is something listening on the loader port? Connect, send NOTHING, and
    close with RST (SO_LINGER 0) so the far end sees a reset rather than an
    empty script. Returns ok=True only when the TCP handshake completed."""
    t0 = time.time()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, LOADER_PORT))
        ms = round((time.time() - t0) * 1000.0, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        except OSError:
            pass
        return {"ok": True, "ms": ms, "error": ""}
    except socket.timeout:
        return {"ok": False, "ms": None, "error": "timeout (host silent on %d)" % LOADER_PORT}
    except OSError as e:
        err = getattr(e, "strerror", None) or str(e)
        if getattr(e, "errno", None) in (111, 10061) or "refused" in err.lower():
            err = "connection refused (nothing listening on %d)" % LOADER_PORT
        return {"ok": False, "ms": None, "error": err}
    finally:
        try:
            sock.close()
        except OSError:
            pass


# --------------------------------------------------------------------------
# HTTP API
# --------------------------------------------------------------------------
MIME = {
    ".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8", ".png": "image/png",
    ".svg": "image/svg+xml", ".ico": "image/x-icon", ".json": "application/json",
}


class App:
    def __init__(self, verbose=False):
        # --config (testing) uses exactly that file and never migrates the user's own settings into it
        self.cfg = Config(CONFIG_PATH, migrate=not CONFIG_OVERRIDDEN)
        self.project = Project()
        self.log = LogBuffer()
        self.jobs = JobRunner()
        self.jobs.on_done = self._job_done
        self.jobs.on_start = self._job_start
        self.jobs.on_step_start = self._step_start
        self.console_scan = {}          # session-only, see DEFAULT_CONFIG note
        self.scan = ConsoleScan(self._scan_commit)
        self.payload = PayloadTracker(self.cfg.snapshot().get("payload_state"), self.cfg.set_payload_state)
        self.log.on_line = self._log_line
        self.dialogs = Dialogs()
        self.tray = None
        self.window = None
        self.stop = threading.Event()
        self.verbose = verbose
        self.last_ping = {}
        self.ping_lock = threading.Lock()
        self.last_probe = {}
        self.probe_lock = threading.Lock()
        self.last_script_sent = 0.0    # when a job last talked to the loader

    def console_summary(self):
        """One (kind, text) pair shared by the tray icon and the tooltip.
        kind: good / warn / bad / muted."""
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "")
        if not valid_ipv4(ip):
            return "muted", "Console: no address set"
        log = self.log.status()
        age = log["last_rx_age"]
        if age is not None and age < 30:
            return "good", "Console: payload running (log %ds ago)" % int(age)
        pg = self.last_ping
        pg_age = (time.time() - pg["when"]) if pg.get("when") else None
        if pg_age is not None and pg_age < 3 * AUTO_PING_INTERVAL:
            if pg.get("ok"):
                lstate, _ = self.loader_state()
                extra = {"ok": ", loader listening", "bad": ", loader NOT listening"}.get(lstate, "")
                return "good", "Console: reachable (%s, %s ms)%s" % (ip, pg.get("ms"), extra)
            return "bad", "Console: no ping reply from %s" % ip
        return "warn", "Console: checking %s" % ip

    def do_probe(self, force=False):
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "").strip()
        if not valid_ipv4(ip):
            raise ValueError("Set a valid PS5 IPv4 address first")
        if not force and not cfg.get("probe_loader", True):
            raise ValueError("Loader probe is switched off")
        if self.jobs.busy():
            raise ValueError("A job is talking to the console right now")
        with self.probe_lock:
            res = probe_loader(ip)
            res["when"] = time.time()
            res["ip"] = ip
            self.last_probe = res
        if res["ok"]:
            self.payload.mark_gone()
        return res

    def loader_state(self):
        """(state, text) for the checklist: ok / bad / stale / off / hold."""
        cfg = self.cfg.snapshot()
        if not cfg.get("probe_loader", True):
            return "off", "probe off - you confirm the loader is armed"
        log = self.log.status()
        ps = self.payload.state
        # Fresh log traffic means the emulator is alive -- unless the traffic
        # was its own exit report, in which case the tracker says "exited".
        # Fresh log traffic means the emulator is alive -- except while one of our
        # own jobs runs (the upload receiver logs on the same port) or right
        # after the emulator's own exit report.
        if log["last_rx_age"] is not None and log["last_rx_age"] < 30 and                 ps.get("state") != "exited" and not self.jobs.busy():
            return "ok", "emulator running - the loader accepted the script"
        if ps.get("state") == "running":
            return "ok", "emulator running (%s) - exit it before launching again" % ps.get("detail", "")
        pg = self.last_ping
        pg_age = (time.time() - pg["when"]) if pg.get("when") else None
        if pg_age is not None and pg_age < 3 * AUTO_PING_INTERVAL and pg.get("ok") is False:
            return "bad", "console not answering ping - loader status unknown"
        if time.time() - self.last_script_sent < LOADER_PROBE_HOLDOFF:
            job = self.jobs.current
            if job is not None and job.rc is None and not job.cancelled:
                return "hold", "sending a script now"
            if job is not None and job.rc == 0 and not job.cancelled:
                return "ok", "loader accepted the last script"
            if job is not None and job.rc not in (None, 0):
                return "bad", "the last send failed - is the loader armed?"
            return "hold", "script just sent - not probing for %d s" % LOADER_PROBE_HOLDOFF
        pr = self.last_probe
        age = (time.time() - pr["when"]) if pr.get("when") else None
        if age is None or age > (LOADER_PROBE_RECHECK if pr.get("ok") else LOADER_PROBE_INTERVAL) + 30:
            return "stale", "checking TCP %d..." % LOADER_PORT
        if pr.get("ok"):
            return "ok", "loader listening on TCP %d (checked %ds ago)" % (LOADER_PORT, int(age))
        if ps.get("state") == "exited":
            return "bad", "not listening - the emulator has exited; re-arm the loader (Star Wars Racer Revenge > OPTIONS > HALL OF FAME)"
        return "bad", "not listening - either the loader is not armed, or the emulator is already running"

    def auto_probe_loop(self):
        """Refreshes the loader check. Paused while a job runs, for a while
        after a script was sent, and while the payload is logging."""
        while not self.stop.wait(1.0):
            cfg = self.cfg.snapshot()
            if not cfg.get("probe_loader", True):
                continue
            ip = cfg.get("ps5_ip", "").strip()
            if not valid_ipv4(ip) or self.jobs.busy():
                continue
            log = self.log.status()
            if log["last_rx_age"] is not None and log["last_rx_age"] < 30 and                     self.payload.state.get("state") != "exited":
                continue          # the emulator is talking: it is running, do not probe
            if time.time() - self.last_script_sent < LOADER_PROBE_HOLDOFF:
                continue
            pg = self.last_ping
            if pg.get("when") and pg.get("ok") is False and                     time.time() - pg["when"] < 3 * AUTO_PING_INTERVAL:
                continue          # unreachable: probing would only time out
            last = self.last_probe
            # A listening loader is touched as rarely as possible: every empty
            # connection is something the loader has to shrug off, so once it
            # answered we leave it alone for LOADER_PROBE_RECHECK seconds.
            wait = LOADER_PROBE_RECHECK if last.get("ok") else LOADER_PROBE_INTERVAL
            if last.get("ip") == ip and last.get("when") and \
                    time.time() - last["when"] < wait:
                continue
            try:
                self.do_probe()
            except ValueError:
                pass

    def auto_ping_loop(self):
        """Keeps the reachability mark fresh without the user pressing Ping.
        Skipped while the payload is logging: that is the stronger signal."""
        while not self.stop.wait(1.0):
            cfg = self.cfg.snapshot()
            ip = cfg.get("ps5_ip", "").strip()
            if not valid_ipv4(ip):
                continue
            log = self.log.status()
            if log["last_rx_age"] is not None and log["last_rx_age"] < 30:
                continue
            last = self.last_ping
            if last.get("ip") == ip and last.get("when") and \
                    time.time() - last["when"] < AUTO_PING_INTERVAL:
                continue
            try:
                self.do_ping()
            except ValueError:
                pass

    # ---- state -----------------------------------------------------------
    def state(self, roms=True):
        cfg = self.cfg.snapshot()
        payload, cands = self.project.select_payload(cfg)
        send = self.project.find_send()
        upload = self.project.find_upload()
        st = {
            "app": {"name": APP_NAME, "version": APP_VERSION, "root": ROOT,
                    "python": sys.version.split()[0], "frozen": FROZEN,
                    "config_path": CONFIG_PATH, "tray": bool(self.tray and self.tray.available),
                    "window": self.window is not None},
            "facts": {
                "golden_bin_sha256": GOLDEN_M16C_BIN_SHA256,
                "bios_size": BIOS_SIZE, "rom_capacity": ROM_PICKER_CAPACITY,
                "console_rom_dir": CONSOLE_ROM_DIR,
                "console_bios_path": CONSOLE_BIOS_PATH,
                "console_bios_fallback": CONSOLE_BIOS_FALLBACK,
                "log_port": LOG_PORT, "loader_port": LOADER_PORT,
                "blob_ports": [BLOB_PORT_LO, BLOB_PORT_HI],
            },
            "config": {k: v for k, v in cfg.items() if k != "uploads"},
            "tools": {"send": send, "upload": upload,
                      "dialogs": self.dialogs.available},
            "payload": payload, "payload_candidates": cands,
            "bios": dict(self.project.check_bios(cfg.get("bios_path", "")), send=self.bios_pending(cfg)),
            "console": {"ip": cfg.get("ps5_ip", ""),
                        "ip_valid": valid_ipv4(cfg.get("ps5_ip", "")),
                        "ping": self.last_ping, "log": self.log.status(),
                        "auto_ping": AUTO_PING_INTERVAL,
                        "probe": self.last_probe,
                        "probe_interval": LOADER_PROBE_INTERVAL,
                        "loader": dict(zip(("state", "text"), self.loader_state())),
                        "payload": self.payload.state,
                        "summary": dict(zip(("kind", "text"), self.console_summary()))},
            "job": self.jobs.public(),
            "uploads": cfg.get("uploads", {}),
        }
        if roms:
            st["roms"] = self.project.scan_roms(cfg.get("rom_dir", ""), cfg.get("uploads", {}),
                                                self.console_scan, cfg.get("deselected", []))
        st["console_scan"] = self.console_scan
        return st

    # ---- actions ---------------------------------------------------------
    def launch(self, body):
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "").strip()
        if not valid_ipv4(ip):
            raise ValueError("Set a valid PS5 IPv4 address first")
        send = self.project.find_send()
        if not send:
            raise ValueError("tools/send.py not found next to this launcher")
        payload, _ = self.project.select_payload(cfg)
        if not payload or not payload["ok"]:
            raise ValueError("No usable payload pair (m16cgpsp.lua + m16cgpsp.bin)")
        if not payload["verified"] and not cfg.get("allow_unverified"):
            raise ValueError(
                "Payload SHA-256 does not match the frozen v0.0.1 golden value. "
                "Refusing to send. Enable 'allow unverified payload' only for development builds.")
        if self.jobs.busy():
            raise ValueError("Another job is still running")
        step = {"argv": tool_argv(send["path"], [ip, payload["lua"], payload["bin"]]),
                "title": "Launch LUAp0rt GBA -> %s" % ip,
                "result": {"payload": payload["key"], "verified": payload["verified"],
                           "kind": "launch", "local": payload["bin"], "name": "LUAp0rt GBA payload",
                           "size": payload.get("bin_size", 0) + payload.get("lua_size", 0)}}
        steps = [step]
        kind, title = "launch", step["title"]
        if body.get("with_roms", True):
            # The BIOS and the checked games go first, unless already there.
            roms, _ = self.pending_roms(cfg)
            bios = self.bios_pending(cfg)
            up = self.project.find_upload()
            if (roms or bios["needs_send"]) and up["available"]:
                self.upload_gate()
                pre = []
                if bios["needs_send"]:
                    pre.append(self._upload_step(up, ip, "bios", cfg["bios_path"], CONSOLE_BIOS_PATH,
                                                 ["--expect-size", str(BIOS_SIZE)]))
                pre += [self._upload_step(up, ip, "rom", r["path"], r["remote"], []) for r in roms]
                steps = pre + [step]
                kind = "launch_with_roms"
                what = ("BIOS" if bios["needs_send"] else "")
                if roms:
                    what += (" + " if what else "") + "%d game%s" % (len(roms), "" if len(roms) == 1 else "s")
                title = "Send %s, then launch -> %s" % (what, ip)
        job = Job(kind, title, steps, ROOT)
        self.jobs.start(job)
        return job.public()

    def upload_gate(self):
        """Uploads are scripts for the Lua loader, so they can only work while
        it is listening. Refuse, with a reason, whenever that is not proven:
        the emulator is running (the loader handed over to it), the console is
        not answering, or a probe made right now finds nothing on 9026."""
        ps = self.payload.state
        log = self.log.status()
        if ps.get("state") in ("running", "starting") or \
                (log["last_rx_age"] is not None and log["last_rx_age"] < 30 and ps.get("state") != "exited"):
            raise Blocked("emulator_running",
                          "The emulator is running on the console, so the loader is not listening and "
                          "files cannot be sent. To load ROMs: " + REARM_TEXT + ", then send again.")
        ip = self.cfg.snapshot().get("ps5_ip", "").strip()
        pg = self.last_ping
        if pg.get("ip") != ip or not pg.get("when") or time.time() - pg["when"] > AUTO_PING_INTERVAL:
            pg = self.do_ping()          # no current answer for this address: ask now
        if pg.get("ok") is False:
            raise Blocked("unreachable", "The console is not answering ping. Check that it is on and "
                                         "on the same network, then try again.")
        pr = self.last_probe
        fresh = pr.get("ok") and pr.get("when") and time.time() - pr["when"] < LOADER_PROBE_INTERVAL
        if not fresh:
            pr = self.do_probe(force=True)
        if not pr.get("ok"):
            raise Blocked("loader_down",
                          "The Lua loader is not listening on TCP 9026 (%s). Files can only be sent while it "
                          "is armed: either the loader was never started (Star Wars Racer Revenge > OPTIONS > "
                          "HALL OF FAME), or the emulator is still running - in that case %s."
                          % (pr.get("error", "no answer"), REARM_TEXT))

    def _upload_step(self, up, ip, kind, local, remote, extra):
        st = os.stat(local)
        return {"argv": tool_argv(up["path"], [ip, local, remote] + extra),
                "title": "Upload %s -> %s:%s" % (os.path.basename(local), ip, remote),
                "result": {"kind": kind, "local": local, "remote": remote,
                           "size": st.st_size, "mtime": st.st_mtime}}

    def upload(self, body):
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "").strip()
        if not valid_ipv4(ip):
            raise ValueError("Set a valid PS5 IPv4 address first")
        up = self.project.find_upload()
        if not up["available"]:
            raise ValueError("File upload is unavailable: " + up["reason"])
        kind = body.get("kind")
        resume = bool(body.get("resume"))
        if kind == "bios":
            local = cfg.get("bios_path", "")
            chk = self.project.check_bios(local)
            if not chk["ok"]:
                raise ValueError("BIOS check failed: " + chk["message"])
            remote = CONSOLE_BIOS_PATH
            extra = ["--expect-size", str(BIOS_SIZE)]
        elif kind == "rom":
            local = body.get("path", "")
            rom_dir = cfg.get("rom_dir", "")
            if not (local and os.path.isfile(local)):
                raise ValueError("ROM file not found")
            if not rom_dir or os.path.dirname(os.path.abspath(local)) != os.path.abspath(rom_dir):
                raise ValueError("ROM must come from the selected ROM folder")
            if not local.lower().endswith(ROM_EXT):
                raise ValueError("Only .gba files are listed by the console picker")
            remote = CONSOLE_ROM_DIR + "/" + os.path.basename(local)
            extra = ["--resume"] if resume else []
        else:
            raise ValueError("kind must be 'rom' or 'bios'")
        if self.jobs.busy():
            raise ValueError("Another job is still running")
        self.upload_gate()
        step = self._upload_step(up, ip, kind, local, remote, extra)
        job = Job("upload", step["title"], [step], ROOT)
        self.jobs.start(job)
        return job.public()

    def bios_pending(self, cfg):
        """The local BIOS, if verified and not known to be on the console:
        skip when this launcher already sent that exact file, or the console
        reported it found (with the right size) in this session."""
        chk = self.project.check_bios(cfg.get("bios_path", ""))
        info = {"needs_send": False, "sent_at": None, "why": ""}
        if not chk["ok"]:
            info["why"] = "no verified local BIOS"
            return info
        up = cfg.get("uploads", {}).get(CONSOLE_BIOS_PATH)
        try:
            st = os.stat(chk["path"])
        except OSError:
            return info
        if up:
            info["sent_at"] = up.get("when")
            if up.get("size") == st.st_size and abs(up.get("mtime", 0) - st.st_mtime) < 1.0:
                info["why"] = "already sent unchanged by this launcher"
                return info
        cb = (self.console_scan or {}).get("bios") or {}
        if cb.get("present") and cb.get("size_ok") is not False:
            info["why"] = "console reported it present"
            return info
        info["needs_send"] = True
        info["why"] = "not known to be on the console"
        return info

    def pending_roms(self, cfg):
        """Checked games that still have to go to the console (picker cap applies)."""
        lib = self.project.scan_roms(cfg.get("rom_dir", ""), cfg.get("uploads", {}),
                                     self.console_scan, cfg.get("deselected", []))
        if not lib["ok"]:
            raise ValueError("ROM folder: " + lib["message"])
        return [r for r in lib["roms"][:ROM_PICKER_CAPACITY] if r["checked"] and r["needs_send"]], lib

    def upload_all(self, body):
        """Send the checked games that are not on the console yet, one
        tools/upload.py run each, in the picker's order, stopping at the
        first failure."""
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "").strip()
        if not valid_ipv4(ip):
            raise ValueError("Set a valid PS5 IPv4 address first")
        up = self.project.find_upload()
        if not up["available"]:
            raise ValueError("File upload is unavailable: " + up["reason"])
        roms, lib = self.pending_roms(cfg)
        bios = self.bios_pending(cfg)
        if not roms and not bios["needs_send"]:
            if lib["checked"] == 0:
                raise ValueError("No games are checked")
            raise ValueError("Nothing to send: the BIOS and every checked game were already sent unchanged, or the console reported them present")
        if self.jobs.busy():
            raise ValueError("Another job is still running")
        self.upload_gate()
        steps = []
        if bios["needs_send"]:
            steps.append(self._upload_step(up, ip, "bios", cfg["bios_path"], CONSOLE_BIOS_PATH,
                                           ["--expect-size", str(BIOS_SIZE)]))
        steps += [self._upload_step(up, ip, "rom", r["path"], r["remote"], []) for r in roms]
        what = ("BIOS" if bios["needs_send"] else "")
        if roms:
            what += (" + " if what else "") + "%d game%s" % (len(roms), "" if len(roms) == 1 else "s")
        job = Job("upload_all", "Send %s -> %s:%s" % (what, ip, CONSOLE_ROM_DIR), steps, ROOT)
        self.jobs.start(job)
        return job.public()

    def _log_line(self, line):
        self.scan.feed(line)
        self.payload.feed(line)
        if line.startswith("Done. status=") or line.startswith("verdict:"):
            # the emulator is gone: forget the "accepted the script" evidence
            # so the loader row goes back to probing for a re-armed loader
            self.last_script_sent = 0.0
            self.last_probe = {}
        if line.startswith("M16C: BIOS"):
            upd = self.scan.bios_update()
            if upd:
                self.console_scan = upd

    def _scan_commit(self, scan):
        self.console_scan = scan

    def _job_start(self, job):
        self.last_script_sent = time.time()
        self.last_probe = {}

    def _step_start(self, job, step):
        if step.get("result", {}).get("kind") == "launch":
            self.payload.mark_sent()
            self.console_scan = {}      # a new boot will report afresh; nothing is known until then

    def _job_done(self, job, step):
        r = step["result"]
        if r.get("kind") in ("rom", "bios"):
            self.cfg.record_upload(r["remote"], {
                "name": os.path.basename(r["local"]), "size": r["size"],
                "mtime": r["mtime"], "when": time.time(),
                "sha256": self.project.cached_sha(r["local"]),
            })

    def do_ping(self):
        cfg = self.cfg.snapshot()
        ip = cfg.get("ps5_ip", "").strip()
        if not valid_ipv4(ip):
            raise ValueError("Set a valid PS5 IPv4 address first")
        with self.ping_lock:
            res = ping(ip)
            res["when"] = time.time()
            res["ip"] = ip
            self.last_ping = res
            if not res["ok"]:
                # Whatever the loader said earlier no longer holds: the console is
                # not even reachable. Forget the probe and the last send, so the
                # row goes back to "checking" and is re-probed once ping returns.
                self.last_probe = {}
                self.last_script_sent = 0.0
        return res


class ExclusiveHTTPServer(ThreadingHTTPServer):
    """One launcher per port. The stdlib default enables SO_REUSEADDR, which on
    Windows lets several processes bind the same TCP port at once; a user who
    double-clicks the exe twice then gets two launchers with two different
    in-memory settings answering the same address. Binding exclusively makes
    the second copy fail fast and simply open the first one's dashboard."""
    allow_reuse_address = False
    daemon_threads = True

    def server_bind(self):
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        ThreadingHTTPServer.server_bind(self)


class Handler(BaseHTTPRequestHandler):
    app = None  # set by serve()
    server_version = "LUAp0rtLauncher/" + APP_VERSION

    def log_message(self, fmt, *args):
        if self.app.verbose:
            BaseHTTPRequestHandler.log_message(self, fmt, *args)

    # ---- plumbing --------------------------------------------------------
    def send_json(self, obj, status=HTTPStatus.OK):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_error_json(self, msg, status=HTTPStatus.BAD_REQUEST):
        body = {"error": str(msg)}
        code = getattr(msg, "code", None)
        if code:
            body["code"] = code
            status = HTTPStatus.CONFLICT
        self.send_json(body, status)

    def read_body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        if not raw:
            return {}
        try:
            return json.loads(raw.decode("utf-8"))
        except ValueError:
            raise ValueError("request body is not valid JSON")

    def serve_static(self, name):
        if name in ("", "/"):
            name = "index.html"
        name = name.lstrip("/")
        path = os.path.normpath(os.path.join(UI_DIR, name))
        if not path.startswith(UI_DIR) or not os.path.isfile(path):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        ext = os.path.splitext(path)[1].lower()
        with open(path, "rb") as f:
            data = f.read()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", MIME.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    # ---- routes ----------------------------------------------------------
    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        app = self.app
        try:
            if u.path == "/api/state":
                return self.send_json(app.state(roms=q.get("roms", ["1"])[0] != "0"))
            if u.path == "/api/roms":
                cfg = app.cfg.snapshot()
                return self.send_json(app.project.scan_roms(cfg.get("rom_dir", ""),
                                                            cfg.get("uploads", {}),
                                                            app.console_scan, cfg.get("deselected", [])))
            if u.path == "/api/log":
                since = int(q.get("since", ["0"])[0])
                lines, nxt = app.log.since(since)
                return self.send_json({"lines": [{"i": i, "t": t, "s": s} for i, t, s in lines],
                                       "next": nxt, "status": app.log.status()})
            if u.path == "/api/log/download":
                data = app.log.dump().encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Disposition",
                                 'attachment; filename="luap0rt-console-%s.log"'
                                 % time.strftime("%Y%m%d-%H%M%S"))
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            if u.path == "/api/job":
                since = int(q.get("since", ["0"])[0])
                return self.send_json({"job": app.jobs.public(since)})
            if u.path.startswith("/api/"):
                return self.send_error_json("unknown endpoint", HTTPStatus.NOT_FOUND)
            return self.serve_static(u.path)
        except ValueError as e:
            return self.send_error_json(e)
        except Exception as e:  # surface, never hang the UI
            return self.send_error_json("%s: %s" % (type(e).__name__, e),
                                        HTTPStatus.INTERNAL_SERVER_ERROR)

    def receive_rom(self, q):
        """POST /api/roms/add?name=<file>[&overwrite=1] with the raw file as
        the body: written into the ROM folder, nothing else. Only .gba, only
        a bare file name, at most 64 MB (a GBA cartridge is at most 32 MB)."""
        app = self.app
        n = int(self.headers.get("Content-Length") or 0)

        def refuse(exc):
            # The browser is still sending the body; if we answer and close
            # now it sees a broken connection instead of our message. Drain
            # (bounded) first, then refuse.
            left = min(n, 64 * 1024 * 1024)
            while left > 0:
                chunk = self.rfile.read(min(1 << 20, left))
                if not chunk:
                    break
                left -= len(chunk)
            raise exc

        cfg = app.cfg.snapshot()
        rom_dir = cfg.get("rom_dir", "")
        if not rom_dir or not os.path.isdir(rom_dir):
            refuse(ValueError("Choose a ROM folder first"))
        name = os.path.basename(q.get("name", [""])[0].strip())
        if not name or name in (".", "..") or "/" in name or "\\" in name:
            refuse(ValueError("Bad file name"))
        if not name.lower().endswith(ROM_EXT):
            refuse(ValueError("%s is not a .gba file; the console only lists .gba" % name))
        if n <= 0:
            raise ValueError("Empty upload")
        if n > 64 * 1024 * 1024:
            refuse(ValueError("File too large for a GBA ROM (%s)" % human_size(n)))
        dest = os.path.join(rom_dir, name)
        overwrite = q.get("overwrite", ["0"])[0] == "1"
        if os.path.exists(dest) and not overwrite:
            refuse(Blocked("exists", "%s is already in the folder" % name))
        tmp = dest + ".part"
        left = n
        with open(tmp, "wb") as f:
            while left > 0:
                chunk = self.rfile.read(min(1 << 20, left))
                if not chunk:
                    break
                f.write(chunk)
                left -= len(chunk)
        if left:
            os.remove(tmp)
            raise ValueError("Upload ended early")
        os.replace(tmp, dest)
        return {"ok": True, "name": name, "size": n, "path": dest}

    def do_POST(self):
        u = urlparse(self.path)
        app = self.app
        try:
            if u.path == "/api/roms/add":
                return self.send_json(self.receive_rom(parse_qs(u.query)))
            body = self.read_body()
            if u.path == "/api/config":
                patch = {}
                for k in ("ps5_ip", "rom_dir", "bios_path", "payload",
                          "custom_lua", "custom_bin", "allow_unverified", "probe_loader",
                          "setup_done", "deselected"):
                    if k in body:
                        patch[k] = body[k]
                if "ps5_ip" in patch:
                    patch["ps5_ip"] = str(patch["ps5_ip"]).strip()
                    if patch["ps5_ip"] and not valid_ipv4(patch["ps5_ip"]):
                        raise ValueError("'%s' is not a valid IPv4 address" % patch["ps5_ip"])
                if "allow_unverified" in patch:
                    patch["allow_unverified"] = bool(patch["allow_unverified"])
                if "probe_loader" in patch:
                    patch["probe_loader"] = bool(patch["probe_loader"])
                    app.last_probe = {}
                if "setup_done" in patch:
                    patch["setup_done"] = bool(patch["setup_done"])
                if "deselected" in patch:
                    if not isinstance(patch["deselected"], list):
                        raise ValueError("deselected must be a list of file names")
                    patch["deselected"] = sorted({str(x) for x in patch["deselected"]})
                app.cfg.update(patch)
                return self.send_json({"ok": True, "state": app.state()})
            if u.path == "/api/launch":
                return self.send_json({"ok": True, "job": app.launch(body)})
            if u.path == "/api/upload":
                return self.send_json({"ok": True, "job": app.upload(body)})
            if u.path == "/api/upload_all":
                return self.send_json({"ok": True, "job": app.upload_all(body)})
            if u.path == "/api/quit":
                app.stop.set()
                return self.send_json({"ok": True})
            if u.path == "/api/console_scan/clear":
                app.console_scan = {}
                return self.send_json({"ok": True})
            if u.path == "/api/upload/forget":
                app.cfg.forget_upload(str(body.get("remote", "")))
                return self.send_json({"ok": True})
            if u.path == "/api/job/cancel":
                return self.send_json({"ok": app.jobs.cancel()})
            if u.path == "/api/ping":
                return self.send_json({"ok": True, "ping": app.do_ping()})
            if u.path == "/api/probe":
                return self.send_json({"ok": True, "probe": app.do_probe(force=True),
                                       "loader": dict(zip(("state", "text"), app.loader_state()))})
            if u.path == "/api/log/clear":
                app.log.clear()
                return self.send_json({"ok": True})
            if u.path == "/api/browse":
                kind = body.get("kind", "file")
                if kind not in ("dir", "file", "bios"):
                    raise ValueError("kind must be dir, file or bios")
                if not app.dialogs.available:
                    raise ValueError("Native file dialogs are unavailable (tkinter missing); "
                                     "type the path instead")
                res = app.dialogs.request(kind, body.get("title", "Select"),
                                          body.get("initial", ""))
                return self.send_json({"ok": True, "path": res})
            if u.path == "/api/open":
                target = str(body.get("path", ""))
                if not target or not os.path.exists(target):
                    raise ValueError("path does not exist")
                open_in_file_manager(target)
                return self.send_json({"ok": True})
            return self.send_error_json("unknown endpoint", HTTPStatus.NOT_FOUND)
        except ValueError as e:
            return self.send_error_json(e)
        except Exception as e:
            return self.send_error_json("%s: %s" % (type(e).__name__, e),
                                        HTTPStatus.INTERNAL_SERVER_ERROR)


def open_in_file_manager(path):
    if platform.system().lower().startswith("win"):
        os.startfile(path)  # noqa: S606 - user-initiated, local path
    elif sys.platform == "darwin":
        subprocess.Popen(["open", path])
    else:
        subprocess.Popen(["xdg-open", path])


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
def url_for(host, port):
    return "http://%s:%d/" % (host, port)


def run_tool(argv):
    """--run-tool <script> [args...]: execute one of the frozen tools in this
    process, with this (possibly bundled) interpreter. Output goes straight to
    the parent launcher's pipe, line-buffered."""
    import runpy
    if not argv:
        raise SystemExit("--run-tool needs a script path")
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except (AttributeError, ValueError):
        pass
    sys.argv = list(argv)
    runpy.run_path(argv[0], run_name="__main__")


def serve(host, port, open_browser, verbose, show_window=True):
    say("---- start: %s ----" % " ".join(sys.argv))
    app = App(verbose=verbose)
    Handler.app = app

    threading.Thread(target=udp_log_thread, args=(app.log, app.stop),
                     daemon=True, name="udp-log").start()
    threading.Thread(target=app.auto_ping_loop, daemon=True, name="auto-ping").start()
    threading.Thread(target=app.auto_probe_loop, daemon=True, name="auto-probe").start()

    try:
        httpd = ExclusiveHTTPServer((host, port), Handler)
    except OSError as e:
        say("launcher: cannot bind http://%s:%d (%s). Is another launcher already "
            "running? If so, its dashboard is at %s -- opening it." % (host, port, e, url_for(host, port)))
        if open_browser:
            webbrowser.open(url_for(host, port))
        raise SystemExit(1)
    httpd.daemon_threads = True
    threading.Thread(target=httpd.serve_forever, daemon=True, name="http").start()

    url = url_for(host, port)
    say("%s v%s%s" % (APP_NAME, APP_VERSION, " (exe)" if FROZEN else ""))
    say("  project root : %s" % ROOT)
    say("  dashboard    : %s" % url)
    say("  console log  : listening on udp/%d" % LOG_PORT)
    if app.log.bind_error:
        say("  WARNING      : %s" % app.log.bind_error)
    say("  config       : %s" % CONFIG_PATH)
    if show_window and app.dialogs.available:
        try:
            app.window = StatusWindow(app, url)
            app.dialogs.window = app.window
            say("  window       : status window open (taskbar button)")
        except Exception as e:
            app.window = None
            say("  window       : unavailable (%s)" % e)
    app.tray = Tray(app, url)
    if app.tray.start():
        say("  tray icon    : right-click for Open dashboard / Quit")
    else:
        say("  tray icon    : not available (pip install pystray pillow)")
    say("  quit         : Ctrl+C here, the Quit button in the dashboard, or the tray menu")
    if open_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    def status_loop():
        while not app.stop.wait(2.0):
            if app.tray:
                app.tray.update(*app.console_summary())
    threading.Thread(target=status_loop, daemon=True, name="tray-status").start()

    try:
        if app.window is not None:
            app.window.run()         # main thread: Tk loop; dialogs run inside it
        else:
            app.dialogs.pump(app.stop)   # main thread: native dialogs live here
    except KeyboardInterrupt:
        pass
    finally:
        app.stop.set()
        app.jobs.cancel()
        if app.tray:
            app.tray.stop()
        httpd.shutdown()
        say("launcher: bye")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--run-tool":
        run_tool(sys.argv[2:])
        return 0
    ap = argparse.ArgumentParser(description=APP_NAME)
    ap.add_argument("--host", default="127.0.0.1", help="UI bind address (default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=8765, help="UI port (default 8765)")
    ap.add_argument("--no-browser", action="store_true", help="do not open the browser")
    ap.add_argument("--verbose", action="store_true", help="log HTTP requests")
    ap.add_argument("--no-window", action="store_true",
                    help="no status window (tray icon and dashboard only)")
    ap.add_argument("--config", default="",
                    help="use this settings file instead of the per-user one (testing)")
    ap.add_argument("--log-port", type=int, default=0,
                    help="listen for the console log on this UDP port instead of 9027 (testing)")
    a = ap.parse_args()
    global CONFIG_PATH, LOG_PORT, CONFIG_OVERRIDDEN
    if a.config:
        CONFIG_PATH = os.path.abspath(a.config)
        CONFIG_OVERRIDDEN = True
    if a.log_port:
        LOG_PORT = a.log_port
    if a.host not in ("127.0.0.1", "localhost", "::1"):
        say("launcher: refusing to bind %s -- the dashboard is local-only by design"
            % a.host)
        return 2
    serve(a.host, a.port, not a.no_browser, a.verbose, show_window=not a.no_window)
    return 0


if __name__ == "__main__":
    sys.exit(main())
