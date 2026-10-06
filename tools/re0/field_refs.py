"""Static: every instruction in RE0's image whose memory operand has a given displacement (field offset).

usage: python field_refs.py <disp> [<disp> ...]   (hex, e.g. 0x67a4)
Needs image.bin (see disasm.py dump). Prints address, mnemonic, operands; writes are the ones with the field first.
"""
import sys

import capstone
from capstone.x86 import X86_OP_MEM

import disasm

TEXT_START = 0x401000
TEXT_END = 0x8B1000
CHUNK = 0x4000


def refs(img, disps):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    va = TEXT_START
    while va < TEXT_END:
        base = va - disasm.IMAGE_BASE
        last = va
        for ins in md.disasm(img[base:base + CHUNK], va):
            last = ins.address + ins.size
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base != 0 and op.mem.disp in disps:
                    yield ins
        va = last if last > va else va + 1


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    disps = {int(arg, 16) for arg in sys.argv[1:]}
    for ins in refs(disasm.load(), disps):
        print(f"{ins.address:#x}  {ins.mnemonic:6} {ins.op_str}")


if __name__ == "__main__":
    main()
