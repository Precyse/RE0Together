"""Print both characters' room record, scene id, in-room flag and position from the running game (read only).

usage: python party_state.py
"""
import struct
import sys

sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_PLAYER = 0xDCBF3C
S_SCENE_INFO = 0xDCBF40
CONTROLLED, PARTNER = 0x2C, 0x3C
FLAGS, IN_ROOM = 0xC, 0x4000
POSITION = 0x40
RECORD = 0xFF4
CURRENT_RECORD = 0x20
RECORD_SCENE = 8
NAMES = {0xCCF5A0: "Billy", 0xCCFD10: "Rebecca"}


def main():
    proc = probe.Proc(probe.find_pid())
    player = proc.u32(S_PLAYER)
    current = proc.u32(proc.u32(S_SCENE_INFO) + CURRENT_RECORD)
    print(f"loaded record {current:#x} scene {proc.u32(current + RECORD_SCENE):#x}")
    for label, offset in (("controlled", CONTROLLED), ("partner", PARTNER)):
        obj = proc.u32(player + offset)
        record = proc.u32(obj + RECORD)
        x, y, z = struct.unpack("<3f", proc.read(obj + POSITION, 12))
        in_room = bool(proc.u32(obj + FLAGS) & IN_ROOM)
        print(f"{label:10} {NAMES.get(proc.u32(obj), '?'):8} record {record:#x} scene {proc.u32(record + RECORD_SCENE):#x} "
              f"in_room={in_room} pos=({x:.0f}, {y:.0f}, {z:.0f})")


if __name__ == "__main__":
    main()
