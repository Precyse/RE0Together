"""Read-only: per live enemy, over time: class vtable, AI state (+0x67a4) and its action arguments (+0x67a8, +0x67ac,
+0x67b0, set by the class's setAction, vtable slot 63), motion number (+0x4a4), motion frame (+0x4e0), total frames
(+0x4f4), HP and position. Prints a line whenever the state, action or motion changes.

usage: python enemy_state_probe.py [seconds=30] [interval=0.1] [frames]
Add the word `frames` to print every sample (to see the frame advance). Run it while enemies are walking or attacking
in the loaded room (walk the character near them first).
"""
import struct
import sys
import time

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
every = "frames" in sys.argv[3:]
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ENEMY, POOL, ENTRY_SIZE, OBJECT, SLOTS = 0xDCDC78, 0x4B0, 16, 0xC, 37
ACTION, MOTION, FRAME, TOTAL, HP, POSITION = 0x67A4, 0x4A4, 0x4E0, 0x4F4, 0x1030, 0x40

proc = probe.Proc(probe.find_pid())
last = {}
start = time.time()
while time.time() - start < seconds:
    enemy = proc.u32(S_ENEMY)
    for slot in range(SLOTS):
        obj = proc.u32(enemy + POOL + slot * ENTRY_SIZE + OBJECT) if enemy else 0
        if not obj:
            continue
        action = struct.unpack("<4i", proc.read(obj + ACTION, 16))
        motion = struct.unpack("<H", proc.read(obj + MOTION, 2))[0]
        frame = struct.unpack("<f", proc.read(obj + FRAME, 4))[0]
        total = struct.unpack("<f", proc.read(obj + TOTAL, 4))[0]
        hp = struct.unpack("<i", proc.read(obj + HP, 4))[0]
        x, y, z = struct.unpack("<3f", proc.read(obj + POSITION, 12))
        if every or last.get(slot) != (action, motion):
            print(f"t={time.time() - start:6.2f} slot {slot:2} vt {proc.u32(obj):#x} action {action} "
                  f"motion {motion:#05x} frame {frame:7.2f}/{total:7.2f} hp {hp} pos=({x:.0f},{z:.0f})")
        last[slot] = (action, motion)
    time.sleep(interval)
