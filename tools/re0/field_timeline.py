"""Record which small fields of RE0 singletons change over time (read-only polling, safe to stop any time).

usage: python field_timeline.py <seconds> <name=static_ptr_hex:size_hex> [...]
       e.g. python field_timeline.py 60 sDoorLoad=dcbeb8:260 sSubMenu=dcebd0:100
Prints one line per change: time, object, offset, old -> new (dword fields whose values stay below SMALL_LIMIT).
"""
import struct
import sys
import time

import probe

POLL_SECONDS = 0.05
SMALL_LIMIT = 0x10000


def parse(spec):
    name, rest = spec.split("=")
    static, size = rest.split(":")
    return name, int(static, 16), int(size, 16)


def main():
    seconds = float(sys.argv[1])
    targets = [parse(s) for s in sys.argv[2:]]
    sys.argv = ["probe"]
    proc = probe.Proc(probe.find_pid())
    last = {}
    start = time.time()
    while time.time() - start < seconds:
        for name, static, size in targets:
            data = proc.read(proc.u32(static), size)
            previous = last.get(name)
            if previous and len(previous) == len(data):
                for off in range(0, size - 3, 4):
                    old, new = struct.unpack_from("<I", previous, off)[0], struct.unpack_from("<I", data, off)[0]
                    if old != new and old < SMALL_LIMIT and new < SMALL_LIMIT:
                        print(f"{time.time() - start:6.2f}s {name}+{off:#x}: {old:#x} -> {new:#x}", flush=True)
            last[name] = data
        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    main()
