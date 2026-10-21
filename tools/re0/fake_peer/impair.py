"""Seeded link impairment model: pure, virtual time, no sockets.

Ported from coop-anything's tools/chaos (impair.py and profiles.json), trimmed to what the fake peer needs.
Copyright (c) 2026 cranebeaker and contributors, MIT License.

One `Impairment` models one direction of one link. It answers each frame with a `Verdict`: dropped (with a reason) or the
list of delivery delays in ms (two entries = a duplicate). Loss is a Gilbert-Elliott two-state model (a good state with
the base loss, a bad state that a burst enters and leaves per frame); reorder lets a frame skip the latency so it overtakes
earlier ones; bandwidth is a serialization delay with a tail-drop queue. Reliable frames only get the delay (`delay_ms`).
"""
import json
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path

PROFILES_PATH = Path(__file__).with_name("profiles.json")
MASK_64 = (1 << 64) - 1
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
SPLITMIX_STEP = 0x9E3779B97F4A7C15
SPLITMIX_MUL_1 = 0xBF58476D1CE4E5B9
SPLITMIX_MUL_2 = 0x94D049BB133111EB
FLOAT_BITS = 53
MS_PER_S = 1000.0
BITS_PER_BYTE = 8


def fnv1a64(text):
    h = FNV_OFFSET
    for byte in text.encode("utf-8"):
        h = ((h ^ byte) * FNV_PRIME) & MASK_64
    return h


class Rng:
    """SplitMix64 seeded by `seed:label`, so each direction of a run has its own stream and the same seed repeats a run."""

    def __init__(self, seed, label):
        self._state = fnv1a64(f"{seed}:{label}")

    def u64(self):
        self._state = (self._state + SPLITMIX_STEP) & MASK_64
        z = self._state
        z = ((z ^ (z >> 30)) * SPLITMIX_MUL_1) & MASK_64
        z = ((z ^ (z >> 27)) * SPLITMIX_MUL_2) & MASK_64
        return z ^ (z >> 31)

    def random(self):
        return (self.u64() >> (64 - FLOAT_BITS)) / float(1 << FLOAT_BITS)

    def uniform(self, low, high):
        return low + (high - low) * self.random()


@dataclass
class Params:
    latency_ms: float = 0.0
    jitter_ms: float = 0.0       # the delay is latency +/- jitter, uniform, never below 0
    loss: float = 0.0            # loss in the good state
    burst_enter: float = 0.0     # per-frame chance good -> bad
    burst_exit: float = 0.5      # per-frame chance bad -> good
    burst_loss: float = 1.0      # loss in the bad state
    reorder: float = 0.0         # chance a frame skips the latency
    duplicate: float = 0.0
    bandwidth_bps: int = 0       # 0 = unlimited
    buffer_ms: float = 200.0     # queue depth at the bandwidth cap before a tail drop

    def idle(self):
        return self == Params()

    def expected_loss(self):
        """Long-run loss fraction (bandwidth drops not counted)."""
        total = self.burst_enter + self.burst_exit
        bad = self.burst_enter / total if total > 0 else 0.0
        return (1 - bad) * self.loss + bad * self.burst_loss


@dataclass
class Verdict:
    delays_ms: list = field(default_factory=list)  # empty = dropped
    reason: str = ""                               # why it was dropped: loss, burst or bandwidth

    @property
    def dropped(self):
        return not self.delays_ms


PARAM_NAMES = tuple(f.name for f in fields(Params))


def load_profiles(path=None):
    raw = json.loads(Path(path or PROFILES_PATH).read_text(encoding="utf-8"))
    return {name: Params(**values) for name, values in raw.items() if not name.startswith("_")}


def make_params(profile=None, **overrides):
    """The named profile (or the idle model) with `overrides` applied; an unknown name or key is an error."""
    params = Params()
    if profile:
        profiles = load_profiles()
        if profile not in profiles:
            raise KeyError(f"unknown link profile {profile!r}; have {', '.join(profiles)}")
        params = profiles[profile]
    return with_changes(params, overrides)


def with_changes(params, changes):
    values = asdict(params)
    for name, value in changes.items():
        if name not in PARAM_NAMES:
            raise KeyError(f"unknown link setting {name!r}; have {', '.join(PARAM_NAMES)}")
        values[name] = value
    return Params(**values)


class Impairment:
    def __init__(self, params, seed, label):
        self.params = params
        self._rng = Rng(seed, label)
        self._bad = False
        self._free_at_ms = 0.0

    def delay_ms(self):
        """The delay a reliable frame gets: latency and jitter only."""
        p = self.params
        return max(0.0, p.latency_ms + self._rng.uniform(-p.jitter_ms, p.jitter_ms))

    def decide(self, now_ms, size):
        p, rng = self.params, self._rng
        if self._bad:
            if rng.random() < p.burst_exit:
                self._bad = False
        elif rng.random() < p.burst_enter:
            self._bad = True
        if rng.random() < (p.burst_loss if self._bad else p.loss):
            return Verdict(reason="burst" if self._bad else "loss")
        queue_ms = 0.0
        if p.bandwidth_bps > 0:
            start = max(now_ms, self._free_at_ms)
            if start - now_ms > p.buffer_ms:
                return Verdict(reason="bandwidth")
            self._free_at_ms = start + size * BITS_PER_BYTE * MS_PER_S / p.bandwidth_bps
            queue_ms = self._free_at_ms - now_ms
        delays = [self._one_delay() + queue_ms]
        if p.duplicate > 0 and rng.random() < p.duplicate:
            delays.append(self._one_delay() + queue_ms)
        return Verdict(delays_ms=delays)

    def _one_delay(self):
        if self._rng.random() < self.params.reorder:
            return 0.0
        return self.delay_ms()
