#!/usr/bin/env python3
"""Minimal ELF64 reader for the LUAport offline tooling.

LuaPSX's tools shell out to `readelf` and parse its text output. That works on
the Linux/WSL box the payload was developed on, but it makes every measurement
depend on binutils being installed AND on readelf's column layout staying put.

The size report is the input to M1's GO/NO-GO decision, so it is worth having it
depend on nothing but the file itself. This module parses the container
directly: section headers and the symbol table, which is all the measurements
need.

Only the subset LUAport uses is implemented -- little-endian ELF64 on x86-64.
"""

import struct


class ElfError(Exception):
    pass


class Elf64:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.blob = f.read()

        if len(self.blob) < 64 or self.blob[:4] != b"\x7fELF":
            raise ElfError("%s: not an ELF file" % path)
        if self.blob[4] != 2:
            raise ElfError("%s: not ELF64" % path)
        if self.blob[5] != 1:
            raise ElfError("%s: not little-endian" % path)

        self.e_type = struct.unpack_from("<H", self.blob, 0x10)[0]
        self.e_machine = struct.unpack_from("<H", self.blob, 0x12)[0]
        e_shoff = struct.unpack_from("<Q", self.blob, 0x28)[0]
        e_shentsize = struct.unpack_from("<H", self.blob, 0x3A)[0]
        e_shnum = struct.unpack_from("<H", self.blob, 0x3C)[0]
        e_shstrndx = struct.unpack_from("<H", self.blob, 0x3E)[0]

        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
             sh_link, sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
                "<IIQQQQIIQQ", self.blob, off)
            self.sections.append({
                "name_off": sh_name, "type": sh_type, "flags": sh_flags,
                "addr": sh_addr, "offset": sh_offset, "size": sh_size,
                "link": sh_link, "info": sh_info, "align": sh_addralign,
                "entsize": sh_entsize, "name": "",
            })

        if e_shstrndx < len(self.sections):
            strtab = self.sections[e_shstrndx]
            base = strtab["offset"]
            for s in self.sections:
                s["name"] = self._cstr(base + s["name_off"])

        self._symbols = None

    def _cstr(self, off):
        end = self.blob.find(b"\x00", off)
        if end < 0:
            return ""
        return self.blob[off:end].decode("utf-8", "replace")

    @property
    def is_dyn(self):
        return self.e_type == 3          # ET_DYN

    @property
    def type_name(self):
        return {0: "NONE", 1: "REL", 2: "EXEC", 3: "DYN", 4: "CORE"}.get(
            self.e_type, "0x%X" % self.e_type)

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def symbols(self):
        """Every symbol in .symtab as a list of dicts."""
        if self._symbols is not None:
            return self._symbols

        out = []
        for sec in self.sections:
            if sec["type"] != 2:          # SHT_SYMTAB
                continue
            strtab = self.sections[sec["link"]]
            n = sec["size"] // 24 if sec["size"] else 0
            for i in range(n):
                off = sec["offset"] + i * 24
                st_name, st_info, st_other, st_shndx, st_value, st_size = \
                    struct.unpack_from("<IBBHQQ", self.blob, off)
                out.append({
                    "name": self._cstr(strtab["offset"] + st_name),
                    "info": st_info,
                    "bind": st_info >> 4,
                    "type": st_info & 0xF,
                    "shndx": st_shndx,
                    "value": st_value,
                    "size": st_size,
                })
        self._symbols = out
        return out

    def symbol_values(self, names):
        """{name: value} for the requested linker symbols."""
        want = set(names)
        out = {}
        for s in self.symbols():
            if s["name"] in want:
                out[s["name"]] = s["value"]
        return out

    def relocations(self, start_addr, end_addr):
        """Parse ELF64 Rela entries living in [start_addr, end_addr).

        Returns a list of (r_offset, r_type, r_addend). The table is located by
        virtual address because linker.ld names its bounds with symbols rather
        than relying on the section surviving under a predictable name.
        """
        if end_addr <= start_addr:
            return []

        host = None
        for s in self.sections:
            if s["size"] and s["addr"] <= start_addr < s["addr"] + s["size"]:
                host = s
                break
        if host is None:
            raise ElfError("could not locate the section holding 0x%X" % start_addr)

        file_off = host["offset"] + (start_addr - host["addr"])
        out = []
        for i in range((end_addr - start_addr) // 24):
            r_offset, r_info, r_addend = struct.unpack_from(
                "<QQq", self.blob, file_off + i * 24)
            out.append((r_offset, r_info & 0xFFFFFFFF, r_addend))
        return out

    def section_of(self, addr):
        for s in self.sections:
            if s["size"] and s["addr"] <= addr < s["addr"] + s["size"]:
                return s["name"]
        return "(outside sections)"
