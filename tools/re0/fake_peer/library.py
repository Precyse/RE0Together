"""The frame journal of a run (frames.jsonl) and the library built from past runs.

Journal: one entry per frame in either direction {t wall clock ms, dir, type, slot, flags, payload} and one per note. The
library indexes the frames of every run under the output root by kind (door, pose clip, enemy clip, hit request, lease
grant, floor item, box); scenarios ask it and skip a gate, naming the run that would fill it, when nothing is there.
"""
import json
import threading
import time
from pathlib import Path

from fake_peer import wire
from fake_peer import wire_check
from fake_peer.wire import Msg

LIBRARY_FILE = "library.json"
FRAMES_FILE = "frames.jsonl"
CLIP_MARK = "clip"
MARK_START, MARK_END = "start", "end"
IN, OUT = "in", "out"


def wall_ms():
    return time.time() * 1000.0


class Entry:
    """A journal entry: a frame (type is not None) or a note."""

    __slots__ = ("t", "dir", "type", "slot", "flags", "payload", "note")

    def __init__(self, t, direction=None, msg_type=None, slot=0, flags=0, payload=b"", note=None):
        self.t, self.dir, self.type, self.slot, self.flags, self.payload, self.note = t, direction, msg_type, slot, flags, payload, note

    def as_json(self):
        if self.note is not None:
            return {"t": round(self.t, 3), "note": self.note}
        return {"t": round(self.t, 3), "dir": self.dir, "type": self.type, "slot": self.slot, "flags": self.flags,
                "payload": self.payload.hex()}

    @staticmethod
    def from_json(item):
        if "note" in item:
            return Entry(item["t"], note=item["note"])
        return Entry(item["t"], item["dir"], item["type"], item["slot"], item["flags"], bytes.fromhex(item["payload"]))


class Journal:
    """Every frame and emulator decision of a run, in memory and (when a path is given) in frames.jsonl."""

    def __init__(self, path=None):
        self.entries = []
        self._lock = threading.Lock()
        self._file = open(path, "w", encoding="utf-8") if path else None

    def frame(self, direction, frame, t=None):
        self._add(Entry(wall_ms() if t is None else t, direction, frame.type, frame.slot, frame.flags, frame.payload))

    def note(self, text):
        self._add(Entry(wall_ms(), note=text))

    def _add(self, entry):
        with self._lock:
            self.entries.append(entry)
            if self._file:
                self._file.write(json.dumps(entry.as_json()) + "\n")
                self._file.flush()

    def frames(self, msg_type=None, direction=None, since=0.0, until=None):
        with self._lock:
            snapshot = list(self.entries)
        return [e for e in snapshot if e.type is not None and (msg_type is None or e.type == msg_type) and
                (direction is None or e.dir == direction) and e.t >= since and (until is None or e.t <= until)]

    def notes(self):
        with self._lock:
            return [e for e in self.entries if e.note is not None]

    def close(self):
        if self._file:
            self._file.close()
            self._file = None


def read_journal(path):
    with open(path, encoding="utf-8") as handle:
        return [Entry.from_json(json.loads(line)) for line in handle if line.strip()]


# ---- the library ---------------------------------------------------------------------------------------------------

class Library:
    """Recordings by kind. Items are plain dicts; payloads are bytes. `source` names the run each came from."""

    def __init__(self):
        self.doors = {}
        self.pose_clips = {}
        self.enemy_clips = {}
        self.hit_requests = {}
        self.lease_grants = {}
        self.floor_items = []
        self.boxes = {}
        self.vtables = wire_check.vtables("kEnemyVtables")

    def ingest(self, run, entries):
        """Adds what the frames of one run hold; the same key from a later run replaces the earlier one."""
        frames = [e for e in entries if e.type is not None]
        self._doors(run, frames)
        self._pose_clips(run, entries)
        self._enemy_clips(run, frames)
        for entry in frames:
            if entry.dir == IN:
                self._single(run, entry)

    def _doors(self, run, frames):
        pending = []
        for entry in frames:
            if entry.dir != IN:
                continue
            if entry.type == Msg.DOOR_FOLLOW and len(entry.payload) == wire.DoorFollow.size:
                pending.append((run, wire.DoorFollow.unpack(entry.payload)))
            elif entry.type == Msg.POSE_STATE and pending and len(entry.payload) == wire.PoseState.size:
                pose = wire.PoseState.unpack(entry.payload)
                for source, follow in list(pending):
                    if pose.room == follow.scene:
                        self.doors[(follow.source_scene, follow.scene)] = {"run": source, "follow": follow._asdict(), "arrival": pose.pos}
                        pending.remove((source, follow))

    def _pose_clips(self, run, entries):
        open_marks = {}
        for entry in entries:
            if entry.note is not None and entry.note.startswith(f"mark {CLIP_MARK}:"):
                _, character, name, edge = entry.note[len("mark "):].split(":")
                key = (character, name)
                if edge == MARK_START:
                    open_marks[key] = entry.t
                elif edge == MARK_END and key in open_marks:
                    start = open_marks.pop(key)
                    frames = [e for e in entries if e.type in CLIP_TYPES and e.dir == IN and start <= e.t <= entry.t]
                    if frames:
                        self.pose_clips[key] = {"run": run, "start": start, "frames": [_frame_item(e, start) for e in frames]}

    def _enemy_clips(self, run, frames):
        load = {}
        clips = {}
        for entry in frames:
            if entry.dir != IN:
                continue
            if entry.type == Msg.POSE_STATE and len(entry.payload) == wire.PoseState.size:
                pose = wire.PoseState.unpack(entry.payload)
                load = {"scene": pose.room, "load": pose.load_serial}
            elif entry.type in (Msg.ENEMY_POSE, Msg.ENEMY_MOTION) and len(entry.payload) >= wire.EnemyHeader.size:
                header = wire.EnemyHeader.unpack(entry.payload[:wire.EnemyHeader.size])
                key = (header.scene, load.get("load", 0) if load.get("scene") == header.scene else 0)
                clips.setdefault(key, []).append(entry)
        for key, items in clips.items():
            start = items[0].t
            self.enemy_clips[key] = {"run": run, "start": start, "frames": [_frame_item(e, start) for e in items]}

    def _single(self, run, entry):
        payload = entry.payload
        if entry.type == Msg.HIT_REQUEST and len(payload) == wire.HitRequest.size:
            hit = wire.HitRequest.unpack(payload)
            if hit.vtable in self.vtables:
                self.hit_requests[(self.vtables.index(hit.vtable), hit.attack_type)] = {"run": run, "payload": payload}
        elif entry.type == Msg.ENEMY_LEASE and len(payload) == wire.EnemyLease.size:
            lease = wire.EnemyLease.unpack(payload)
            self.lease_grants[lease.class_index] = {"run": run, "payload": payload}
        elif entry.type == Msg.FLOOR_PUT and len(payload) == wire.FloorPut.size:
            put = wire.FloorPut.unpack(payload)
            self._floor(run, put.room, put.item_id, put.count, put.pos)
        elif entry.type == Msg.FLOOR_TAKE and len(payload) == wire.FloorTake.size:
            take = wire.FloorTake.unpack(payload)
            self._floor(run, take.room, take.item_id, 1, take.pos)
        elif entry.type == Msg.TAKE_REQUEST and len(payload) == wire.TakeRequest.size:
            take = wire.TakeRequest.unpack(payload)
            self._floor(run, take.scene, take.item_id, take.count, take.pos)
        elif entry.type == Msg.PROP_MOVE and len(payload) == wire.PropMove.size:
            move = wire.PropMove.unpack(payload)
            self.boxes[(move.scene, move.unit)] = {"run": run, "pos": move.pos, "yaw": move.yaw}
        elif entry.type == Msg.PROP_COMMIT and len(payload) == wire.PropCommit.size:
            commit = wire.PropCommit.unpack(payload)
            self.boxes[(commit.scene, commit.unit)] = {"run": run, "pos": commit.pos, "yaw": commit.yaw}
        elif entry.type == Msg.FLOOR_SNAPSHOT:
            for item in _floor_snapshot_items(payload):
                self._floor(run, item.room, item.item_id, item.count, item.pos)

    def _floor(self, run, scene, item_id, count, pos):
        item = {"run": run, "scene": scene, "item_id": item_id, "count": count, "pos": tuple(pos)}
        self.floor_items = [i for i in self.floor_items if (i["scene"], i["item_id"], i["pos"]) != (scene, item_id, item["pos"])]
        self.floor_items.append(item)

    # lookups: None when the library holds nothing for the key

    def door(self, source_scene, scene=None):
        for (source, target), item in self.doors.items():
            if source == source_scene and (scene is None or target == scene):
                return item
        return None

    def pose_clip(self, character, name=None):
        matches = [item for (who, clip), item in self.pose_clips.items() if who == character and (name is None or clip == name)]
        return max(matches, key=lambda item: item["start"], default=None)

    def enemy_clip(self, scene):
        matches = [item for (clip_scene, _), item in self.enemy_clips.items() if clip_scene == scene]
        return max(matches, key=lambda item: item["start"], default=None)

    def hit_request(self, class_index):
        matches = [item for (cls, _), item in self.hit_requests.items() if cls == class_index]
        return matches[0] if matches else None

    def lease_grant(self, class_index):
        return self.lease_grants.get(class_index)

    def items_in(self, scene):
        return [item for item in self.floor_items if item["scene"] == scene]

    def box(self, scene, unit):
        return self.boxes.get((scene, unit))

    # persistence

    def as_json(self):
        def clip(item):
            return {"run": item["run"], "start": item["start"], "frames": [dict(f, payload=f["payload"].hex()) for f in item["frames"]]}
        return {
            "doors": [{"key": list(key), **{k: v for k, v in item.items()}} for key, item in self.doors.items()],
            "pose_clips": [{"key": list(key), **clip(item)} for key, item in self.pose_clips.items()],
            "enemy_clips": [{"key": list(key), **clip(item)} for key, item in self.enemy_clips.items()],
            "hit_requests": [{"key": list(key), "run": i["run"], "payload": i["payload"].hex()} for key, i in self.hit_requests.items()],
            "lease_grants": [{"key": key, "run": i["run"], "payload": i["payload"].hex()} for key, i in self.lease_grants.items()],
            "floor_items": self.floor_items,
            "boxes": [{"key": list(key), **item} for key, item in self.boxes.items()],
        }

    def has(self, kind):
        return bool({"door": self.doors, "pose clip": self.pose_clips, "enemy clip": self.enemy_clips,
                     "hit request": self.hit_requests, "lease grant": self.lease_grants, "floor item": self.floor_items,
                     "box": self.boxes}[kind])

    def summary(self):
        return (f"doors {len(self.doors)}, pose clips {len(self.pose_clips)}, enemy clips {len(self.enemy_clips)}, "
                f"hit requests {len(self.hit_requests)}, lease grants {len(self.lease_grants)}, "
                f"floor items {len(self.floor_items)}, boxes {len(self.boxes)}")


# The scenario whose natural run fills each kind.
FILLED_BY = {"door": "t9_rooms", "pose clip": "record", "enemy clip": "t5_observe", "hit request": "t5_observe",
             "lease grant": "t8_contact", "floor item": "t12_items", "box": "t10_props"}
CLIP_TYPES = (Msg.POSE_STATE, Msg.MOTION_EVENT, Msg.WEAPON_FX)


def _frame_item(entry, start):
    return {"dt": entry.t - start, "type": entry.type, "payload": entry.payload}


def _floor_snapshot_items(payload):
    try:
        return wire.unpack_floor_snapshot(payload)[1]
    except wire.WireError:
        return []


def build(output_root):
    """The library of every run folder under `output_root` (oldest first), written to library.json there."""
    root = Path(output_root)
    library = Library()
    for frames_file in sorted(root.glob(f"*/{FRAMES_FILE}")):
        library.ingest(frames_file.parent.name, read_journal(frames_file))
    root.mkdir(parents=True, exist_ok=True)
    (root / LIBRARY_FILE).write_text(json.dumps(library.as_json(), indent=1), encoding="utf-8")
    return library
