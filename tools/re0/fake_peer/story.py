"""Death, save gate and cutscene skip: PLAYER_DIED, a door the fake is in, a busy think state, DEMO_SKIP_REQUEST / DEMO_SKIP.
EVENT_START / EVENT_STEPS from the game are recorded and counted, never answered (the fake runs no scripts)."""
from fake_peer import wire
from fake_peer.link import Module
from fake_peer.peer import THINK_BUSY_GROUP
from fake_peer.wire import Msg

DOOR_SHARED = 1
FLAG_SERIAL_IS_RECEIVERS = 1


class Story(Module):
    HANDLES = {Msg.DEMO_SKIP: "on_skip", Msg.DEMO_SKIP_REQUEST: "on_skip", Msg.EVENT_START: "on_event", Msg.EVENT_STEPS: "on_event"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.skips = []   # (type name, DemoSkip record) the game sent
        self.events = {}  # type name -> count
        self._before_door = None

    def die(self):
        """The fake's character died (PLAYER_DIED)."""
        self.ctx.link.send(Msg.PLAYER_DIED, bytes((self.ctx.peer.character,)))

    def in_door(self, scene):
        """The fake is inside a running door to `scene` (the host's save must wait): ROOM_STATE door target, held pose."""
        if not self.ctx.rooms.is_known(scene):
            raise ValueError(f"scene 0x{scene:02x} was never named by the game")
        place = self.ctx.peer.place
        place.door_target, place.door_flags = scene, DOOR_SHARED
        self.ctx.peer.send_room_state()
        sources = self.ctx.sources
        self._before_door = (sources.mode, sources.opts)
        sources.use("held")

    def clear_door(self):
        place = self.ctx.peer.place
        place.door_target, place.door_flags = wire.NO_SCENE, 0
        self.ctx.peer.send_room_state()
        if self._before_door:
            self.ctx.sources.use(self._before_door[0], **self._before_door[1])
            self._before_door = None

    def busy_think(self, busy=True):
        """The fake's poses report think group 2 (a busy script thread), or stop reporting it."""
        self.ctx.peer.think_override = (THINK_BUSY_GROUP, 0, 0) if busy else None

    def request_skip(self, demo_id, serial):
        """A follower's press: DEMO_SKIP_REQUEST for a demo the game leads (serial is the receiver's)."""
        self.ctx.link.send(Msg.DEMO_SKIP_REQUEST, wire.DemoSkip.pack(demo_id=demo_id, serial=serial, flags=FLAG_SERIAL_IS_RECEIVERS))

    def send_skip(self, demo_id, serial):
        """The leader's skip."""
        self.ctx.link.send(Msg.DEMO_SKIP, wire.DemoSkip.pack(demo_id=demo_id, serial=serial))

    def on_skip(self, frame):
        self.skips.append((wire.type_name(frame.type), wire.DemoSkip.unpack(frame.payload)))

    def on_event(self, frame):
        name = wire.type_name(frame.type)
        self.events[name] = self.events.get(name, 0) + 1
