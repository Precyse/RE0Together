"""Seeded network-fault model for the echo hub (docs/SR2_NOTES.md "Echo testing"): pure, virtual-time, no sockets. One
Impairment models one direction of one link: feed it each datagram with the time (ms) and size (bytes) and it answers with a
Verdict, dropped (with the reason) or the delivery delays (two or more are a duplicate). The shapes are the named profiles in
echo_profiles.json. Loss is a Gilbert-Elliott two-state model (a good and a bad state, so losses come in bursts), delay is
latency plus uniform jitter, a reordered datagram skips the latency and overtakes earlier ones, a bandwidth cap adds queueing
delay and tail-drops once the queue is deeper than buffer_ms.

Ported from coop-anything tools/chaos/impair.py and profiles.json (the SplitMix64 stream, the Gilbert-Elliott loss, the
bandwidth queue and the profile names), leaving out its bit corruption and its test mutants.
Copyright (c) 2026 cranebeaker and contributors, MIT license."""
import json
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path

PROFILES_PATH = Path(__file__).with_name("echo_profiles.json")
PERFECT_PROFILE = "none"

MASK_64 = (1 << 64) - 1
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
GOLDEN_GAMMA = 0x9E3779B97F4A7C15
MIX_1 = 0xBF58476D1CE4E5B9
MIX_2 = 0x94D049BB133111EB
MANTISSA_SHIFT = 11
MANTISSA_SCALE = 1.0 / (1 << 53)
BITS_PER_BYTE = 8
MS_PER_S = 1000.0

REASON_LOSS = "loss"
REASON_BURST = "burst"
REASON_BANDWIDTH = "bandwidth"
REASON_CUT = "cut"


def fnv1a64(text):
    value = FNV_OFFSET
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * FNV_PRIME) & MASK_64
    return value


class Rng:
    """SplitMix64 seeded from the seed and a label, so every direction of every link has its own stream."""

    def __init__(self, seed, label):
        self.state = fnv1a64(f"{seed}:{label}")

    def next_u64(self):
        self.state = (self.state + GOLDEN_GAMMA) & MASK_64
        z = self.state
        z = ((z ^ (z >> 30)) * MIX_1) & MASK_64
        z = ((z ^ (z >> 27)) * MIX_2) & MASK_64
        return z ^ (z >> 31)

    def random(self):
        return (self.next_u64() >> MANTISSA_SHIFT) * MANTISSA_SCALE

    def uniform(self, low, high):
        return low + (high - low) * self.random()


@dataclass(frozen=True)
class Params:
    latency_ms: float = 0.0
    jitter_ms: float = 0.0          # the delay is latency +/- jitter, uniform, never below 0
    loss: float = 0.0               # loss chance in the good state
    burst_enter: float = 0.0        # per datagram, good -> bad
    burst_exit: float = 0.5         # per datagram, bad -> good
    burst_loss: float = 1.0         # loss chance in the bad state
    reorder: float = 0.0            # chance a datagram skips the latency and overtakes earlier ones
    duplicate: float = 0.0
    bandwidth_bps: int = 0          # 0 = unlimited
    buffer_ms: float = 200.0        # queue depth at the cap before a tail drop

    def expected_loss(self):
        """The long-run loss fraction of the two-state model (bandwidth drops not counted)."""
        total = self.burst_enter + self.burst_exit
        share_bad = self.burst_enter / total if total > 0 else 0.0
        return (1 - share_bad) * self.loss + share_bad * self.burst_loss

    def is_perfect(self):
        return self == Params()


@dataclass(frozen=True)
class Verdict:
    delays_ms: tuple = field(default_factory=tuple)   # empty = dropped
    reason: str = ""                                  # set when dropped
    reordered: bool = False                           # the first copy skipped the latency

    @property
    def dropped(self):
        return not self.delays_ms


def load_profiles(path=PROFILES_PATH):
    """Every named profile of the file (keys starting with an underscore are notes)."""
    known = {f.name for f in fields(Params)}
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    profiles = {}
    for name, values in raw.items():
        if name.startswith("_"):
            continue
        unknown = set(values) - known
        if unknown:
            raise KeyError(f"profile {name!r} has unknown keys {sorted(unknown)}")
        profiles[name] = Params(**values)
    return profiles


def profile_params(name, profiles=None):
    profiles = profiles or load_profiles()
    if name not in profiles:
        raise KeyError(f"unknown profile {name!r}; have {sorted(profiles)}")
    return profiles[name]


def with_overrides(params, **overrides):
    return Params(**{**asdict(params), **overrides})


class Impairment:
    """One direction of one link."""

    def __init__(self, params, seed, label):
        self.params = params
        self.rng = Rng(seed, label)
        self.bad = False
        self.free_at_ms = 0.0

    def set_params(self, params):
        self.params = params

    def decide(self, now_ms, size):
        p, rng = self.params, self.rng
        self.advance_burst_state()
        if rng.random() < (p.burst_loss if self.bad else p.loss):
            return Verdict(reason=REASON_BURST if self.bad else REASON_LOSS)
        queue_ms = 0.0
        if p.bandwidth_bps > 0:
            start = max(now_ms, self.free_at_ms)
            if start - now_ms > p.buffer_ms:
                return Verdict(reason=REASON_BANDWIDTH)
            self.free_at_ms = start + size * BITS_PER_BYTE * MS_PER_S / p.bandwidth_bps
            queue_ms = self.free_at_ms - now_ms
        first, reordered = self.one_delay(queue_ms)
        delays = [first]
        if p.duplicate > 0 and rng.random() < p.duplicate:
            delays.append(self.one_delay(queue_ms)[0])
        return Verdict(tuple(delays), reordered=reordered)

    def advance_burst_state(self):
        p, rng = self.params, self.rng
        if self.bad:
            if rng.random() < p.burst_exit:
                self.bad = False
        elif rng.random() < p.burst_enter:
            self.bad = True

    def one_delay(self, queue_ms):
        """(delay in ms, whether this copy skipped the latency)."""
        p, rng = self.params, self.rng
        if rng.random() < p.reorder:
            return queue_ms, True
        return max(0.0, p.latency_ms + rng.uniform(-p.jitter_ms, p.jitter_ms)) + queue_ms, False
