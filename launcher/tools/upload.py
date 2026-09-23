#!/usr/bin/env python3
"""Upload a user-supplied file into the console sandbox.

    python tools/upload.py <PS5_IP> <local-file> <remote-path> [options]

      --expect-size N   refuse to send unless the local file is exactly N bytes
      --resume          append to whatever is already there instead of replacing
      --tries N         connection attempts before giving up (default 10)

The M3 use is a GBA BIOS:

    python tools/upload.py 192.168.1.50 gba_bios.bin /temp0/gba_bios.bin \\
        --expect-size 16384

or, equivalently:

    make m3-upload-bios PS5_IP=192.168.1.50 BIOS=gba_bios.bin

NO BIOS IS EMBEDDED ANYWHERE IN LUAport. This tool exists precisely so that the
BIOS stays a user-supplied file that travels out of band, as a separate explicit
action, and never becomes bytes inside the image or inside a delivery script.
gpsp/bios_data.S is excluded from every build for the same reason.

Ported from LuaPSX/upload_disc.py, which pairs with LuaPSX/lua/upload_resume.lua.
LuaPSX/ is read-only reference and its copy still works identically; this is an
additive LUAport-owned file so the port can be delivered without the reference
tree present. The transport is unchanged: the script goes to TCP 9026, then the
data connection walks TCP 9028-9045 looking for the live receiver's ACK.

--------------------------------------------------------------------------
THE ONE DELIBERATE PROTOCOL CHANGE FROM LuaPSX, AND WHY
--------------------------------------------------------------------------
LuaPSX's uploader is resume-ONLY: its receiver opens without O_TRUNC and the
sender skips whatever is already present. That is exactly right for the ~1GB
disc images it was built for, where three of four transfers died mid-flight and
restarting from zero did not converge.

It is WRONG as the default for a 16KB BIOS. Resume-only means that replacing a
BIOS with a DIFFERENT one of the same length silently does nothing: the receiver
reports 16384 bytes already present, the sender sees `have >= size` and returns
"complete" without transferring a byte. The user would be left running their old
BIOS while being told the upload succeeded.

So the header carries a flags word and the receiver honours a truncate bit:

    ->  u16 path_len (LE), u16 flags (LE), path bytes
    <-  'K', u64 (LE) offset to resume from
    ->  raw bytes from that offset until EOF

    flags bit 0 (0x0001) = open with O_TRUNC

TRUNCATE IS THE DEFAULT because a 16KB file needs no resume machinery and
correctness matters more. --resume opts back into append semantics and is what
M4 will want for multi-megabyte ROMs. Both ends of this protocol are LUAport's
own (tools/upload.py and lua/upload.lua), so the change is self-contained and
does not affect LuaPSX.

--------------------------------------------------------------------------
INTEGRITY REPORTING
--------------------------------------------------------------------------
The FNV-1a 32 printed here is computed with EXACTLY the algorithm in
adapters/gba/gba_bios.c:225-234, over the whole local file. The M3 fixture
prints the same hash over bios_rom at step 52, so the two can be compared by eye
for a genuine end-to-end integrity check across the transfer, the filesystem and
gpSP's own load_bios().

It is REPORTED, NEVER ASSERTED, on both sides. No hash is pinned: doing so would
reject the GPL2 open-source replacement BIOS, which is a perfectly valid input,
and would hard-code a fingerprint of copyrighted data this project does not
distribute.
"""

import argparse
import os
import socket
import struct
import sys
import time

LOADER_PORT = 9026
DATA_PORT_LO, DATA_PORT_HI = 9028, 9045
CHUNK = 256 * 1024

FLAG_TRUNC = 0x0001


def fnv1a32(data):
    """FNV-1a 32-bit -- byte-identical to adapters/gba/gba_bios.c:bios_fnv1a."""
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def send_script(host, path):
    data = open(path, "rb").read()
    s = socket.create_connection((host, LOADER_PORT), timeout=15)
    try:
        s.sendall(data)
        s.shutdown(socket.SHUT_WR)
    finally:
        s.close()


def attempt(host, local, remote, size, flags):
    """One connection. Returns the byte count on the console, or -1 if no
    live receiver answered anywhere in the port range."""
    rp = remote.encode()
    header = struct.pack("<HH", len(rp), flags) + rp

    for port in range(DATA_PORT_LO, DATA_PORT_HI + 1):
        try:
            s = socket.create_connection((host, port), timeout=5)
        except OSError:
            continue
        try:
            s.sendall(header)
            s.settimeout(10)
            reply = b""
            while len(reply) < 9:
                b = s.recv(9 - len(reply))
                if not b:
                    raise OSError("short reply")
                reply += b
            # A listener leaked by a dead script accepts and buffers without
            # complaint, then resets part-way through. The ACK is the only
            # reliable discriminator -- a successful connect proves nothing.
            if reply[0:1] != b"K":
                raise OSError("no ACK")
            have = struct.unpack("<Q", reply[1:9])[0]
        except OSError:
            s.close()
            continue

        print("  port %d: console has %d bytes, continuing from there"
              % (port, have), flush=True)
        if have >= size:
            s.close()
            return have

        s.settimeout(600)
        sent = have
        t0 = time.time()
        last = 0.0
        try:
            with open(local, "rb") as f:
                f.seek(have)
                while True:
                    b = f.read(CHUNK)
                    if not b:
                        break
                    s.sendall(b)
                    sent += len(b)
                    now = time.time()
                    if now - last > 3.0:
                        el = now - t0
                        rate = (sent - have) / el / 1e6 if el > 0.2 else 0.0
                        print("    %d / %d bytes  %.1f MB/s"
                              % (sent, size, rate), flush=True)
                        last = now
            s.shutdown(socket.SHUT_WR)
        except OSError as e:
            print("    dropped at %d bytes (%s)" % (sent, type(e).__name__),
                  flush=True)
        finally:
            try:
                s.close()
            except OSError:
                pass
        return sent

    return -1


def main():
    ap = argparse.ArgumentParser(
        description="Upload a file into the console sandbox.")
    ap.add_argument("host", help="console address")
    ap.add_argument("local", help="local file to send")
    ap.add_argument("remote", help="absolute path on the console")
    ap.add_argument("--expect-size", type=int, default=0,
                    help="refuse to send unless the file is exactly this many bytes")
    ap.add_argument("--resume", action="store_true",
                    help="append to an existing partial file instead of replacing it")
    ap.add_argument("--tries", type=int, default=10,
                    help="connection attempts before giving up (default 10)")
    args = ap.parse_args()

    if not os.path.isfile(args.local):
        raise SystemExit("upload: no such file: %s" % args.local)

    size = os.path.getsize(args.local)

    # The PC-side gate. This is a CONVENIENCE, not the authority: the binding
    # size check is the one apps/m3gpsp/main.c performs at step 50, on the
    # console, against the file it actually opened. Checking here just avoids
    # spending a transfer on a file that cannot possibly pass.
    if args.expect_size and size != args.expect_size:
        raise SystemExit(
            "upload: %s is %d bytes, expected exactly %d.\n"
            "        A GBA BIOS is 16384 bytes. A file of any other size is\n"
            "        rejected by the M3 fixture's step-50 gate anyway, because\n"
            "        gpSP's load_bios() reads 0x4000 bytes blind and discards\n"
            "        filestream_read's return value -- so a short file would be\n"
            "        loaded partially and still report success."
            % (args.local, size, args.expect_size))

    blob = open(args.local, "rb").read()

    here = os.path.dirname(os.path.abspath(__file__))
    lua = os.path.join(os.path.dirname(here), "lua", "upload.lua")
    if not os.path.isfile(lua):
        raise SystemExit("upload: receiver script not found: %s" % lua)

    flags = 0 if args.resume else FLAG_TRUNC

    print("upload %s -> %s:%s" % (args.local, args.host, args.remote))
    print("  %d bytes, fnv1a=0x%08X, mode=%s"
          % (size, fnv1a32(blob), "resume" if args.resume else "replace"))
    print("  (the M3 fixture prints the same fnv1a at step 52 -- compare them)")

    done = 0
    for tries in range(1, args.tries + 1):
        # A wedged loader accepts the connection and then resets. That is a
        # failed attempt, not a reason to abandon the upload with a traceback.
        try:
            send_script(args.host, lua)
        except OSError as e:
            print("  attempt %d: loader did not take the script (%s)"
                  % (tries, type(e).__name__), flush=True)
            time.sleep(2.0)
            continue

        # The receiver needs a moment to bind and reach its accept() loop.
        time.sleep(1.2)

        got = attempt(args.host, args.local, args.remote, size, flags)
        if got < 0:
            print("  attempt %d: no live receiver on %d-%d"
                  % (tries, DATA_PORT_LO, DATA_PORT_HI), flush=True)
            time.sleep(1.0)
            continue
        if got >= size:
            print("complete: %d bytes at %s" % (size, args.remote))
            print("now send the payload:  make m3-send PS5_IP=%s" % args.host)
            return 0
        if got <= done:
            print("  attempt %d made no progress (%d bytes)" % (tries, got))
        done = got
        print("  attempt %d ended at %d bytes, continuing" % (tries, got),
              flush=True)

        # Every later attempt must resume, whatever the initial mode was.
        # Re-truncating on a retry would restart from zero forever.
        flags = 0
        time.sleep(1.0)

    print("gave up short of the full file")
    return 1


if __name__ == "__main__":
    sys.exit(main())
