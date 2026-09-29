"""Offline disassembly of RE0's decrypted image (dumped from the running game by `probe.py`-style reads).

usage: python disasm.py dump                  # save the in-memory image to image.bin (game must be running)
       python disasm.py fn <addr> [max_insns] # disassemble a function from image.bin until ret
       python disasm.py vtable <addr> [count] # list vtable entries
       python disasm.py xrefs <addr>          # code locations that call/jmp/reference addr
"""
import os
import re
import struct
import sys

import capstone

IMAGE_BASE = 0x400000
IMAGE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "image.bin")
DEFAULT_MAX_INSNS = 200
DEFAULT_VTABLE_COUNT = 32
CALL_REL, JMP_REL = 0xE8, 0xE9

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def load():
    return open(IMAGE_FILE, "rb").read()


def dump():
    sys.argv = ["probe"]
    import probe
    proc = probe.Proc(probe.find_pid())
    secs = probe.image_sections(proc)
    end = max(va + len(data) for va, data in secs.values())
    image = bytearray(end - IMAGE_BASE)
    for va, data in secs.values():
        image[va - IMAGE_BASE:va - IMAGE_BASE + len(data)] = data
    open(IMAGE_FILE, "wb").write(image)
    print(f"{IMAGE_FILE} {len(image):#x} bytes")


def u32(img, va):
    return struct.unpack_from("<I", img, va - IMAGE_BASE)[0]


def fn(img, va, max_insns):
    code = img[va - IMAGE_BASE:va - IMAGE_BASE + max_insns * 15]
    for i, ins in enumerate(md.disasm(code, va)):
        print(f"{ins.address:#x}  {ins.mnemonic:6} {ins.op_str}")
        if ins.mnemonic == "ret" or i + 1 >= max_insns:
            break


def vtable(img, va, count):
    for i in range(count):
        print(f"[{i:2}] +{i * 4:#05x}  {u32(img, va + i * 4):#x}")


def xrefs(img, target):
    text_end = 0x8B1000
    for m in re.finditer(re.escape(struct.pack("<I", target)), img):
        print(f"abs  {IMAGE_BASE + m.start():#x}")
    for off in range(0x1000, text_end - 5):
        op = img[off]
        if op in (CALL_REL, JMP_REL) and IMAGE_BASE + off + 5 + struct.unpack_from("<i", img, off + 1)[0] == target:
            print(f"{'call' if op == CALL_REL else 'jmp '} {IMAGE_BASE + off:#x}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "dump":
        return dump()
    img = load()
    addr = int(sys.argv[2], 16)
    if cmd == "fn":
        fn(img, addr, int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_MAX_INSNS)
    elif cmd == "vtable":
        vtable(img, addr, int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_VTABLE_COUNT)
    elif cmd == "xrefs":
        xrefs(img, addr)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
