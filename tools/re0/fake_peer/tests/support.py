"""Test doubles: a Context whose link records what the fake sends instead of using a socket."""
import tempfile
import threading
import time
from pathlib import Path

from fake_peer import wire
from fake_peer.link import SessionInfo
from fake_peer.runner import Context, Options
from fake_peer.wire import Frame

HOST_SLOT, GUEST_SLOT = 0, 1


class RecordingLink:
    """Stands in for a Link: remembers every frame sent, offers the same surface the modules use."""

    def __init__(self, fake_slot):
        self.sent = []
        self.session = SessionInfo()
        self.session.fake_slot = fake_slot
        self.session.host_slot = HOST_SLOT
        self.session.game_slot = 1 - fake_slot
        self.session.welcomed.set()
        self.session.peer_up.set()
        self.connected = threading.Event()
        self.connected.set()
        self.guard = None

    def send(self, msg_type, payload=b"", flags=wire.FLAG_RELIABLE, slot=None):
        if self.guard and not self.guard(int(msg_type)):
            return False
        self.sent.append(Frame(int(msg_type), flags, wire.SLOT_ALL if slot is None else slot, bytes(payload)))
        return True

    def of(self, msg_type):
        return [f for f in self.sent if f.type == msg_type]

    def stop_heartbeats(self, seconds):
        pass

    def close(self):
        pass


def make_context(seat="guest", library=None, status=False):
    """A Context for `seat` on a recording link; its modules are the real ones. `status` turns the periodic status senders on."""
    folder = tempfile.mkdtemp(prefix="fake_peer_test_")
    options = Options(seat=seat, transport="bridge", bridge_port=1, no_game=True, out=folder, status=status)
    ctx = Context(options)
    ctx.link = RecordingLink(GUEST_SLOT if seat == "guest" else HOST_SLOT)
    ctx.link.guard = ctx._allow_send
    ctx.authority.mode = "first"  # no reconcile behind a test's back; tests call it themselves
    if library is not None:
        ctx.library = library
    return ctx


def deliver(ctx, msg_type, payload, slot=None, flags=wire.FLAG_RELIABLE):
    """The game side sends a frame: it goes through the dispatcher as a received one."""
    if slot is None:
        slot = ctx.link.session.game_slot
    ctx.journal.frame("in", Frame(int(msg_type), flags, slot, payload))
    ctx.dispatcher.dispatch(Frame(int(msg_type), flags, slot, payload))


def tick(ctx, seconds=0.0):
    """Runs every module's periodic step once, `seconds` after now."""
    now = time.monotonic() + seconds
    for module in ctx.modules:
        module.tick(now)


def pose_from_game(ctx, scene, pos=(0.0, 0.0, 0.0), load_serial=1, **fields):
    deliver(ctx, wire.Msg.POSE_STATE, wire.PoseState.pack(
        room=scene, load_serial=load_serial, pos=pos, quat=wire.IDENTITY_PACKED_QUAT, motion0=0x20, motion1=wire.NO_MOTION, **fields),
        flags=0)


def scratch_path():
    return Path(tempfile.mkdtemp(prefix="fake_peer_test_"))
