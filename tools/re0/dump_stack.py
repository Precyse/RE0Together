"""Print the crashing thread's registers and the return addresses on its stack from a coop\\crash-*.dmp.

usage: python dump_stack.py <crash.dmp> [stack_bytes]
Needs `pip install minidump`. Addresses in re0hd.exe can then be read with disasm.py.
"""
import logging
import ntpath
import struct
import sys

from minidump.minidumpfile import MinidumpFile

DEFAULT_STACK_BYTES = 0x600
CODE_MODULES = ("re0hd", "dinput8")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 else DEFAULT_STACK_BYTES
    logging.disable(logging.ERROR)  # the parser logs a harmless PEB error for small dumps
    dump = MinidumpFile.parse(sys.argv[1])
    reader = dump.get_reader()
    record = dump.exception.exception_records[0]
    thread = next(t for t in dump.threads.threads if t.ThreadId == record.ThreadId)
    ctx = thread.ContextObject
    print(f"exception {record.ExceptionRecord.ExceptionCode} at {record.ExceptionRecord.ExceptionAddress:#x}")
    print(f"eax {ctx.Eax:#x} ebx {ctx.Ebx:#x} ecx {ctx.Ecx:#x} edx {ctx.Edx:#x} esi {ctx.Esi:#x} edi {ctx.Edi:#x} "
          f"esp {ctx.Esp:#x} ebp {ctx.Ebp:#x}")
    modules = [(m.baseaddress, m.baseaddress + m.size, ntpath.basename(m.name)) for m in dump.modules.modules]
    stack = b""
    while len(stack) < size:  # the dump holds the stack only up to its base
        try:
            stack += reader.read(ctx.Esp + len(stack), 4)
        except Exception:
            break
    for offset in range(0, len(stack), 4):
        (value,) = struct.unpack_from("<I", stack, offset)
        for low, high, name in modules:
            if low <= value < high and name.lower().startswith(CODE_MODULES):
                print(f"{ctx.Esp + offset:#x}  {value:#x}  {name}+{value - low:#x}")


if __name__ == "__main__":
    main()
