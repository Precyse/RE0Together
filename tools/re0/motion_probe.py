"""Read-only: the uModel motion block of live enemies (offsets 0x490..0x510), sampled five times.

usage: python motion_probe.py [slot ...]   (-1 = the controlled player)
"""
import struct
import sys
import time

slots = [int(a) for a in sys.argv[1:]] or [-1, 2]
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ENEMY, POOL, ENTRY_SIZE, OBJECT = 0xDCDC78, 0x4B0, 16, 0xC
BLOCK_START, BLOCK_SIZE, SAMPLES, INTERVAL = 0x490, 0x80, 5, 0.2

proc = probe.Proc(probe.find_pid())
enemy = proc.u32(S_ENEMY)
S_PLAYER, CONTROLLED = 0xDCBF3C, 0x2C
PLAYER_SLOT = -1
for slot in slots:
    if slot == PLAYER_SLOT:
        obj = proc.u32(proc.u32(S_PLAYER) + CONTROLLED)
    else:
        obj = proc.u32(enemy + POOL + slot * ENTRY_SIZE + OBJECT)
    print("slot", slot, hex(obj))
    for _ in range(SAMPLES):
        data = proc.read(obj + BLOCK_START, BLOCK_SIZE)
        print(" ".join("%08x" % w for w in struct.unpack("<32I", data)))
        floats = struct.unpack("<32f", data)
        print("  floats", " ".join("%.2f" % f for f in floats[:32]))
        time.sleep(INTERVAL)
