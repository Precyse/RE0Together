"""FACT_SET wire format round trip between this script (the fake peer's encoder) and the adapter's C++ codec
(adapters/ds2/build/fact_wire_test.exe). No game needed.

usage: python fact_wire_test.py
"""
import os
import struct
import subprocess
import sys
import tempfile

EXE = os.path.join(os.path.dirname(__file__), "..", "..", "adapters", "ds2", "build", "fact_wire_test.exe")
FACT_SET = 0x010B
KIND_BOOL, KIND_INT = 1, 2
FLAG_ARG5, FLAG_ARG6 = 1, 2
HEADER = struct.Struct("<II")  # count, reserved
ENTRY = struct.Struct("<BBH16sI")  # kind, flags, reserved, uuid, value


def uuid_from(seed):
    return bytes((seed + i) & 0xFF for i in range(16))


def encode(entries):
    return HEADER.pack(len(entries), 0) + b"".join(ENTRY.pack(k, f, 0, u, v) for k, f, u, v in entries)


def decode(payload):
    count, _ = HEADER.unpack_from(payload)
    return [(k, f, u, v) for k, f, _, u, v in ENTRY.iter_unpack(payload[HEADER.size:HEADER.size + count * ENTRY.size])]


def run(mode, path):
    return subprocess.run([EXE, mode, path], capture_output=True, text=True)


def main():
    failures = 0
    sent = [(KIND_BOOL, FLAG_ARG5, uuid_from(1), 1), (KIND_INT, FLAG_ARG5 | FLAG_ARG6, uuid_from(40), 0xFFFFFFFE)]
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "payload.bin")
        with open(path, "wb") as f:
            f.write(encode(sent))
        result = run("--decode", path)
        seen = [tuple(int(x) for x in line.split()) for line in result.stdout.splitlines()]
        want = [(k, f, *u, v) for k, f, u, v in sent]
        ok = result.returncode == 0 and seen == want
        print("ok  " if ok else "FAIL", "python payload decodes the same in C++")
        failures += not ok

        run("--encode", path)
        with open(path, "rb") as f:
            got = decode(f.read())
        ok = got == [(KIND_BOOL, FLAG_ARG5, uuid_from(1), 1), (KIND_INT, FLAG_ARG6, uuid_from(40), 0xFFFFFFFE)]
        print("ok  " if ok else "FAIL", "C++ payload decodes the same in python")
        failures += not ok

        with open(path, "wb") as f:
            f.write(encode(sent)[:-1])
        ok = run("--decode", path).returncode == 2
        print("ok  " if ok else "FAIL", "C++ rejects a truncated python payload")
        failures += not ok
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
