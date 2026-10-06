"""Static: for each enemy vtable the function in a slot and the stack bytes its returns pop.

usage: python slot_funcs.py <slot> <vtable> [<vtable> ...]
"""
import sys

import capstone

import disasm

MAX_INSNS = 4000
RET_OPCODES = (0xC3, 0xC2)


def returns(img, va):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    pops = set()
    for i, ins in enumerate(md.disasm(img[va - disasm.IMAGE_BASE:va - disasm.IMAGE_BASE + MAX_INSNS * 8], va)):
        if ins.mnemonic == "ret":
            pops.add(int(ins.op_str, 16) if ins.op_str else 0)
        if i > MAX_INSNS:
            break
    return pops


def main():
    slot = int(sys.argv[1])
    img = disasm.load()
    for arg in sys.argv[2:]:
        vtable = int(arg, 16)
        func = disasm.u32(img, vtable + slot * 4)
        print(f"{vtable:#x} slot {slot} -> {func:#x} ret pops {sorted(returns(img, func))}")


if __name__ == "__main__":
    main()
