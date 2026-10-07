"""Static: disassemble RE0 room event scripts (nativePC/event/*.bes2, rEventScript) with the game's own opcode table.

usage: python bes2.py <file.bes2>              # triggers and every thread's code
       python bes2.py <file.bes2> --triggers   # the trigger table only
       python bes2.py --find <opcode name> <dir> # files whose code uses an opcode
Needs image.bin (see disasm.py dump) for the opcode names and operand signatures.

File layout (big-endian): u32 trigger count, 12 bytes pad, then per trigger {u32 condition type, u32 code offset,
u32 param1, u32 param2}; the code follows, offsets are from the file start. An opcode is a u16 index into the table at
0xcd57d8 (rows {handler, signature, name}); its operands follow, sized by the signature: U1/S1 1, U2/S2 2, U4/S4/F4 4,
Fn (flag number) 2, Rn (register) 1.
"""
import os
import struct
import sys

import disasm

OPCODE_TABLE = 0xcd57d8
OPCODE_ROW = 12
OPCODE_COUNT = 312
HEADER_SIZE = 0x10
TRIGGER_SIZE = 0x10
OPERAND_SIZE = {"U1": 1, "S1": 1, "U2": 2, "S2": 2, "U4": 4, "S4": 4, "F4": 4, "Fn": 2, "Rn": 1}


def opcode_table():
    img = disasm.load()

    def cstr(va):
        start = va - disasm.IMAGE_BASE
        return img[start:img.index(b"\0", start)].decode("latin1") if va else ""

    table = []
    for i in range(OPCODE_COUNT):
        row = OPCODE_TABLE + i * OPCODE_ROW
        handler, signature, name = (disasm.u32(img, row + k * 4) for k in range(3))
        sig = cstr(signature)
        table.append((cstr(name), handler, [sig[j:j + 2] for j in range(0, len(sig), 2)]))
    return table


def triggers(data):
    count = struct.unpack_from(">I", data, 0)[0]
    return [struct.unpack_from(">IIII", data, HEADER_SIZE + i * TRIGGER_SIZE) for i in range(count)]


def operand(data, pc, kind):
    size = OPERAND_SIZE[kind]
    fmt = {1: ">B", 2: ">H", 4: ">I"}[size]
    value = struct.unpack_from(fmt, data, pc)[0]
    if kind == "F4":
        value = struct.unpack_from(">f", data, pc)[0]
    elif kind[0] == "S" and value >= 1 << (size * 8 - 1):
        value -= 1 << (size * 8)
    return value, size


def code(data, table):
    count = struct.unpack_from(">I", data, 0)[0]
    pc = HEADER_SIZE + count * TRIGGER_SIZE
    while pc + 2 <= len(data):
        op = struct.unpack_from(">H", data, pc)[0]
        if op >= len(table):
            yield pc, f"?? {op:#x}", []
            pc += 2
            continue
        name, _, sig = table[op]
        args, at = [], pc + 2
        for kind in sig:
            value, size = operand(data, at, kind)
            args.append(f"{value:g}" if kind == "F4" else hex(value) if value >= 10 else str(value))
            at += size
        yield pc, name, args
        pc = at


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    table = opcode_table()
    if sys.argv[1] == "--find":
        name, folder = sys.argv[2], sys.argv[3]
        for file in sorted(os.listdir(folder)):
            data = open(os.path.join(folder, file), "rb").read()
            hits = [pc for pc, op, _ in code(data, table) if op == name]
            if hits:
                print(file, " ".join(hex(pc) for pc in hits))
        return
    data = open(sys.argv[1], "rb").read()
    starts = {}
    for i, (kind, offset, p1, p2) in enumerate(triggers(data)):
        print(f"trigger {i:2}: type {kind:#04x} code {offset:#06x} p1 {p1:#x} p2 {p2:#x}")
        starts.setdefault(offset, []).append(i)
    if "--triggers" in sys.argv:
        return
    for pc, name, args in code(data, table):
        for i in starts.get(pc, []):
            print(f"; --- trigger {i}")
        print(f"{pc:#06x}  {name} {', '.join(args)}")


if __name__ == "__main__":
    main()
