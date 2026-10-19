"""Read-only: both characters' status, hit attributes, +0x1034, think group/state/phase, grab command and motion state,
sampled at --hz, one line whenever one of them changes (the animation frame alone does not count as a change).

Per character: status +0x68c4, hit attributes +0x16ec, uCharaBase +0x1034, think (+0x67b8) group / state / phase, grab
command +0x69cc, motion number +0x4a4, frame +0x4ec, weapon type +0x6950, manual pitch +0x695c.

usage: python player_fields_probe.py [--hz 10] [--seconds 60]
Run it on a solo game (coop=0) and play the moment you want to see: a front grab, a bite from behind, a release by
shooting, an escape by mashing.
"""
import argparse
import struct
import sys
import time

parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
parser.add_argument("--hz", type=float, default=10.0)
parser.add_argument("--seconds", type=float, default=60.0)
args = parser.parse_args()
sys.argv = sys.argv[:1]
import probe  # noqa: E402

S_PLAYER = 0xDCBF3C
CONTROLLED, PARTNER = 0x2C, 0x3C
THINK = 0x67B8
THINK_GROUP, THINK_STATE, THINK_PHASE = 0x8, 0xC, 0x10
STATUS, HIT_ATTRIBUTES, X1034, GRAB_COMMAND = 0x68C4, 0x16EC, 0x1034, 0x69CC
MOTION, FRAME, WEAPON, PITCH = 0x4A4, 0x4EC, 0x6950, 0x695C
NAMES = {0xCCF5A0: "Billy", 0xCCFD10: "Rebecca"}


def read(proc, address, fmt):
    data = proc.read(address, struct.calcsize(fmt))
    return struct.unpack(fmt, data)[0] if len(data) == struct.calcsize(fmt) else None


def sample(proc, player):
    think = proc.u32(player + THINK)
    return {
        "status": read(proc, player + STATUS, "<I"),
        "hit": read(proc, player + HIT_ATTRIBUTES, "<I"),
        "x1034": read(proc, player + X1034, "<I"),
        "think": tuple(read(proc, think + offset, "<i") if think else None
                       for offset in (THINK_GROUP, THINK_STATE, THINK_PHASE)),
        "grab": read(proc, player + GRAB_COMMAND, "<i"),
        "motion": read(proc, player + MOTION, "<H"),
        "weapon": read(proc, player + WEAPON, "<i"),
        "pitch": read(proc, player + PITCH, "<I"),  # raw bits: a NaN would never compare equal
    }


def show(elapsed, label, name, fields, frame):
    group, state, phase = fields["think"]
    pitch = struct.unpack("<f", struct.pack("<I", fields["pitch"]))[0]
    print(f"t={elapsed:7.2f} {label:10} {name:8} status {fields['status']:#010x} hit {fields['hit']:#06x} "
          f"x1034 {fields['x1034']:#x} think {group}/{state}/{phase} grab {fields['grab']} "
          f"motion {fields['motion']:#05x} frame {frame:7.2f} weapon {fields['weapon']} pitch {pitch:.3f}")


def main():
    proc = probe.Proc(probe.find_pid())
    last = {}
    start = time.time()
    while time.time() - start < args.seconds:
        player = proc.u32(S_PLAYER)
        for label, offset in (("controlled", CONTROLLED), ("partner", PARTNER)):
            obj = proc.u32(player + offset) if player else 0
            if not obj:
                continue
            fields = sample(proc, obj)
            if None in (fields["status"], fields["hit"], fields["x1034"], fields["grab"], fields["motion"],
                        fields["weapon"], fields["pitch"]) or None in fields["think"]:
                continue
            if last.get(label) != fields:
                frame = read(proc, obj + FRAME, "<f") or 0.0
                show(time.time() - start, label, NAMES.get(proc.u32(obj), "?"), fields, frame)
                last[label] = fields
        time.sleep(1.0 / args.hz)


if __name__ == "__main__":
    main()
