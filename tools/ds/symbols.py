"""Finds the code behind a Decima script-exported function name (static, from the exe file).

Decima registers script-callable functions by name: a small generated helper loads the name string, and its caller
passes the function's address as the fifth argument ([rsp+0x20]). This walks name -> helper -> caller -> address.

usage: python symbols.py <exe> <Name> [<Name> ...]
"""
import sys

import disasm
from pe_image import PeImage

FIFTH_ARG_SLOT = "qword ptr [rsp + 0x20]"
LOOKBACK_INSNS = 6


def address_args_before(img, call_va):
    """The lea'd address stored into the fifth-argument slot just before the call at call_va."""
    start = call_va - 0x30
    insns = [i for i in disasm.md.disasm(img.read(start, 0x30 + 5), start) if i.address <= call_va]
    loaded = {}
    for insn in insns[-LOOKBACK_INSNS:]:
        if insn.mnemonic == "lea" and "rip" in insn.op_str:
            reg = insn.op_str.split(",")[0]
            loaded[reg] = insn.address + insn.size + insn.disp
        elif insn.mnemonic == "mov" and insn.op_str.startswith(FIFTH_ARG_SLOT):
            reg = insn.op_str.split(",")[1].strip()
            if reg in loaded:
                return loaded[reg]
    return None


def resolve(img, name):
    for string_va in disasm.find_strings(img, name):
        for ref in disasm.xrefs(img, string_va):
            helper = disasm.function_start(img, ref.address)
            for call in disasm.xrefs(img, helper) if helper else []:
                target = address_args_before(img, call.address)
                if target:
                    yield helper, call.address, target


def main():
    img = PeImage(sys.argv[1])
    for name in sys.argv[2:]:
        found = list(resolve(img, name))
        if not found:
            print(f"{name}: not found")
        for helper, call, target in found:
            print(f"{name}: function {target:#x} (registered by {helper:#x} from {call:#x})")


if __name__ == "__main__":
    main()
