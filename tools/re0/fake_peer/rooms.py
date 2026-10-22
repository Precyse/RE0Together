"""Rooms: the fake character's ROOM_MOVE / ROOM_STATE, DOOR_FOLLOW answers and runs, door races and the self-heal probe.

Scenes, entries and door arguments are never invented: a scene the fake moves to must be one the game itself named (its
poses, ROOM_MOVE, ROOM_STATE, DOOR_FOLLOW) or the library holds.
"""
import time

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

FOLLOWER_SPOT = 2  # spot_rule mode of a character placed behind its partner
OPEN_DOOR_TIMEOUT_S = 10.0
ANSWERS = ("commit", "busy", "other room", "none")
LEAVE_BEHIND = 1


class UnknownScene(ValueError):
    """The scene was never named by the game, so moving there could crash the engine."""


class Rooms(Module):
    HANDLES = {Msg.DOOR_FOLLOW: "on_door_follow", Msg.DOOR_FOLLOW_RESULT: "on_result", Msg.ROOM_MOVE: "on_game_move",
               Msg.POSE_STATE: "on_pose", Msg.ROOM_STATE: "on_room_state"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.answer = "commit"
        self.door_ms = None  # None: follow a committed door when the game's own ROOM_MOVE arrives; else this long after the answer
        self.race = False
        self.known = set()
        self.arrival = {}  # scene -> where the game's character first stood in it
        self.answered = []  # (serial, answer) the fake gave
        self.results = {}  # serial -> result code of the fake's own DOOR_FOLLOW
        self.game_doors = []  # DoorFollow records the game sent
        self._following = None  # {"scene", "entry", "at"}: a committed follow waiting for the game's ROOM_MOVE or its time
        self._running = None  # {"serial", "scene", "entry"}: the fake's own door waiting for the game's answer
        self._awaiting_arrival = set()
        self._sent_at = 0.0

    # ---- what the game named -------------------------------------------------------------------------------------

    def _learn(self, *scenes):
        self.known.update(s for s in scenes if s is not None and s < wire.SCENE_LIMIT)

    def on_pose(self, frame):
        pose = wire.PoseState.unpack(frame.payload)
        self._learn(pose.room)
        if pose.room in self._awaiting_arrival:
            self._awaiting_arrival.discard(pose.room)
            self.arrival[pose.room] = tuple(pose.pos)

    def on_room_state(self, frame):
        state = wire.RoomState.unpack(frame.payload)
        self._learn(state.scene, state.door_target if state.door_target != wire.NO_SCENE else None)

    def on_game_move(self, frame):
        move = wire.RoomMove.unpack(frame.payload)
        self._learn(move.scene)
        self._awaiting_arrival.add(move.scene)
        if self._following and self._following["at"] is None and self._following["scene"] == move.scene:
            follow, self._following = self._following, None
            self.move(follow["scene"], follow["entry"], FOLLOWER_SPOT)
        if self._running and self._running["scene"] == move.scene:
            run, self._running = self._running, None
            self.move(run["scene"], run["entry"], wire.AUTO_SPOT)

    def library_scenes(self):
        library = self.ctx.library
        return {scene for pair in library.doors for scene in pair} | {scene for scene, _ in library.enemy_clips}

    def is_known(self, scene):
        return scene in self.known or scene in self.library_scenes()

    # ---- the fake's own moves --------------------------------------------------------------------------------------

    def move(self, scene, entry=0, mode=wire.AUTO_SPOT):
        """The fake's character changes room: new load serial, ROOM_MOVE, ROOM_STATE; poses follow in the new room."""
        if not self.is_known(scene):
            raise UnknownScene(f"scene 0x{scene:02x} was never named by the game")
        peer = self.ctx.peer
        if peer.place.scene == scene:
            return False
        peer.place_at(scene, entry, mode)
        peer.pos = self.arrival.get(scene, peer.pos)
        self.ctx.authority.entered(scene)
        self.ctx.link.send(Msg.ROOM_MOVE, wire.RoomMove.pack(
            seq=peer.next("room_move"), scene=scene, entry=entry, load_serial=peer.place.load_serial,
            character=peer.character, mode=mode))
        return True

    def probe_self_heal(self, scene):
        """The poses name another scene with no ROOM_MOVE (the game heals it after 1 s)."""
        if not self.is_known(scene):
            raise UnknownScene(f"scene 0x{scene:02x} was never named by the game")
        peer = self.ctx.peer
        peer.place.scene = scene
        peer.place.load_serial = peer.next_u16("load_serial")

    # ---- the game's doors --------------------------------------------------------------------------------------------

    def on_door_follow(self, frame):
        follow = wire.DoorFollow.unpack(frame.payload)
        self._learn(follow.source_scene, follow.scene)
        self.game_doors.append(follow)
        if self.race:
            self._send_own_door(follow.source_scene, follow.scene, follow.entry, follow)
        result = self._result_for(follow)
        if result is None:
            self.ctx.journal.note(f"door follow {follow.serial} not answered")
            return
        peer = self.ctx.peer
        self.ctx.link.send(Msg.DOOR_FOLLOW_RESULT, wire.DoorFollowResult.pack(
            serial=follow.serial, result=result, character=peer.character))
        self.answered.append((follow.serial, wire.DOOR_RESULTS[result]))
        if result == wire.RESULT_COMMIT and self.ctx.sources.mode != "echo":  # the echo moves the fake a second later
            at = None if self.door_ms is None else time.monotonic() + self.door_ms / 1000.0
            self._following = {"scene": follow.scene, "entry": follow.entry, "at": at}

    def _result_for(self, follow):
        if self.race:
            return wire.RESULT_BUSY
        if self.answer == "none":
            return None
        if self.answer == "busy":
            return wire.RESULT_BUSY
        in_room = self.ctx.peer.place.scene == follow.source_scene
        if self.answer == "other room" or not in_room:
            return wire.RESULT_OTHER_ROOM
        return wire.RESULT_COMMIT

    # ---- the fake's own doors ------------------------------------------------------------------------------------------

    def run_door(self, source_scene, scene=None):
        """Runs a door the library holds for `source_scene`: DOOR_FOLLOW now, the move when the game's answer allows it."""
        item = self.ctx.library.door(source_scene, scene)
        if item is None:
            return False
        follow = item["follow"]
        if self.ctx.remote.party_mode == LEAVE_BEHIND or self.ctx.peer.party_mode == LEAVE_BEHIND:
            self.move(follow["scene"], follow["entry"])
            return True
        self._send_own_door(follow["source_scene"], follow["scene"], follow["entry"], follow)
        return True

    def _send_own_door(self, source_scene, scene, entry, template):
        fields = template._asdict() if hasattr(template, "_asdict") else dict(template)
        serial = self.ctx.peer.next("door_follow")
        fields.update(serial=serial, source_scene=source_scene, scene=scene, entry=entry, character=self.ctx.peer.character,
                      reason=wire.REASON_TEAM, reserved=0)
        self.ctx.link.send(Msg.DOOR_FOLLOW, wire.DoorFollow.pack(**fields))
        self._running = {"serial": serial, "scene": scene, "entry": entry}
        self._sent_at = time.monotonic()
        return serial

    def on_result(self, frame):
        result = wire.DoorFollowResult.unpack(frame.payload)
        self.results[result.serial] = wire.DOOR_RESULTS[result.result]
        run = self._running
        if run and run["serial"] == result.serial and result.result != wire.RESULT_COMMIT:
            self._running = None
            self.move(run["scene"], run["entry"])

    def tick(self, now):
        follow = self._following
        if follow and follow["at"] is not None and now >= follow["at"]:
            self._following = None
            self.move(follow["scene"], follow["entry"], FOLLOWER_SPOT)
        run = self._running
        if run and time.monotonic() - self._sent_at > OPEN_DOOR_TIMEOUT_S:
            self._running = None
            self.ctx.journal.note(f"own door {run['serial']} timed out")
