"""savefmt on copies of saves: decrypt then encrypt gives the file back, and the decrypted segments look right (a text
chunk, a PNG thumbnail). Usage: python savefmt_test.py [FILE...]  (default: the copies in G:/coop-scratch/ds2/savefmt)"""
import struct
import sys
from pathlib import Path

import savefmt

DEFAULT_DIR = Path(r"G:\coop-scratch\ds2\savefmt")
PNG_MAGIC = b"\x89PNG"
EXPECTED_COUNT_OFFSET = 2


def check_save(path):
    save = path.read_bytes()
    index, sizes, chunks = savefmt.segments(save)
    problems = []
    if savefmt.assemble(save[:savefmt.HEADER_BYTES], index, sizes, chunks) != save:
        problems.append("re-encrypt differs")
    if struct.unpack_from("<H", index, EXPECTED_COUNT_OFFSET)[0] != len(sizes):
        problems.append("index count differs from the table")
    if not chunks[1].startswith(PNG_MAGIC):
        problems.append("second chunk is not the PNG thumbnail")
    return problems


def main():
    files = [Path(a) for a in sys.argv[1:]] or sorted(DEFAULT_DIR.glob("*.dat"))
    if not files:
        print("no save copies found")
        return 1
    failed = False
    for path in files:
        problems = check_save(path)
        print(f"{path.name}: " + ("ok" if not problems else "; ".join(problems)))
        failed |= bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
