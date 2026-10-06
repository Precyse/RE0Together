"""Static: per enemy update implementation, the state dispatch table and a profile of each state handler
(setAction calls, direct record writes, position writes, size), to spot the AI decision step.

usage: python think_scan.py <table> <entries> <vtable of one class using it>
"""
import struct
import sys

import capstone

import disasm

MAX_INSNS = 700
SET_ACTION_DISP = 0xFC
STATE_DISP = 0x67A4
POSITION_DISPS = (0x40, 0x44, 0x48)


def resolve(img, address, vtable):
    """A table entry that is a thunk `mov eax,[ecx]; jmp [eax+K]` resolves through the class vtable."""
    code = img[address - disasm.IMAGE_BASE:address - disasm.IMAGE_BASE + 8]
    if code[:2] == b"\x8b\x01" and code[2] == 0xFF and code[3] == 0xA0:
        slot = struct.unpack_from("<I", code, 4)[0] // 4
        return disasm.u32(img, vtable + slot * 4)
    return address


SET_ACTION_IMPLEMENTATIONS = (0x4CC670, 0x44D820, 0x4650F0, 0x480C20, 0x4BBC70, 0x48AC80, 0x4B17F0)
CALL_DEPTH = 2


def direct_calls(img, function):
    """Targets of the `call rel32` instructions of one function body."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    start = function - disasm.IMAGE_BASE
    targets = []
    for i, ins in enumerate(md.disasm(img[start:start + MAX_INSNS * 8], function)):
        if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            targets.append(int(ins.op_str, 16))
        if ins.mnemonic == "ret" or i >= MAX_INSNS:
            break
    return targets


def reaches_set_action(img, function, depth=CALL_DEPTH):
    """setAction call sites in the function and, up to `depth` levels, in what it calls directly."""
    own = profile(img, function)["setAction"]
    if depth == 0:
        return own
    return own + sum(reaches_set_action(img, t, depth - 1) for t in direct_calls(img, function)
                     if disasm.IMAGE_BASE <= t < 0x8B1000)


def profile(img, function):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    counts = {"setAction": 0, "stateWrite": 0, "posWrite": 0, "insns": 0, "calls": 0}
    start = function - disasm.IMAGE_BASE
    for ins in md.disasm(img[start:start + MAX_INSNS * 8], function):
        counts["insns"] += 1
        if ins.mnemonic == "call":
            counts["calls"] += 1
            if f"+ {SET_ACTION_DISP:#x}]" in ins.op_str or ins.op_str in {hex(f) for f in SET_ACTION_IMPLEMENTATIONS}:
                counts["setAction"] += 1
        if ins.mnemonic == "mov" and f"+ {STATE_DISP:#x}]," in ins.op_str:
            counts["stateWrite"] += 1
        if ins.mnemonic == "movss" and any(f"+ {d:#x}]," in ins.op_str for d in POSITION_DISPS):
            counts["posWrite"] += 1
        if ins.mnemonic == "ret" or counts["insns"] >= MAX_INSNS:
            break
    return counts


def main():
    table, entries, vtable = (int(a, 16) for a in sys.argv[1:4])
    img = disasm.load()
    for state in range(entries):
        raw = disasm.u32(img, table + state * 4)
        if not raw:
            print(f"state {state}: none")
            continue
        function = resolve(img, raw, vtable)
        counts = profile(img, function)
        counts["reachSetAction"] = reaches_set_action(img, function)
        print(f"state {state}: {function:#x} {counts}")


if __name__ == "__main__":
    main()
