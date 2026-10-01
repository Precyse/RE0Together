"""List the live enemies of the loaded room from the running game (read only): pool slot, class vtable, HP, position.

usage: python enemies.py
"""
import struct
import sys

sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ENEMY = 0xDCDC78
POOL, ENTRY_SIZE, OBJECT, SLOTS = 0x4B0, 16, 0xC, 37
HP, POSITION = 0x1030, 0x40


def main():
    proc = probe.Proc(probe.find_pid())
    enemy = proc.u32(S_ENEMY)
    found = 0
    for slot in range(SLOTS):
        obj = proc.u32(enemy + POOL + slot * ENTRY_SIZE + OBJECT)
        if not obj:
            continue
        hp = struct.unpack("<i", proc.read(obj + HP, 4))[0]
        x, y, z = struct.unpack("<3f", proc.read(obj + POSITION, 12))
        print(f"slot {slot:2} vtable {proc.u32(obj):#x} hp {hp} pos=({x:.0f}, {y:.0f}, {z:.0f})")
        found += 1
    print(f"{found} enemies")


if __name__ == "__main__":
    main()
