"""The link layer of the echo hub (docs/SR2_NOTES.md "Echo testing"): one seeded Impairment (echo_impair.py) per direction of
every pair of instances, one-way cuts, and the counters that let a run prove its profile was active. The hub asks `decide`
for every datagram copy it relays; nothing here touches a socket or a clock."""
from collections import Counter
from dataclasses import dataclass

import echo_impair as impair

IMPAIRED_REASONS = (impair.REASON_LOSS, impair.REASON_BURST, impair.REASON_BANDWIDTH)
PERCENT = 100.0


@dataclass(frozen=True)
class LinkReport:
    """What the link did to the datagrams offered to it since the last mark."""
    profile: str
    perfect: bool       # the profile does nothing to a datagram
    seed: int
    expected_loss: float
    offered: int
    lost: int           # dropped by the profile: loss, burst, bandwidth
    cut: int            # dropped by a one-way cut, not part of the observed loss
    duplicated: int
    reordered: int

    @property
    def impaired(self):
        return self.offered - self.cut

    @property
    def observed_loss(self):
        return self.lost / self.impaired if self.impaired else 0.0

    def describe(self):
        return (f"link {self.profile} seed {self.seed}: {self.offered} offered, {self.lost} lost "
                f"({self.observed_loss * PERCENT:.1f}%, the profile expects {self.expected_loss * PERCENT:.1f}%), "
                f"{self.cut} cut, {self.duplicated} duplicated, {self.reordered} reordered")


class LinkModel:
    """The faults between the instances: `decide(sender, receiver, now_ms, size)` is the Verdict for one datagram copy."""

    def __init__(self, profile, params, seed):
        self.profile = profile
        self.params = params
        self.seed = seed
        self.directions = {}
        self.cuts = set()
        self.counts = Counter()

    @classmethod
    def named(cls, profile, seed, profiles=None, **overrides):
        """A model for a named profile; `overrides` replace single fields of it (latency_ms=40)."""
        params = impair.with_overrides(impair.profile_params(profile, profiles), **overrides)
        return cls(profile if not overrides else f"{profile}+{'+'.join(sorted(overrides))}", params, seed)

    def decide(self, sender, receiver, now_ms, size):
        self.counts["offered"] += 1
        if (sender, receiver) in self.cuts:
            self.counts[impair.REASON_CUT] += 1
            return impair.Verdict(reason=impair.REASON_CUT)
        verdict = self.direction(sender, receiver).decide(now_ms, size)
        if verdict.dropped:
            self.counts[verdict.reason] += 1
        else:
            self.counts["duplicated"] += len(verdict.delays_ms) - 1
            self.counts["reordered"] += verdict.reordered
        return verdict

    def direction(self, sender, receiver):
        key = (sender, receiver)
        if key not in self.directions:
            self.directions[key] = impair.Impairment(self.params, self.seed, f"{sender}>{receiver}")
        return self.directions[key]

    def switch(self, profile, params):
        """A new profile for every direction from now on; the random streams and the queues carry on."""
        self.profile, self.params = profile, params
        for direction in self.directions.values():
            direction.set_params(params)

    def cut(self, sender, receiver):
        self.cuts.add((sender, receiver))

    def uncut(self, sender, receiver):
        self.cuts.discard((sender, receiver))

    def clear_counters(self):
        self.counts.clear()

    def report(self):
        return LinkReport(self.profile, self.params.is_perfect(), self.seed, self.params.expected_loss(), self.counts["offered"],
                          sum(self.counts[reason] for reason in IMPAIRED_REASONS), self.counts[impair.REASON_CUT],
                          self.counts["duplicated"], self.counts["reordered"])
