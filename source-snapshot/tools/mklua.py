#!/usr/bin/env python3
"""Package a payload for the network loader.

    python mklua.py <template.lua.in> <blob.bin> <out.lua> [blob.elf]

Unlike bin2lua.py this does NOT embed the blob. Luac0re's remote lua loader
caps scripts at 500KB and hex encoding doubles the payload, so an embedded
blob tops out around 240KB -- a 663KB script was accepted by the socket and
then silently never executed. The blob is sent separately over TCP instead,
which lifts the ceiling to 4MB.

Only @@JIT_SIZE@@ is substituted; the blob travels on its own.
"""
import os
import subprocess
import sys

PAGE = 0x4000


def linker_syms(elf, names):
    out = subprocess.run(["readelf", "-sW", elf], capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[7] in names:
            syms[parts[7]] = int(parts[1], 16)
    missing = [n for n in names if n not in syms]
    if missing:
        raise SystemExit("mklua: missing linker symbols: %s" % ", ".join(missing))
    return syms


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    template, binary, out = sys.argv[1], sys.argv[2], sys.argv[3]
    elf = sys.argv[4] if len(sys.argv) > 4 else os.path.splitext(binary)[0] + ".elf"

    blob = open(binary, "rb").read()
    syms = linker_syms(elf, ["__data_start", "__bss_end"])
    data_start, bss_end = syms["__data_start"], syms["__bss_end"]

    # The reservation runs to __data_start so that .data's initial image is
    # inside a mapping when _start copies it out; everything above is anonymous
    # RW that _start maps itself.
    jit_size = (data_start + PAGE - 1) & ~(PAGE - 1)

    if len(blob) > data_start:
        raise SystemExit(
            "mklua: blob (%d B) overruns __data_start (0x%X) -- the .data init "
            "image would be clobbered by the RW mapping." % (len(blob), data_start))

    text = open(template, encoding="utf-8").read()

    # The console logs to the PC over UDP, so the template carries the PC's
    # address. Getting it wrong produces a run with NO log at all, which is
    # indistinguishable from a payload that never started -- so it comes from
    # the build rather than being edited by hand each time the network moves.
    # The template defaults to "auto", which makes the console derive the log
    # destination from its own subnet -- no configuration, works anywhere. An
    # explicit PC_IP overrides that for anyone who wants a unicast log.
    pc_ip = os.environ.get("PC_IP", "auto")
    if pc_ip and pc_ip != "auto":
        text = text.replace('local PC_IP    = "auto"',
                            'local PC_IP    = "%s"' % pc_ip, 1)
        print("  klog -> %s (explicit)" % pc_ip)
    else:
        print("  klog -> auto (console's own subnet broadcast)")
    if "@@JIT_SIZE@@" not in text:
        raise SystemExit("mklua: template is missing @@JIT_SIZE@@")
    text = text.replace("@@JIT_SIZE@@", "0x%X" % jit_size)

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    CHUNK = 0x40000
    nmaps = (jit_size + CHUNK - 1) // CHUNK
    print("mklua: %s" % out)
    print("  blob      %8d B  (sent separately over TCP 9027)" % len(blob))
    print("  reserve   0x%X -> %d JIT mapping(s) of 0x%X" % (jit_size, nmaps, CHUNK))
    print("  RW region 0x%X..0x%X  mapped by _start" % (data_start, bss_end))
    print("  lua       %8d B  (limit 500KB)" % len(text))
    if len(text) > 500 * 1024:
        raise SystemExit("mklua: script exceeds the 500KB remote-loader limit")


if __name__ == "__main__":
    main()
