"""Static scan of a Decima executable's reflection data (RTTI): type names, bases, and field offsets.

Decima describes its classes in static RTTI records (kind byte, name, bases, attributes with offsets). They sit in
initialized data with an id of -1 until the factory registers them, so they can be read from the file without
running the game. Output stays local (tools/ds/out is git-ignored): it is derived from game code.

usage: python rtti_scan.py <exe> scan                 # write out/<exe>.types.json
       python rtti_scan.py <exe> type <Name>          # one compound with inherited fields at absolute offsets
       python rtti_scan.py <exe> find <regex>         # compound names matching
       python rtti_scan.py <exe> field <regex>        # compounds that have a field whose name matches
"""
import json
import re
import sys
from pathlib import Path

from pe_image import PeImage

OUT_DIR = Path(__file__).parent / "out"

KIND_ATOM, KIND_POINTER, KIND_CONTAINER, KIND_ENUM, KIND_COMPOUND, KIND_ENUM_FLAGS, KIND_POD, KIND_ENUM_BITSET = range(8)
UNREGISTERED_ID = 0xFFFFFFFF

# RTTICompound layout (x64): counts at +6..+9, size +0x10, name +0x40, bases +0x58, attrs +0x60.
COMPOUND_NUM_BASES, COMPOUND_NUM_ATTRS, COMPOUND_NUM_HANDLERS = 0x6, 0x7, 0x8
COMPOUND_SIZE, COMPOUND_NAME, COMPOUND_BASES, COMPOUND_ATTRS = 0x10, 0x40, 0x58, 0x60
BASE_STRIDE, BASE_OFFSET = 0x10, 0x8
ATTR_STRIDE, ATTR_OFFSET, ATTR_FLAGS, ATTR_NAME = 0x38, 0x8, 0xA, 0x10
# Atom, enum, POD: name at +0x10. Pointer and container: item type +0x8, data +0x10 (data name at +0).
SIMPLE_NAME = 0x10
WRAPPER_ITEM, WRAPPER_DATA = 0x8, 0x10
MAX_NAME_DEPTH = 4

HEADER = re.compile(rb"\xff\xff\xff\xff[\x00-\x07]", re.DOTALL)


class Scanner:
    def __init__(self, img):
        self.img = img

    def data_ptr(self, va):
        return va and self.img.in_section(va, ".data", ".rdata")

    def type_name(self, va, depth=0):
        img = self.img
        if not self.data_ptr(va) or depth > MAX_NAME_DEPTH:
            return "?"
        kind = img.u8(va + 4)
        if kind == KIND_COMPOUND:
            return img.cstr(img.ptr(va + COMPOUND_NAME)) or "?"
        if kind in (KIND_POINTER, KIND_CONTAINER):
            data = img.ptr(va + WRAPPER_DATA)
            outer = img.cstr(img.ptr(data)) if self.data_ptr(data) else None
            return f"{outer or '?'}<{self.type_name(img.ptr(va + WRAPPER_ITEM), depth + 1)}>"
        return img.cstr(img.ptr(va + SIMPLE_NAME)) or "?"

    def compound(self, va):
        img = self.img
        name = img.cstr(img.ptr(va + COMPOUND_NAME))
        if not name or not name.isidentifier():
            return None
        nb, na = img.u8(va + COMPOUND_NUM_BASES), img.u8(va + COMPOUND_NUM_ATTRS)
        bases_va, attrs_va = img.ptr(va + COMPOUND_BASES), img.ptr(va + COMPOUND_ATTRS)
        if (nb and not self.data_ptr(bases_va)) or (na and not self.data_ptr(attrs_va)):
            return None
        bases = []
        for i in range(nb):
            b = bases_va + i * BASE_STRIDE
            bases.append({"type": self.type_name(img.ptr(b)), "offset": img.u32(b + BASE_OFFSET)})
        attrs = []
        for i in range(na):
            a = attrs_va + i * ATTR_STRIDE
            t = img.ptr(a)
            attr_name = img.cstr(img.ptr(a + ATTR_NAME))
            if not attr_name or (t and not self.data_ptr(t)):
                break  # the count byte overshoots on some types; the table ends at the first malformed entry
            if not t:
                attrs.append({"category": attr_name})
                continue
            attrs.append({"name": attr_name, "offset": img.u16(a + ATTR_OFFSET), "flags": img.u16(a + ATTR_FLAGS),
                          "type": self.type_name(t)})
        return {"name": name, "va": hex(va), "size": img.u32(va + COMPOUND_SIZE), "bases": bases, "attrs": attrs}

    def scan(self):
        lo, hi = self.img.section(".data")
        data = self.img.read(lo, hi - lo)
        types = {}
        for m in HEADER.finditer(data):
            va = lo + m.start()
            if data[m.start() + 4] != KIND_COMPOUND:
                continue
            c = self.compound(va)
            if c and c["name"] not in types:
                types[c["name"]] = c
        return types


def load(exe):
    out = OUT_DIR / (Path(exe).stem + ".types.json")
    if not out.exists():
        OUT_DIR.mkdir(exist_ok=True)
        types = Scanner(PeImage(exe)).scan()
        out.write_text(json.dumps(types, indent=1))
        print(f"scanned {len(types)} compound types -> {out}")
    return json.loads(out.read_text())


def print_type(types, name, base_offset=0, depth=0):
    t = types.get(name)
    pad = "  " * depth
    if not t:
        print(f"{pad}{name} (not found)")
        return
    print(f"{pad}{name} size=0x{t['size']:x} @+0x{base_offset:x}")
    for b in t["bases"]:
        print_type(types, b["type"], base_offset + b["offset"], depth + 1)
    for a in t["attrs"]:
        if "category" in a:
            print(f"{pad}  [{a['category']}]")
        else:
            print(f"{pad}  +0x{base_offset + a['offset']:04x} {a['type']} {a['name']}")


def main():
    exe, cmd = sys.argv[1], sys.argv[2]
    types = load(exe)
    if cmd == "scan":
        return
    arg = sys.argv[3]
    if cmd == "type":
        print_type(types, arg)
    elif cmd == "find":
        for n in sorted(types):
            if re.search(arg, n):
                print(n, hex(types[n]["size"]))
    elif cmd == "field":
        for n, t in sorted(types.items()):
            hits = [a for a in t["attrs"] if "name" in a and re.search(arg, a["name"])]
            for a in hits:
                print(f"{n} +0x{a['offset']:x} {a['type']} {a['name']}")


if __name__ == "__main__":
    main()
