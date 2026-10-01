"""Static x64 analysis of a Decima executable straight from the file (no running game needed).

usage: python disasm.py <exe> fn <va> [max_insns]     # disassemble from va until ret (or max_insns)
       python disasm.py <exe> xrefs <va>              # code that references va (rip-relative or call/jmp rel32)
       python disasm.py <exe> str <text>              # where a string lives and the code that loads it
       python disasm.py <exe> vtable <Class> [count]  # MSVC RTTI: the class's vtables and their first entries
       python disasm.py <exe> vtname <va>             # MSVC RTTI class name of the vtable at va
"""
import struct
import sys

import capstone
import numpy as np

from pe_image import PeImage

DEFAULT_MAX_INSNS = 120
DEFAULT_VTABLE_COUNT = 24
DISP_TO_END = (4, 5, 8)  # disp32 is followed by 0, 1 or 4 immediate bytes
CALL_REL, JMP_REL = 0xE8, 0xE9
MAX_DISP_BACK = 7  # prefixes + opcode + modrm + sib before a disp32
COL_SIGNATURE_X64 = 1
TYPE_DESCRIPTOR_NAME = 0x10
COL_TYPE_DESCRIPTOR, COL_SELF = 0xC, 0x14

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def text_array(img):
    lo, hi = img.section(".text")
    return lo, np.frombuffer(img.read(lo, hi - lo), dtype=np.uint8)


def disp_sites(img, target):
    """Offsets in .text whose int32 resolves to target for some instruction end (disp + 4 + imm)."""
    lo, text = text_array(img)
    n = len(text) - 4
    words = (text[:n].astype(np.int64) | (text[1:n + 1].astype(np.int64) << 8) |
             (text[2:n + 2].astype(np.int64) << 16) | (text[3:n + 3].astype(np.int64) << 24))
    words = np.where(words >= 1 << 31, words - (1 << 32), words)
    positions = np.arange(n, dtype=np.int64) + lo
    hits = set()
    for tail in DISP_TO_END:
        for p in np.nonzero(positions + tail + words == target)[0]:
            hits.add(int(p) + lo)
    return sorted(hits)


def instruction_at(img, disp_va):
    """Decodes the instruction whose rip-relative disp32 (or call/jmp rel32) sits at disp_va (longest match first,
    so a REX prefix is not dropped)."""
    for back in range(MAX_DISP_BACK, 0, -1):
        start = disp_va - back
        for insn in md.disasm(img.read(start, 16), start):
            rip_relative = "rip" in insn.op_str and start + insn.disp_offset == disp_va
            branch = back == 1 and insn.bytes[0] in (CALL_REL, JMP_REL)
            if rip_relative or branch:
                return insn
            break
    return None


def xrefs(img, target):
    out = []
    for site in disp_sites(img, target):
        insn = instruction_at(img, site)
        if insn:
            out.append(insn)
    return out


def function_start(img, va, limit=0x4000):
    """Nearest preceding int3/ret padding boundary: a cheap guess at the containing function."""
    for back in range(1, limit):
        if img.u8(va - back) == 0xCC and img.u8(va - back - 1) in (0xCC, 0xC3):
            return va - back + 1
    return None


def print_fn(img, va, max_insns):
    for i, insn in enumerate(md.disasm(img.read(va, max_insns * 15), va)):
        print(f"  {insn.address:#x}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic in ("ret", "int3") or i + 1 >= max_insns:
            break


def find_strings(img, text):
    needle = text.encode() + b"\0"
    hits, i = [], img.image.find(needle)
    while i >= 0:
        if i == 0 or img.image[i - 1] == 0:
            hits.append(img.base + i)
        i = img.image.find(needle, i + 1)
    return hits


def vtables(img, class_name):
    found = []
    for td_name in find_strings(img, f".?AV{class_name}@@"):
        td_rva = td_name - TYPE_DESCRIPTOR_NAME - img.base
        lo, hi = img.section(".rdata")
        data = img.read(lo, hi - lo)
        pattern = struct.pack("<I", td_rva)
        i = data.find(pattern)
        while i >= 0:
            col = lo + i - COL_TYPE_DESCRIPTOR
            if img.u32(col) == COL_SIGNATURE_X64 and img.u32(col + COL_SELF) == col - img.base:
                ref = data.find(struct.pack("<Q", col))
                while ref >= 0:
                    if ref % 8 == 0:
                        found.append((lo + ref + 8, img.u32(col + 4)))
                    ref = data.find(struct.pack("<Q", col), ref + 1)
            i = data.find(pattern, i + 1)
    return found


def class_of_vtable(img, vt):
    """MSVC RTTI name of the class whose vtable starts at vt (the complete object locator sits just before it)."""
    col = img.ptr(vt - 8)
    if not img.in_section(col, ".rdata"):
        return None
    return img.cstr(img.base + img.u32(col + COL_TYPE_DESCRIPTOR) + TYPE_DESCRIPTOR_NAME)


def main():
    img = PeImage(sys.argv[1])
    cmd, arg = sys.argv[2], sys.argv[3]
    if cmd == "fn":
        print_fn(img, int(arg, 16), int(sys.argv[4]) if len(sys.argv) > 4 else DEFAULT_MAX_INSNS)
    elif cmd == "xrefs":
        for insn in xrefs(img, int(arg, 16)):
            start = function_start(img, insn.address)
            print(f"  {insn.address:#x}: {insn.mnemonic} {insn.op_str}   (fn ~{start:#x})" if start else
                  f"  {insn.address:#x}: {insn.mnemonic} {insn.op_str}")
    elif cmd == "str":
        for va in find_strings(img, arg):
            print(f"{arg!r} at {va:#x}")
            for insn in xrefs(img, va):
                start = function_start(img, insn.address)
                print(f"  {insn.address:#x}: {insn.mnemonic} {insn.op_str}   (fn ~{start or 0:#x})")
    elif cmd == "vtname":
        print(class_of_vtable(img, int(arg, 16)))
    elif cmd == "vtable":
        count = int(sys.argv[4]) if len(sys.argv) > 4 else DEFAULT_VTABLE_COUNT
        for vt, offset in vtables(img, arg):
            print(f"vtable {vt:#x} (subobject offset {offset:#x})")
            for k in range(count):
                print(f"  [{k:2}] {img.ptr(vt + 8 * k):#x}")


if __name__ == "__main__":
    main()
