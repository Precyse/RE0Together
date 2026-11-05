r"""Checks every method row of the DD2 plugin (adapters/dd2/src/names.h, calls.h) against a type database dump: the
method exists with those parameters, its body (rebased to 0x140000000) is the pinned one, and how many methods share it.

usage: python -I verify_rows.py <methods.tsv of the tdb dump> <live image base of the dump, hex>
       (the dump of build 0x6ae0ce34: coop-scratch\dd2\tdb\methods.tsv, base 7ff74c030000)
Prints OK or BAD per row; exit 1 when any row is BAD. Rows with a share above 1 must never be hooked (names.h rule H2).
"""
import collections
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "adapters" / "dd2" / "src"
ROW = re.compile(r'"((?:app|via|System)\.[^"]+)",\s*"(\w+)",\s*"([^"]*)",\s*(0x[0-9a-fA-F]+)', re.S)
DUMP_BASE = 0x140000000


def main(methods_path, live_base):
    source = (SRC / "calls.h").read_text() + (SRC / "names.h").read_text()
    wanted = {(t, m, p): int(a, 16) for t, m, p, a in ROW.findall(source)}
    found, share = {}, collections.Counter()
    with open(methods_path, encoding="utf-8", errors="replace") as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < 10 or c[9] in ("", "0"):
                continue
            body = int(c[9], 16) - live_base + DUMP_BASE
            share[body] += 1
            params = ",".join(p.strip() for p in c[4].split(",") if p.strip())
            if (c[1], c[2], params) in wanted:
                found[(c[1], c[2], params)] = body
    bad = 0
    for key, pinned in wanted.items():
        ok = found.get(key) == pinned
        bad += not ok
        print("OK " if ok else "BAD", f"{key[0]}.{key[1]}({key[2]})", hex(pinned), "dump", hex(found[key]) if key in found else None,
              "share", share.get(pinned))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2], 16))
