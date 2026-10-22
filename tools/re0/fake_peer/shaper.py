"""The fake's link shaper: the seeded impairment model (impair.py) on both directions, plus `cut` and `stall` windows.

`out` shapes the frames the fake sends, `inbound` the frames the game sends. Reliable frames only get delay and stall and
stay in order; unreliable frames get the full model (loss, bursts, reorder, duplicates, bandwidth). A cut drops the
unreliable frames of its direction and holds the reliable ones until it ends; a stall holds everything until it ends.
"""
import heapq
import threading
import time

from fake_peer.impair import MS_PER_S, Impairment, make_params, with_changes

OUT = "out"
IN = "in"
BOTH = "both"
DIRECTIONS = (OUT, IN, BOTH)


class Direction:
    """One direction of the link: the impairment, the windows and the reliable order."""

    def __init__(self, params, seed, label, clock):
        self._clock = clock
        self._impairment = Impairment(params, seed, label)
        self._cut_until = 0.0
        self._stall_until = 0.0
        self._last_reliable_due = 0.0

    @property
    def params(self):
        return self._impairment.params

    @params.setter
    def params(self, params):
        self._impairment.params = params

    def cut(self, seconds):
        self._cut_until = self._clock() + seconds

    def stall(self, seconds):
        self._stall_until = self._clock() + seconds

    def idle(self):
        now = self._clock()
        return self.params.idle() and now >= self._cut_until and now >= self._stall_until

    def plan(self, reliable, size):
        """The monotonic times to deliver a frame of `size` bytes: none = dropped, two = duplicated."""
        now = self._clock()
        if now < self._stall_until:
            return [self._ordered(reliable, self._stall_until)]
        if now < self._cut_until:
            return [self._ordered(True, self._cut_until)] if reliable else []
        if reliable:
            return [self._ordered(True, now + self._impairment.delay_ms() / MS_PER_S)]
        verdict = self._impairment.decide(now * MS_PER_S, size)
        return [now + delay / MS_PER_S for delay in verdict.delays_ms]

    def _ordered(self, reliable, due):
        if reliable:
            due = max(due, self._last_reliable_due)
            self._last_reliable_due = due
        return due


class Shaper:
    """Both directions of the link under one seed. The default model is idle (a pass-through)."""

    def __init__(self, params=None, seed=0, clock=time.monotonic):
        self.seed = seed
        self.out = Direction(params or make_params(), seed, OUT, clock)
        self.inbound = Direction(params or make_params(), seed, IN, clock)

    def _directions(self, direction):
        if direction not in DIRECTIONS:
            raise ValueError(f"direction is one of {', '.join(DIRECTIONS)}, not {direction!r}")
        return {OUT: (self.out,), IN: (self.inbound,), BOTH: (self.out, self.inbound)}[direction]

    def cut(self, seconds, direction=OUT):
        """Unreliable frames dropped, reliable ones held in order, until the window ends."""
        for side in self._directions(direction):
            side.cut(seconds)

    def stall(self, seconds, direction=OUT):
        """Everything held until the window ends, then delivered at once."""
        for side in self._directions(direction):
            side.stall(seconds)

    def configure(self, direction=OUT, **changes):
        """Changes model settings (latency_ms, jitter_ms, loss, burst_*, reorder, duplicate, bandwidth_bps, buffer_ms)."""
        for side in self._directions(direction):
            side.params = with_changes(side.params, changes)

    def idle(self):
        return self.out.idle() and self.inbound.idle()

    def describe(self):
        return f"seed {self.seed} out {self.out.params} in {self.inbound.params}"


class TimedQueue:
    """Items with a due time, released in due order (then arrival order); safe across threads."""

    def __init__(self):
        self._heap = []
        self._order = 0
        self._lock = threading.Lock()

    def __bool__(self):
        with self._lock:
            return bool(self._heap)

    def push(self, due, item):
        with self._lock:
            self._order += 1
            heapq.heappush(self._heap, (due, self._order, item))

    def pop_due(self, now):
        due_items = []
        with self._lock:
            while self._heap and self._heap[0][0] <= now:
                due_items.append(heapq.heappop(self._heap)[2])
        return due_items
