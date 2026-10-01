"""Finds the code behind a Decima script-exported function name (static, from the exe file).

Decima registers script-callable functions by name, in one of two shapes:
- inline: the group's registration code loads the export's name and, a few instructions later, the function's
  address (`lea reg, [rip+fn]`);
- helper: a small per-export helper loads the name, and its caller passes the function's address as the fifth
  argument (`lea rax, [rip+fn]; mov [rsp+0x20], rax; call helper`).
Both are tried. Names are the symbol names such as "Entity_ExportedSetWorldTransform" or
"DSBaggageManager_sExportedDeleteBaggage".

A result of 0x1400bb3c0 (`xor eax, eax; ret`) or a similar one-instruction stub means the export is compiled out
in this build.

usage: python symbols.py <exe> <Name> [<Name> ...]
"""
import sys

import disasm
from pe_image import PeImage

LOOKAHEAD_BYTES = 0x60
LOOKBEHIND_BYTES = 0x20
FIFTH_ARG_SLOT = "qword ptr [rsp + 0x20]"
BODY_PREVIEW_INSNS = 4


def first_code_lea_after(img, address):
    for insn in disasm.md.disasm(img.read(address, LOOKAHEAD_BYTES), address):
        if insn.mnemonic == "lea" and "rip" in insn.op_str:
            target = insn.address + insn.size + insn.disp
            if img.in_section(target, ".text"):
                return target
    return None


def fifth_argument_before(img, call_va):
    start = call_va - LOOKBEHIND_BYTES
    loaded = {}
    for insn in disasm.md.disasm(img.read(start, LOOKBEHIND_BYTES), start):
        if insn.mnemonic == "lea" and "rip" in insn.op_str:
            loaded[insn.op_str.split(",")[0]] = insn.address + insn.size + insn.disp
        elif insn.mnemonic == "mov" and insn.op_str.startswith(FIFTH_ARG_SLOT):
            target = loaded.get(insn.op_str.split(",")[1].strip())
            if target and img.in_section(target, ".text"):
                return target
    return None


def resolve(img, name):
    for string_va in disasm.find_strings(img, name):
        for ref in disasm.xrefs(img, string_va):
            helper = disasm.function_start(img, ref.address)
            callers = disasm.xrefs(img, helper) if helper else []
            if len(callers) == 1:  # a helper of its own: the address comes from its caller
                target = fifth_argument_before(img, callers[0].address)
                if target:
                    yield "helper", callers[0].address, target
                    continue
            target = first_code_lea_after(img, ref.address + ref.size)
            if target:
                yield "inline", ref.address, target


def preview(img, va):
    insns = list(disasm.md.disasm(img.read(va, 48), va))[:BODY_PREVIEW_INSNS]
    return " ; ".join(f"{i.mnemonic} {i.op_str}" for i in insns)


def main():
    img = PeImage(sys.argv[1])
    for name in sys.argv[2:]:
        found = sorted(set(resolve(img, name)))
        if not found:
            print(f"{name}: not found")
        for shape, site, target in found:
            print(f"{name}: {target:#x} ({shape}, registered at {site:#x})  {preview(img, target)}")


if __name__ == "__main__":
    main()
