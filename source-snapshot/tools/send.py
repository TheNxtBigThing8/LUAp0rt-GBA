#!/usr/bin/env python3
"""Send a LUAport payload in two stages: script to 9026, blob to 9028-9045.

    python tools/send.py <PS5_IP> <script.lua> <blob.bin>

Adapted from LuaPSX/send2.py, unchanged in protocol. It exists as a LUAport
file so the port can be delivered without the reference tree present; LuaPSX/
stays read-only and its copy still works identically.

Luac0re's remote lua loader caps scripts at 500KB, and embedding the blob as
hex doubles it -- so anything past ~240KB of payload has to arrive separately.
The script binds a TCP port in the 9028-9045 range and waits; this connects and
streams the raw blob.

The blob port is a RANGE, not a constant: a script that dies after bind()
leaves its listener behind for the rest of the game session, so a fixed port
would mean one crash costs a game relaunch. The receiver walks the range and
this walks it too, identifying the live receiver by the ACK byte it sends.

UDP 9027 is the log port. That is not a clash with the TCP ports above: the two
protocols do not share a port namespace.
"""

import socket
import sys
import time

PAYLOAD_PORT = 9026
BLOB_PORT_LO, BLOB_PORT_HI = 9028, 9045


def send_script(host, path):
    data = open(path, "rb").read()
    s = socket.create_connection((host, PAYLOAD_PORT), timeout=15)
    try:
        s.sendall(data)
        s.shutdown(socket.SHUT_WR)
    finally:
        s.close()
    print("script: %d bytes -> %s:%d" % (len(data), host, PAYLOAD_PORT))


def send_blob(host, path, tries=40):
    """Find the live receiver by its ACK, then stream the blob.

    A listener leaked by a dead script still accepts and buffers, so a
    successful connect proves nothing -- it only resets once its buffer fills
    part-way through the transfer. The ACK is the discriminator.
    """
    data = open(path, "rb").read()
    for _ in range(tries):
        for port in range(BLOB_PORT_LO, BLOB_PORT_HI + 1):
            try:
                s = socket.create_connection((host, port), timeout=5)
            except OSError:
                continue
            try:
                s.settimeout(4)
                if s.recv(1) != b"K":
                    raise OSError("no ACK")
                s.settimeout(300)
                s.sendall(data)
                s.shutdown(socket.SHUT_WR)
            except OSError:
                s.close()
                continue
            s.close()
            print("blob  : %d bytes -> %s:%d" % (len(data), host, port))
            return True
        time.sleep(0.25)
    print("blob  : no live receiver on %d-%d" % (BLOB_PORT_LO, BLOB_PORT_HI))
    return False


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    host, script, blob = sys.argv[1], sys.argv[2], sys.argv[3]
    send_script(host, script)
    if not send_blob(host, blob):
        raise SystemExit(1)
    print("sent. payload should be running.")


if __name__ == "__main__":
    main()
