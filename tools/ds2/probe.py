"""Read-only runtime probe for Death Stranding 2 (x64 Decima, ASLR). Never writes game memory.

Class vtables come from the exe's MSVC RTTI (static, see disasm.py) and are rebased onto the live module.

usage: python probe.py base                         # live module base and slide
       python probe.py instances <Class> [max] [--near <hex addr>]
                                                    # heap objects whose first qword is the class's vtable; --near
                                                    # scans only the allocation holding that address (seconds, not
                                                    # minutes: entities share the player entity's heap)
       python probe.py refs <hex value> [max]       # where that qword is stored (image .data shown as file VA)
       python probe.py dump <hex addr> [size]       # qwords with the class of any object they point to, and doubles
       python probe.py entity <hex addr>            # Decima Entity world transform (position doubles, rotation rows)
       python probe.py handlers <Type>              # a reflected type's message handlers as registered in the live game
                                                    # (most handler tables are filled at start-up, not in the file)
       python probe.py snap <hex addr> <size> <file>
       python probe.py diff <file_a> <file_b>       # changed qwords between two snaps (as hex and double)
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

import numpy as np

import disasm
from pe_image import PeImage

PROCESS_NAME = "DS2.exe"
EXE = r"G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH\DS2.exe"
PROCESS_VM_READ, PROCESS_QUERY_INFORMATION = 0x0010, 0x0400
MEM_COMMIT, MEM_PRIVATE = 0x1000, 0x20000
READABLE = {0x02, 0x04, 0x20, 0x40}
USER_SPACE_END = 0x7FFF_FFFF_0000
CHUNK = 64 << 20
DEFAULT_DUMP = 0x200
DEFAULT_MAX_HITS = 32
ENTITY_TRANSFORM = 0xE8  # Entity.Orientation (WorldTransform): double position[3], then float rotation[3][3]

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class MEMORY_BASIC_INFORMATION64(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_ulonglong), ("AllocationBase", ctypes.c_ulonglong),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_ulonglong),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


def find_pid():
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.strip('"').split('","')
        if len(parts) > 1 and parts[0].lower() == PROCESS_NAME.lower():
            return int(parts[1])
    sys.exit(f"{PROCESS_NAME} is not running")


class Proc:
    def __init__(self, pid):
        self.h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h:
            sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")
        self.base = self.module_base()

    def module_base(self):
        modules = (ctypes.c_void_p * 1024)()
        needed = wt.DWORD()
        psapi.EnumProcessModulesEx(self.h, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3)
        return modules[0]

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
            return b""
        return buf.raw[:n.value]

    def ptr(self, addr):
        b = self.read(addr, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else 0

    def regions(self, private_only=True):
        addr, mbi = 0, MEMORY_BASIC_INFORMATION64()
        while addr < USER_SPACE_END and k32.VirtualQueryEx(self.h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            if mbi.State == MEM_COMMIT and (mbi.Protect & 0xFF) in READABLE and (mbi.Type == MEM_PRIVATE or not private_only):
                yield mbi.BaseAddress, mbi.RegionSize
            addr = mbi.BaseAddress + mbi.RegionSize

    def allocation_regions(self, address):
        """Committed readable regions of the allocation that contains address."""
        mbi = MEMORY_BASIC_INFORMATION64()
        k32.VirtualQueryEx(self.h, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi))
        allocation = mbi.AllocationBase
        return [(b, sz) for b, sz in self.regions() if self.allocation_of(b) == allocation]

    def allocation_of(self, address):
        mbi = MEMORY_BASIC_INFORMATION64()
        k32.VirtualQueryEx(self.h, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi))
        return mbi.AllocationBase

    def find_qword(self, value, limit, include_image=False, regions=None):
        hits = []
        for base, size in regions if regions is not None else self.regions(private_only=not include_image):
            for off in range(0, size, CHUNK):
                blob = self.read(base + off, min(CHUNK, size - off))
                if len(blob) < 8:
                    continue
                words = np.frombuffer(blob[:len(blob) // 8 * 8], dtype=np.uint64)
                for i in np.nonzero(words == value)[0]:
                    hits.append(base + off + int(i) * 8)
                    if len(hits) >= limit:
                        return hits
        return hits


class Session:
    def __init__(self):
        self.proc = Proc(find_pid())
        self.img = PeImage(EXE)
        self.slide = self.proc.base - self.img.base

    def to_file(self, live):
        return live - self.slide

    def class_of(self, obj):
        vt = self.proc.ptr(obj)
        file_vt = self.to_file(vt)
        if not self.img.in_section(file_vt, ".rdata"):
            return None
        return disasm.class_of_vtable(self.img, file_vt)


def cmd_instances(s, name, limit, near=None):
    regions = s.proc.allocation_regions(near) if near else None
    for vt, offset in disasm.vtables(s.img, name):
        live = vt + s.slide
        hits = s.proc.find_qword(live, limit, regions=regions)
        print(f"{name} vtable {live:#x} (subobject +{offset:#x}): {len(hits)} hits")
        for h in hits:
            print(f"  {h - offset:#x}")


def cmd_refs(s, value, limit):
    lo, hi = s.proc.base, s.proc.base + len(s.img.image)
    for h in s.proc.find_qword(value, limit, include_image=True):
        tag = f"  (image, file va {s.to_file(h):#x})" if lo <= h < hi else ""
        print(f"  {h:#x}{tag}")


def cmd_dump(s, addr, size):
    data = s.proc.read(addr, size)
    for off in range(0, len(data) - 7, 8):
        q = struct.unpack_from("<Q", data, off)[0]
        d = struct.unpack_from("<d", data, off)[0]
        f0, f1 = struct.unpack_from("<2f", data, off)
        note = ""
        if q > 0x10000 and q < USER_SPACE_END:
            cls = s.class_of(q)
            if cls:
                note = f" -> {cls}"
            elif s.img.in_section(s.to_file(q), ".rdata") and (c := disasm.class_of_vtable(s.img, s.to_file(q))):
                note = f" vtable {c}"
        print(f"  +{off:#05x} {q:#018x}  d={d:<14.6g} f=({f0:.4g}, {f1:.4g}){note}")


def cmd_entity(s, addr):
    data = s.proc.read(addr + ENTITY_TRANSFORM, 0x40)
    pos = struct.unpack_from("<3d", data, 0)
    rot = struct.unpack_from("<9f", data, 0x18)
    print(f"class {s.class_of(addr)}")
    print(f"position {pos[0]:.3f} {pos[1]:.3f} {pos[2]:.3f}")
    for r in range(3):
        print("rotation row", " ".join(f"{v:7.3f}" for v in rot[r * 3:r * 3 + 3]))


COMPOUND_NUM_HANDLERS, COMPOUND_HANDLERS, HANDLER_STRIDE = 0x8, 0x68, 0x10


COMPOUND_KIND, COMPOUND_NAME = 4, 0x40
RECORD_ALIGN = 8


def find_live_compound(s, type_name):
    """The reflected compound record for type_name in the live image (.data including the zero-filled part, where
    records built at start-up live)."""
    name_needle = type_name.encode() + b"\0"
    lo, hi = s.img.section(".data")
    data = s.proc.read(lo + s.slide, hi - lo)
    names = set()
    for section in (".rdata", ".data"):  # type names sit in both
        rlo, rhi = s.img.section(section)
        blob = s.proc.read(rlo + s.slide, rhi - rlo)
        i = blob.find(name_needle)
        while i >= 0:
            if i == 0 or blob[i - 1] == 0:
                names.add(rlo + s.slide + i)
            i = blob.find(name_needle, i + 1)
    for name_va in names:
        packed = struct.pack("<Q", name_va)
        j = data.find(packed)
        while j >= 0:
            record = j - COMPOUND_NAME
            if record >= 0 and record % RECORD_ALIGN == 0 and data[record + 4] == COMPOUND_KIND:
                return lo + s.slide + record
            j = data.find(packed, j + 1)
    return None


def cmd_handlers(s, type_name):
    live = find_live_compound(s, type_name)
    if not live:
        sys.exit(f"{type_name}: no reflected record found")
    count = s.proc.read(live + COMPOUND_NUM_HANDLERS, 1)[0]
    table = s.proc.ptr(live + COMPOUND_HANDLERS)
    print(f"{type_name} record {live:#x}: {count} handlers")
    for i in range(count):
        message = s.proc.ptr(table + i * HANDLER_STRIDE)
        handler = s.proc.ptr(table + i * HANDLER_STRIDE + 8)
        name = s.proc.read(s.proc.ptr(message + COMPOUND_NAME), 96).split(b"\0")[0].decode(errors="replace")
        print(f"  on {name} -> {handler:#x} (file {s.to_file(handler):#x})")


def cmd_diff(path_a, path_b):
    a, b = open(path_a, "rb").read(), open(path_b, "rb").read()
    for off in range(0, min(len(a), len(b)) - 7, 8):
        if a[off:off + 8] != b[off:off + 8]:
            (qa,), (qb,) = struct.unpack("<Q", a[off:off + 8]), struct.unpack("<Q", b[off:off + 8])
            (da,), (db,) = struct.unpack("<d", a[off:off + 8]), struct.unpack("<d", b[off:off + 8])
            fa, fb = struct.unpack("<2f", a[off:off + 8]), struct.unpack("<2f", b[off:off + 8])
            print(f"+{off:#06x} {qa:#018x} -> {qb:#018x}  d {da:.4g} -> {db:.4g}  f {fa} -> {fb}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "diff":
        return cmd_diff(sys.argv[2], sys.argv[3])
    s = Session()
    limit = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else DEFAULT_MAX_HITS
    if cmd == "base":
        print(f"base {s.proc.base:#x} slide {s.slide:#x}")
    elif cmd == "instances":
        near = int(sys.argv[sys.argv.index("--near") + 1], 16) if "--near" in sys.argv else None
        cmd_instances(s, sys.argv[2], limit, near)
    elif cmd == "refs":
        cmd_refs(s, int(sys.argv[2], 16), limit)
    elif cmd == "dump":
        cmd_dump(s, int(sys.argv[2], 16), int(sys.argv[3], 16) if len(sys.argv) > 3 else DEFAULT_DUMP)
    elif cmd == "entity":
        cmd_entity(s, int(sys.argv[2], 16))
    elif cmd == "handlers":
        cmd_handlers(s, sys.argv[2])
    elif cmd == "snap":
        open(sys.argv[4], "wb").write(s.proc.read(int(sys.argv[2], 16), int(sys.argv[3], 16)))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
