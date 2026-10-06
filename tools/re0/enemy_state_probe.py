"""Read-only: per live enemy, over time: class vtable, AI state index (+0x67a4), motion number (+0x4a4), motion frame
(+0x4e0), total frames (+0x4f4), HP and position. Prints a line whenever the state, motion or frame changes much.

usage: python enemy_state_probe.py [seconds=30] [interval=0.1]
Run it while enemies are walking or attacking in the loaded room (walk the character near them first).
"""
import struct
import sys
import time

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ENEMY, POOL, ENTRY_SIZE, OBJECT, SLOTS = 0xDCDC78, 0x4B0, 16, 0xC, 37
STATE, MOTION, FRAME, TOTAL, HP, POSITION = 0x67A4, 0x4A4, 0x4E0, 0x4F4, 0x1030, 0x40

proc = probe.Proc(probe.find_pid())
last = {}
end = time.time() + seconds
while time.time() < end:
    enemy = proc.u32(S_ENEMY)
    for slot in range(SLOTS):
        obj = proc.u32(enemy + POOL + slot * ENTRY_SIZE + OBJECT) if enemy else 0
        if not obj:
            continue
        state = proc.u32(obj + STATE)
        motion = struct.unpack("<H", proc.read(obj + MOTION, 2))[0]
        frame, total = struct.unpack("<f", proc.read(obj + FRAME, 4))[0], struct.unpack("<f", proc.read(obj + TOTAL, 4))[0]
        hp = struct.unpack("<i", proc.read(obj + HP, 4))[0]
        x, y, z = struct.unpack("<3f", proc.read(obj + POSITION, 12))
        key = (state, motion)
        if last.get(slot, (None, None, None))[:2] != key or abs(frame - last[slot][2]) > 40:
            print(f"t={seconds - (end - time.time()):6.2f} slot {slot:2} vt {proc.u32(obj):#x} state {state} "
                  f"motion {motion:#05x} frame {frame:7.2f}/{total:7.2f} hp {hp} pos=({x:.0f},{z:.0f})")
        last[slot] = (state, motion, frame)
    time.sleep(interval)
