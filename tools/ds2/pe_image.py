"""Read-only view of a PE file laid out by RVA, for static scans of Decima executables.

Pointers in initialized data are absolute VAs at the preferred image base (the loader relocates them at run time),
so a static scan can follow them without running the game.
"""
import struct

import pefile


class PeImage:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.image = bytearray(self.pe.get_memory_mapped_image())
        self.sections = {s.Name.rstrip(b"\0").decode(): (s.VirtualAddress, s.Misc_VirtualSize) for s in self.pe.sections}

    def section(self, name):
        rva, size = self.sections[name]
        return self.base + rva, self.base + rva + size

    def contains(self, va):
        return self.base <= va < self.base + len(self.image)

    def in_section(self, va, *names):
        return any(lo <= va < hi for lo, hi in (self.section(n) for n in names if n in self.sections))

    def read(self, va, size):
        off = va - self.base
        return bytes(self.image[off:off + size])

    def u8(self, va):
        return self.image[va - self.base]

    def u16(self, va):
        return struct.unpack_from("<H", self.image, va - self.base)[0]

    def u32(self, va):
        return struct.unpack_from("<I", self.image, va - self.base)[0]

    def i32(self, va):
        return struct.unpack_from("<i", self.image, va - self.base)[0]

    def ptr(self, va):
        return struct.unpack_from("<Q", self.image, va - self.base)[0]

    def cstr(self, va, limit=256):
        if not self.contains(va):
            return None
        off = va - self.base
        end = self.image.find(b"\0", off, off + limit)
        if end < 0:
            return None
        text = bytes(self.image[off:end]).decode("latin-1")
        return text if text.isascii() and text.isprintable() else None
