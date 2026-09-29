"""Read-only runtime probe for RE0 HD (32-bit MT Framework, image base 0x400000, no ASLR).

Attaches to a running re0hd.exe, reads its decrypted image, and maps MT Framework
type info (MtDTI) to class vtables and live heap instances. Never writes game memory.

usage: python probe.py classes            # list DTI classes matching the watch list
       python probe.py instances           # find live uPlayer*/uEnemy* objects
       python probe.py dump <addr> [size]  # hex-dump memory at addr
       python probe.py what <addr> [addr...]  # MT class name of objects
       python probe.py snap <addr> <size> <file>
       python probe.py diff <file_a> <file_b>  # changed dwords between two snaps
"""
import ctypes
import ctypes.wintypes as wt
import re
import struct
import sys

PROCESS_NAME = "re0hd.exe"
IMAGE_BASE = 0x400000
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
READABLE = {0x02, 0x04, 0x20, 0x40}  # PAGE_READONLY, READWRITE, EXECUTE_READ, EXECUTE_READWRITE
USER_SPACE_END = 0x7FFF0000
WATCH = re.compile(rb"^(uPlayer\w*|uEnemy\w*|sPlayer|sEnemy|sGameChara|sGamePad|sPad|uCoord|sMain|sGameScene|sGameArea|uModel)$")
DUMP_DEFAULT = 0x200

k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


def find_pid():
    import subprocess
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    m = re.search(r'"%s","(\d+)"' % re.escape(PROCESS_NAME), out)
    if not m:
        sys.exit(f"{PROCESS_NAME} is not running")
    return int(m.group(1))


class Proc:
    def __init__(self, pid):
        self.h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h:
            sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
            return b""
        return buf.raw[:n.value]

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else 0

    def regions(self):
        addr, mbi = 0, MEMORY_BASIC_INFORMATION()
        while addr < USER_SPACE_END and k32.VirtualQueryEx(self.h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            base, size = mbi.BaseAddress or 0, mbi.RegionSize
            if mbi.State == MEM_COMMIT and (mbi.Protect & 0xFF) in READABLE:
                yield base, size
            addr = base + size


def image_sections(proc):
    """Return {name: (va, data)} for the in-memory (decrypted) image."""
    hdr = proc.read(IMAGE_BASE, 0x1000)
    pe = struct.unpack_from("<I", hdr, 0x3C)[0]
    count = struct.unpack_from("<H", hdr, pe + 6)[0]
    opt_size = struct.unpack_from("<H", hdr, pe + 20)[0]
    table = pe + 24 + opt_size
    out = {}
    for i in range(count):
        e = table + i * 40
        name = hdr[e:e + 8].rstrip(b"\0").decode()
        vsize, rva = struct.unpack_from("<II", hdr, e + 8)
        out[name] = (IMAGE_BASE + rva, proc.read(IMAGE_BASE + rva, vsize))
    return out


def find_dti_classes(proc, secs):
    """MtDTI objects live in .data and point at their class-name string in .rdata.
    A class's getDTI() virtual is `mov eax, <dti>; ret`, and vtables in .rdata hold that function."""
    rdata_va, rdata = secs[".rdata"]
    data_va, data = secs[".data"]
    text_va, text = secs[".text"]
    names = {}
    for m in re.finditer(rb"(?<=\0)[a-zA-Z_][\w:]{2,63}\0", rdata):
        s = m.group()[:-1]
        if WATCH.match(s):
            names[rdata_va + m.start()] = s.decode()
    classes = {}
    for off in range(0, len(data) - 8, 4):
        p = struct.unpack_from("<I", data, off)[0]
        if p in names:
            dti = data_va + off - 4  # name is the 2nd field, after the DTI vtable
            classes.setdefault(names[p], []).append(dti)
    for name, dtis in classes.items():
        for dti in dtis:
            getters = [text_va + m.start() for m in re.finditer(re.escape(b"\xB8" + struct.pack("<I", dti) + b"\xC3"), text)]
            vtables = []
            for g in getters:
                vtables += [rdata_va + m.start() for m in re.finditer(re.escape(struct.pack("<I", g)), rdata) if m.start() % 4 == 0]
            yield name, dti, getters, vtables


def cmd_classes(proc):
    secs = image_sections(proc)
    for name, dti, getters, vtables in sorted(find_dti_classes(proc, secs)):
        print(f"{name:24} dti={dti:#x} getDTI={[hex(g) for g in getters]} vtable_slots={[hex(v) for v in vtables[:4]]}")


def cmd_instances(proc):
    secs = image_sections(proc)
    candidates = {}
    for name, dti, getters, slots in find_dti_classes(proc, secs):
        if name.startswith(("uPlayer", "uEnemy")):
            for s in slots:
                candidates[s] = name  # the slot's vtable start is found below by scanning heap pointers into this range
    rdata_va, rdata = secs[".rdata"]
    # vtable start = slot - 4*k; a heap object's first dword equals the start, so accept any heap dword within 64 slots before a getDTI slot
    window = {}
    for s, name in candidates.items():
        for k in range(64):
            window.setdefault(s - 4 * k, (name, k))
    image_end = IMAGE_BASE + 0x0A70000
    hits = {}
    for base, size in proc.regions():
        if IMAGE_BASE <= base < image_end:
            continue
        blob = proc.read(base, size)
        for off in range(0, len(blob) - 4, 4):
            v = struct.unpack_from("<I", blob, off)[0]
            if v in window:
                hits.setdefault((window[v][0], v, window[v][1]), []).append(base + off)
    # true vtable start: the entry whose objects are fewest-but-nonzero is ambiguous, so print all with small counts
    for (name, vt, idx), objs in sorted(hits.items(), key=lambda kv: (kv[0][0], len(kv[1]))):
        if len(objs) <= 64:
            print(f"{name:24} vtable={vt:#x} getDTI_index={idx:3} count={len(objs):3} first={[hex(o) for o in objs[:4]]}")


def cmd_dump(proc, addr, size):
    data = proc.read(addr, size)
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        floats = " ".join(f"{f:10.3f}" for f in struct.unpack_from("<4f", row)) if len(row) == 16 else ""
        print(f"{addr + i:08x}  {row.hex(' ')}  {floats}")


GETDTI_SLOT = 4


def class_name(proc, obj):
    """MT objects: vtable[4] is getDTI() = `mov eax, dti; ret`; the DTI's 2nd field is the name string."""
    fn = proc.u32(proc.u32(obj) + GETDTI_SLOT * 4)
    code = proc.read(fn, 6)
    if len(code) < 6 or code[0] != 0xB8 or code[5] != 0xC3:
        return None
    name = proc.read(proc.u32(struct.unpack_from("<I", code, 1)[0] + 4), 64)
    return name.split(b"\0")[0].decode(errors="replace")


def cmd_what(proc, addrs):
    for a in addrs:
        print(f"{a:#x}  vtable={proc.u32(a):#x}  class={class_name(proc, a)}")


def cmd_snap(proc, addr, size, path):
    with open(path, "wb") as f:
        f.write(proc.read(addr, size))


def cmd_diff(path_a, path_b, word=4):
    """Print dwords that differ between two snapshots of the same object, as hex and float."""
    a, b = open(path_a, "rb").read(), open(path_b, "rb").read()
    for off in range(0, min(len(a), len(b)) - word + 1, word):
        if a[off:off + word] != b[off:off + word]:
            (ia,), (ib,) = struct.unpack("<I", a[off:off + word]), struct.unpack("<I", b[off:off + word])
            (fa,), (fb,) = struct.unpack("<f", a[off:off + word]), struct.unpack("<f", b[off:off + word])
            print(f"+{off:#06x}  {ia:#010x} -> {ib:#010x}   {fa:12.4g} -> {fb:12.4g}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "diff":
        return cmd_diff(sys.argv[2], sys.argv[3])
    proc = Proc(find_pid())
    cmd = sys.argv[1] if len(sys.argv) > 1 else "classes"
    if cmd == "classes":
        cmd_classes(proc)
    elif cmd == "instances":
        cmd_instances(proc)
    elif cmd == "dump":
        cmd_dump(proc, int(sys.argv[2], 16), int(sys.argv[3], 16) if len(sys.argv) > 3 else DUMP_DEFAULT)
    elif cmd == "what":
        cmd_what(proc, [int(a, 16) for a in sys.argv[2:]])
    elif cmd == "snap":
        cmd_snap(proc, int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
