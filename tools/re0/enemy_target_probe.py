"""Read-only: where an enemy object keeps its target (the player it is after). Scans every live enemy's first 0x7000
bytes for the controlled player's and the partner's object addresses, over several samples, and reports the offsets
that hold one of them.

usage: python enemy_target_probe.py [seconds=20] [interval=0.5]
Run it with enemies awake and chasing (walk the character near them), then walk the other character closer to see
the pointer switch. Offsets that always hold a player pointer are the target field.
"""
import collections
import struct
import sys
import time

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_ENEMY, POOL, ENTRY_SIZE, OBJECT, SLOTS = 0xDCDC78, 0x4B0, 16, 0xC, 37
S_PLAYER, CONTROLLED, PARTNER = 0xDCBF3C, 0x2C, 0x3C
SCAN_BYTES = 0x7000

proc = probe.Proc(probe.find_pid())
seen = collections.defaultdict(collections.Counter)
samples = 0
start = time.time()
while time.time() - start < seconds:
    player = proc.u32(S_PLAYER)
    players = {proc.u32(player + CONTROLLED): "controlled", proc.u32(player + PARTNER): "partner"}
    enemy = proc.u32(S_ENEMY)
    for slot in range(SLOTS):
        obj = proc.u32(enemy + POOL + slot * ENTRY_SIZE + OBJECT) if enemy else 0
        if not obj:
            continue
        blob = proc.read(obj, SCAN_BYTES)
        for off in range(0, len(blob) - 3, 4):
            who = players.get(struct.unpack_from("<I", blob, off)[0])
            if who:
                seen[off][who] += 1
    samples += 1
    time.sleep(interval)
print(f"{samples} samples")
for off, who in sorted(seen.items()):
    print(f"+{off:#06x} {dict(who)}")
