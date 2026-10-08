"""Static: read RE0 event-collision files (nativePC/scene/event/*.evc2, MT Framework XFS v15) from disk.

usage: python evc2.py <file.evc2>              # schema, regions, triangles
       python evc2.py --summary <dir> <outdir> # one .txt per file plus evc2_index.tsv

Every .evc2 shipped with the game has the same three classes: root {mData: obj, mQuality: u32},
obj {mAutoDelete: bool, mpArray: obj[]}, region {data: triangle[]}. They hold trigger-volume geometry
(one region = one triangle list) and no flags or script ops; the scripts are the room .bes2 files (bes2.py).

XFS layout (little-endian): "XFS\\0", u16 version, u16 minor, u32 unknown, u32 0, u32 class count, u32 schema size,
then (at 0x18) per class a u32 offset to {u32 name hash, u32 member count, members of 0x18 bytes
{u32 name offset, u8 type, u8 attr, u16 element size, 16 bytes unused}}; names and offsets are from 0x18.
Data starts at 0x18 + schema size: an object is {u16 (class index << 1 | 1), u16 id, u32 size} then per member a
u32 element count and the elements; class-typed elements (types 1, 2) are nested objects.
"""
import os
import struct
import sys

SCHEMA_BASE = 0x18
MEMBER_SIZE = 0x18
TYPE_CLASS = 0x01
TYPE_CLASSREF = 0x02
TYPE_BOOL = 0x03
TYPE_U32 = 0x06
TYPE_TRIANGLE = 0x32
TRIANGLE_FLOATS = 12
XFS_MAGIC = b"XFS\0"


def read_schema(b):
    if b[:4] != XFS_MAGIC:
        raise ValueError("not an XFS file")
    count, size = struct.unpack_from("<II", b, 0x10)
    classes = []
    for off in struct.unpack_from("<%dI" % count, b, SCHEMA_BASE):
        p = SCHEMA_BASE + off
        name_hash, n = struct.unpack_from("<II", b, p)
        members = []
        for i in range(n):
            name_off, typ, attr, elem = struct.unpack_from("<IBBH", b, p + 8 + i * MEMBER_SIZE)
            s = SCHEMA_BASE + name_off
            members.append((b[s:b.index(b"\0", s)].decode("latin1"), typ, attr, elem))
        classes.append((name_hash, members))
    return classes, SCHEMA_BASE + size


def read_value(b, p, typ, elem, classes):
    if typ in (TYPE_CLASS, TYPE_CLASSREF):
        return read_object(b, p, classes)
    raw = b[p:p + elem]
    if typ == TYPE_BOOL:
        value = raw[0]
    elif typ == TYPE_U32:
        value = struct.unpack("<I", raw)[0]
    elif typ == TYPE_TRIANGLE:
        f = struct.unpack("<%df" % TRIANGLE_FLOATS, raw)
        value = [f[0:3], f[4:7], f[8:11]]
    else:
        raise ValueError("unsupported XFS type 0x%02x at 0x%x" % (typ, p))
    return value, p + elem


def read_object(b, p, classes):
    tag, obj_id, size = struct.unpack_from("<HHI", b, p)
    if not tag & 1:
        raise ValueError("unexpected object tag 0x%04x at 0x%x" % (tag, p))
    end = p + 4 + size
    q = p + 8
    obj = {}
    for name, typ, attr, elem in classes[tag >> 1][1]:
        (n,) = struct.unpack_from("<I", b, q)
        q += 4
        values = []
        for _ in range(n):
            v, q = read_value(b, q, typ, elem, classes)
            values.append(v)
        obj[name] = values
    if q != end:
        raise ValueError("object at 0x%x: parsed to 0x%x, size says 0x%x" % (p, q, end))
    return obj, q


def load(path):
    b = open(path, "rb").read()
    classes, data = read_schema(b)
    root, end = read_object(b, data, classes)
    if end != len(b):
        raise ValueError("%s: %d trailing bytes" % (path, len(b) - end))
    return classes, root


def regions(root):
    """[(index, triangles)] of the root's mData.mpArray."""
    holder = root["mData"][0]
    return [(i, r["data"]) for i, r in enumerate(holder["mpArray"])]


def bounds(tris):
    pts = [v for t in tris for v in t]
    return [min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]


def fmt3(v):
    return "(%.1f, %.1f, %.1f)" % tuple(v)


def describe(path, with_triangles=True):
    classes, root = load(path)
    holder = root["mData"][0]
    rs = regions(root)
    lines = ["file %s  size %d" % (os.path.basename(path), os.path.getsize(path)),
             "mQuality %d  mAutoDelete %d  regions %d  triangles %d"
             % (root["mQuality"][0], holder["mAutoDelete"][0], len(rs), sum(len(t) for _, t in rs))]
    for name_hash, members in classes:
        lines.append("; class %08x { %s }" % (name_hash, ", ".join(
            "%s:0x%02x/%d" % (n, t, e) for n, t, a, e in members)))
    for i, tris in rs:
        if not tris:
            lines.append("region %2d: 0 triangles" % i)
            continue
        lo, hi = bounds(tris)
        lines.append("region %2d: %3d triangles  min %s  max %s" % (i, len(tris), fmt3(lo), fmt3(hi)))
        if with_triangles:
            lines.extend("    %s %s %s" % tuple(fmt3(v) for v in t) for t in tris)
    return "\n".join(lines) + "\n", root


def summarize(src, out):
    os.makedirs(out, exist_ok=True)
    rows = ["file\troom\tvariant\tsize\tmQuality\tmAutoDelete\tregions\ttriangles\tper_region"]
    for f in sorted(os.listdir(src)):
        if not f.endswith(".evc2"):
            continue
        text, root = describe(os.path.join(src, f))
        with open(os.path.join(out, f[:-5] + ".txt"), "w") as fh:
            fh.write(text)
        holder = root["mData"][0]
        rs = regions(root)
        stem = f[:-5]
        rows.append("\t".join(map(str, [f, stem[:-1], stem[-1], os.path.getsize(os.path.join(src, f)),
                                        root["mQuality"][0], holder["mAutoDelete"][0], len(rs),
                                        sum(len(t) for _, t in rs), ",".join(str(len(t)) for _, t in rs)])))
    with open(os.path.join(out, "evc2_index.tsv"), "w") as fh:
        fh.write("\n".join(rows) + "\n")
    return len(rows) - 1


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--summary":
        print("%d files" % summarize(sys.argv[2], sys.argv[3]))
    elif len(sys.argv) == 2:
        sys.stdout.write(describe(sys.argv[1])[0])
    else:
        print(__doc__)
