"""Query the DS2 type schema extracted by odradek (reflected classes, fields at absolute offsets, enums).

The schema is odradek's `types.json` for DS2 v1.10.89.0 (odradek-game-ds2/src/main/resources). It lists only
reflected (serialised or script-visible) members: runtime-only members such as DSBaggage +0x28 are not in it.
Output stays on G: (derived from game data, not committed); override the file with DS2_TYPES.

usage: python ds2types.py <Name>            # class with inherited fields, or enum values
       python ds2types.py find <regex>      # type names matching
       python ds2types.py field <regex>     # classes (with inherited fields) that have a field matching
       python ds2types.py at <Name> <hex>   # field of a class covering an absolute offset
"""
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_SCHEMA = Path(r"G:\coop-scratch\ds2\types\ds2_types.json")


def load():
    return json.load(open(os.environ.get("DS2_TYPES", DEFAULT_SCHEMA)))


def fields(types, name, base=0):
    """Yield (absolute offset, field name, type, declaring class), bases first."""
    info = types.get(name)
    if not info or info["kind"] != "compound":
        return
    for b in info.get("bases", []):
        yield from fields(types, b["type"], base + b["offset"])
    for a in info.get("attrs", []):
        if "name" in a:
            yield base + a["offset"], a["name"], a["type"], name


def bases_chain(types, name):
    info = types.get(name)
    return [b["type"] for b in info.get("bases", [])] if info else []


def show(types, name):
    info = types.get(name)
    if not info:
        sys.exit(f"{name}: not found (try: ds2types.py find {name})")
    if info["kind"].startswith("enum"):
        print(f"{name} {info['kind']} size={info['size']}")
        for v in info["values"]:
            print(f"  {v['value']:>6} {v['name']}")
        return
    print(f"{name} ({info['kind']}) bases={bases_chain(types, name)} messages={info.get('messages', [])}")
    for off, fname, ftype, owner in sorted(fields(types, name)):
        print(f"  +{off:#06x} {fname}: {ftype}" + ("" if owner == name else f"   [{owner}]"))


def main(argv):
    if not argv:
        sys.exit(__doc__)
    types = load()
    cmd = argv[0]
    if cmd == "find" and len(argv) == 2:
        print("\n".join(n for n in sorted(types) if re.search(argv[1], n)))
    elif cmd == "field" and len(argv) == 2:
        for n in sorted(types):
            hits = [f for f in fields(types, n) if re.search(argv[1], f[1])]
            for off, fname, ftype, owner in hits:
                print(f"{n} +{off:#x} {fname}: {ftype} [{owner}]")
    elif cmd == "at" and len(argv) == 3:
        target = int(argv[2], 16)
        for off, fname, ftype, owner in sorted(fields(types, argv[1])):
            if off <= target < off + 8:
                print(f"+{off:#x} {fname}: {ftype} [{owner}]")
    else:
        show(types, cmd)


if __name__ == "__main__":
    main(sys.argv[1:])
