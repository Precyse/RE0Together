"""Read-only: what changes in the running game when a character equips another weapon.

usage: python equip_trace.py before       # with the handgun equipped: saves snapshot A
       python equip_trace.py after        # after equipping the shotgun: saves snapshot B and prints every change
       python equip_trace.py show         # prints the changes again from the two saved snapshots

Snapshot = sItem (inventory blocks), sPlayer, both characters' player objects (controlled and partner), and every
heap object those point at whose MT class name looks weapon related (uWeapon, sWeapon, IGameWeapon, uGUIWeapon,
cCharaResourceWeapon...), with their first WEAPON_BYTES bytes. Nothing is written to the game.
"""
import os
import pickle
import struct
import sys

mode = sys.argv[1] if len(sys.argv) > 1 else ""
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ITEM, S_PLAYER = 0xDCBF44, 0xDCBF3C
CONTROLLED, PARTNER = 0x2C, 0x3C
ITEM_BYTES, PLAYER_BYTES, PLAYER_OBJECT_BYTES, WEAPON_BYTES = 0x100, 0x400, 0x7000, 0x300
IMAGE_END = 0x1200000
HEAP_START = 0x01000000
STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "equip_trace.pkl")
WEAPON_WORDS = ("weapon", "wp", "gun")
MAX_REPORTED_CHANGES = 400


def is_heap_pointer(value):
    return HEAP_START <= value < 0x7FFF0000


def weapon_objects(proc, blob):
    """Heap objects pointed at from blob whose class name looks weapon related: {address: class name}."""
    found = {}
    for off in range(0, len(blob) - 3, 4):
        value = struct.unpack_from("<I", blob, off)[0]
        if not is_heap_pointer(value) or value in found:
            continue
        vtable = proc.u32(value)
        if not (probe.IMAGE_BASE <= vtable < IMAGE_END):
            continue
        name = probe.class_name(proc, value)
        if name and any(word in name.lower() for word in WEAPON_WORDS):
            found[value] = name
    return found


def capture(proc):
    player = proc.u32(S_PLAYER)
    regions = {
        "sItem": (proc.u32(S_ITEM), ITEM_BYTES),
        "sPlayer": (player, PLAYER_BYTES),
        "controlled": (proc.u32(player + CONTROLLED), PLAYER_OBJECT_BYTES),
        "partner": (proc.u32(player + PARTNER), PLAYER_OBJECT_BYTES),
    }
    snap = {name: (addr, proc.read(addr, size)) for name, (addr, size) in regions.items() if addr}
    objects = {}
    for _, blob in snap.values():
        objects.update(weapon_objects(proc, blob))
    for addr, name in objects.items():
        snap[f"{name}@{addr:#x}"] = (addr, proc.read(addr, WEAPON_BYTES))
    return snap


def show(a, b):
    changes = 0
    for key in sorted(set(a) | set(b)):
        if key not in a or key not in b:
            where = "appeared" if key in b else "disappeared"
            print(f"{key}: object {where}")
            continue
        base, old = a[key]
        new = b[key][1]
        for off in range(0, min(len(old), len(new)) - 3, 4):
            if old[off:off + 4] == new[off:off + 4]:
                continue
            (x,), (y,) = struct.unpack_from("<I", old, off), struct.unpack_from("<I", new, off)
            (fx,), (fy,) = struct.unpack_from("<f", old, off), struct.unpack_from("<f", new, off)
            print(f"{key:36} +{off:#06x} ({base + off:#x})  {x:#010x} -> {y:#010x}   {fx:.4g} -> {fy:.4g}")
            changes += 1
            if changes >= MAX_REPORTED_CHANGES:
                print("(stopped: too many changes, equip nothing else between the two snapshots)")
                return
    print(f"{changes} changed dwords")


def main():
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    if mode == "before":
        saved = {"a": capture(probe.Proc(probe.find_pid()))}
        pickle.dump(saved, open(STATE_FILE, "wb"))
        print("snapshot A saved;", len(saved["a"]), "regions")
    elif mode == "after":
        saved = pickle.load(open(STATE_FILE, "rb"))
        saved["b"] = capture(probe.Proc(probe.find_pid()))
        pickle.dump(saved, open(STATE_FILE, "wb"))
        show(saved["a"], saved["b"])
    elif mode == "show":
        saved = pickle.load(open(STATE_FILE, "rb"))
        show(saved["a"], saved["b"])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
