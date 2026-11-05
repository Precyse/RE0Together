"""Decodes a Crimson Desert funnel capture (bin64\\coop\\capture-*.cdcap, adapter.ini capture=1) with message names.

usage: python -I capture_dump.py <capture.cdcap> <troc_catalogue.tsv> [--summary] [--golden <dir>] [--only <Name,...>]

  default    one line per packet: seconds since the first, direction, thread, opcode, name, size, first payload bytes
  --summary  per message: count, distinct sizes (the exact sizes size_table.h needs), threads seen
  --golden   writes the first packet of each (message, size) pair to <dir>/<Name>_<size>.bin for the size table test

The catalogue file is the one the adapter writes next to the capture (bin64\\coop\\troc_catalogue.tsv); formats in
adapters/cd/src/capture.h and troc_catalogue.cpp.
"""
import argparse
import csv
import struct
import sys
from collections import defaultdict
from pathlib import Path

FILE_HEADER = struct.Struct("<8sIIQ")
RECORD = struct.Struct("<QIHBBII")
MAGIC = b"CDCAP1\0\0"
PACKET_HEADER = 5
DIRECTIONS = ("C2S", "S2C")
PREVIEW_BYTES = 16


def read_catalogue(path):
    with open(path, encoding="utf-8", newline="") as f:
        return {int(row["opcode"]): row["name"] for row in csv.DictReader(f, delimiter="\t")}


def records(path):
    data = Path(path).read_bytes()
    magic, stamp, _, frequency = FILE_HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        sys.exit(f"{path}: not a capture file")
    offset, first = FILE_HEADER.size, None
    while offset + RECORD.size <= len(data):
        qpc, thread, size, direction, session, channel, flags = RECORD.unpack_from(data, offset)
        offset += RECORD.size
        body = data[offset:offset + size]
        offset += size
        if len(body) < size:
            break  # the game stopped mid-write
        first = qpc if first is None else first
        opcode = struct.unpack_from("<H", body, 0)[0] if size >= 2 else -1
        yield {"t": (qpc - first) / frequency, "thread": thread, "dir": DIRECTIONS[direction & 1], "session": session,
               "channel": channel, "flags": flags, "opcode": opcode, "size": size, "body": body, "stamp": stamp}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture")
    parser.add_argument("catalogue")
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--golden")
    parser.add_argument("--only")
    args = parser.parse_args()
    names = read_catalogue(args.catalogue)
    only = set(args.only.split(",")) if args.only else None
    sizes, counts, threads, golden = defaultdict(set), defaultdict(int), defaultdict(set), {}
    for r in records(args.capture):
        name = names.get(r["opcode"], "?")
        if only and name not in only:
            continue
        key = (r["dir"], name)
        counts[key] += 1
        sizes[key].add(r["size"] - PACKET_HEADER)
        threads[key].add(r["thread"])
        golden.setdefault((name, r["size"] - PACKET_HEADER), r["body"])
        if not args.summary and not args.golden:
            preview = r["body"][PACKET_HEADER:PACKET_HEADER + PREVIEW_BYTES].hex(" ")
            print(f"{r['t']:10.3f} {r['dir']} t{r['thread']:<6} {r['opcode']:5} {name:40} {r['size'] - PACKET_HEADER:5}  {preview}")
    if args.summary:
        for (direction, name), n in sorted(counts.items(), key=lambda item: -item[1]):
            shown = sorted(sizes[(direction, name)])
            size_text = ",".join(map(str, shown[:8])) + ("..." if len(shown) > 8 else "")
            print(f"{direction} {name:40} {n:8}  payload sizes {size_text}  threads {len(threads[(direction, name)])}")
    if args.golden:
        out = Path(args.golden)
        out.mkdir(parents=True, exist_ok=True)
        for (name, payload), body in golden.items():
            (out / f"{name}_{payload}.bin").write_bytes(body)
        print(f"{len(golden)} golden packets in {out}")


if __name__ == "__main__":
    main()
