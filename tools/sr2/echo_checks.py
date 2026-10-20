"""The assertion layer of the echo rig (docs/SR2_NOTES.md "Echo testing"): a check is data, a list of expectations over each
instance's sr2_adapter.log lines (or status lines), read from the offset of the last mark only, so an old run never passes.
This module is the engine; the checks themselves are the table in echo_check_table.py. Every row names the adapter's
logger::write format string verbatim, and echo_checks_test.py proves it is still in adapters/sr2/src."""
import os
import re
import sys
import time
from dataclasses import dataclass
from typing import Callable, Optional

ONCE = "once"
LAST = "last"
NONE = "none"
EACH_REMOTE = "each_remote"
EACH_MEMBER = "each_member"

WHO_ALL = "all"
WHO_HOST = "host"
WHO_GUESTS = "guests"
WHO_TARGET = "target"
WHO_OTHERS = "others"

SOURCE_LOG = "log"
SOURCE_STATUS = "status"

HOST_INSTANCE = 1
DEFAULT_WITHIN_S = 5.0
POLL_S = 0.5
NEVER = sys.maxsize
TARGET_TOKEN = "@SLOT@"
SLOT_GROUP = "slot"
SAMPLE_STRING = "word"
SAMPLE_NUMBER = "7"
SAMPLE_HEX = "1A"
SPECIFIER = re.compile(r"%(?:\.\*|\d*)(?:ll|z|l)?([a-zA-Z])")


class LogTail:
    """The lines of one log file from an offset: the file's size at construction and at every mark. A file that shrank
    (a new run truncated it) is read from its start."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.mark()

    def size(self):
        try:
            return os.path.getsize(self.path)
        except OSError:
            return 0

    def mark(self):
        self.offset = self.size()

    def lines(self):
        if self.size() < self.offset:
            self.offset = 0
        try:
            with open(self.path, "rb") as source:
                source.seek(self.offset)
                data = source.read()
        except OSError:
            return []
        return data.decode("utf-8", errors="replace").splitlines()


@dataclass(frozen=True)
class Expect:
    """One expectation. `format` is the adapter's logger::write format string, `prefix` what the log line has before it,
    `fragments` other verbatim pieces of adapter source the row depends on, `samples` the values of its specifiers in order (None = a default)
    for the sample line and `sample` a whole sample line (status rows). `verify(ctx, instance, match, lines, slot)` returns a problem or None."""
    label: str
    who: str
    regex: str
    rule: str = ONCE
    source: str = SOURCE_LOG
    within_s: float = DEFAULT_WITHIN_S
    format: str = ""
    prefix: str = ""
    fragments: tuple = ()
    samples: tuple = ()
    sample: str = ""
    after: str = ""
    verify: Optional[Callable] = None


@dataclass(frozen=True)
class Check:
    name: str
    expects: tuple
    includes: tuple = ()
    hub_zero: tuple = ()
    needs_target: bool = False


@dataclass
class Result:
    passed: bool
    instance: Optional[int]
    label: str
    detail: str
    index: int = -1


def sample_line(expect):
    """A line the adapter would write for the row: its format with every specifier given a sample value."""
    if expect.sample:
        return expect.sample
    given = iter(expect.samples)

    def value(match):
        chosen = next(given, None)
        if chosen is not None:
            return str(chosen)
        kind = match.group(1)
        return SAMPLE_STRING if kind == "s" else SAMPLE_HEX if kind in "xX" else SAMPLE_NUMBER

    return expect.prefix + SPECIFIER.sub(value, expect.format)


def source_pieces(expect):
    """The verbatim pieces of adapter source the row depends on."""
    return ((expect.format,) if expect.format else ()) + tuple(expect.fragments)


def select(ctx, who, target):
    guests = [i for i in ctx.instances if i != HOST_INSTANCE]
    if who == WHO_HOST:
        return [HOST_INSTANCE]
    if who == WHO_GUESTS:
        return guests
    if who == WHO_TARGET:
        return [target]
    if who == WHO_OTHERS:
        return [i for i in guests if i != target]
    return list(ctx.instances)


def label_of(instance):
    return "host" if instance == HOST_INSTANCE else f"guest{instance}"


def remote_slots(ctx, instance):
    """The slots of every other member, as the hub has them."""
    return {slot for other, slot in ctx.slots().items() if other != instance}


def expected_slots(ctx, instance, rule):
    return set(ctx.slots().values()) if rule == EACH_MEMBER else remote_slots(ctx, instance)


def head_of(expect):
    """The line prefix of the row up to its last colon ("trace: kick:"), to quote the nearest line when none matches."""
    head = (expect.prefix + expect.format.split("%")[0]).strip()
    colon = head.rfind(":")
    return head[:colon + 1] if colon > 0 else head


def last_like(expect, lines):
    head = head_of(expect)
    if not head:
        return lines[-1] if lines else "no line at all"
    return next((line for line in reversed(lines) if head in line), f"no line with '{head}'")


def problem_of(ctx, expect, instance, match, lines, slot):
    return expect.verify(ctx, instance, match, lines, slot) if expect.verify else None


def judge_none(ctx, expect, instance, slot, lines, found):
    if found:
        return Result(False, instance, expect.label, f"unexpected: {lines[found[0][0]]}", found[0][0])
    return Result(True, instance, expect.label, "no such line")


def judge_each(ctx, expect, instance, slot, lines, found):
    wanted = expected_slots(ctx, instance, expect.rule)
    good = [(index, match) for index, match in found if not problem_of(ctx, expect, instance, match, lines, slot)]
    seen = {int(match.group(SLOT_GROUP)) for _index, match in good}
    if wanted <= seen:
        return Result(True, instance, expect.label, f"slots {sorted(seen & wanted)}", good[-1][0])
    return Result(False, instance, expect.label, f"slots {sorted(wanted - seen)} missing; last: {last_like(expect, lines)}")


def judge_last(ctx, expect, instance, slot, lines, found):
    if not found:
        return Result(False, instance, expect.label, last_like(expect, lines))
    index, match = found[-1]
    problem = problem_of(ctx, expect, instance, match, lines, slot)
    return Result(problem is None, instance, expect.label, lines[index] if problem is None else f"{problem}: {lines[index]}", index)


def judge_once(ctx, expect, instance, slot, lines, found):
    first_problem = None
    for index, match in found:
        problem = problem_of(ctx, expect, instance, match, lines, slot)
        if problem is None:
            return Result(True, instance, expect.label, lines[index], index)
        first_problem = first_problem or f"{problem}: {lines[index]}"
    return Result(False, instance, expect.label, first_problem or last_like(expect, lines))


JUDGES = {ONCE: judge_once, LAST: judge_last, NONE: judge_none, EACH_REMOTE: judge_each, EACH_MEMBER: judge_each}


def evaluate(ctx, expect, instance, slot, start):
    """One expectation for one instance, over the lines now; only lines from index `start` count."""
    lines = ctx.tail(instance, expect.source).lines()
    regex = re.compile(expect.regex.replace(TARGET_TOKEN, str(slot)))
    found = [(i, match) for i, line in enumerate(lines) if i >= start and (match := regex.search(line))]
    return JUDGES[expect.rule](ctx, expect, instance, slot, lines, found)


def start_after(earlier, instance):
    """The line index a match must start at: 0 without an earlier expectation, after its match, or never when it failed."""
    if earlier is None:
        return 0
    result = earlier.get(instance)
    return result.index + 1 if result and result.passed else NEVER


def run_expect(ctx, expect, target, slot, earlier, clock, sleep):
    """Every selected instance for one expectation, polling until all pass or the window closes. `earlier` holds the results
    of the expectation this one must come after."""
    instances = select(ctx, expect.who, target)
    results = {}
    deadline = clock() + expect.within_s
    while True:
        for instance in instances:
            if not results.get(instance) or not results[instance].passed:
                results[instance] = evaluate(ctx, expect, instance, slot, start_after(earlier, instance))
        if expect.rule == NONE or all(r.passed for r in results.values()) or clock() >= deadline:
            return results
        sleep(POLL_S)


def run_check(ctx, check, checks, target=None, clock=time.monotonic, sleep=time.sleep):
    """Runs a check and the checks it includes; the results in order, and the hub counters that must be zero."""
    if check.needs_target and target is None:
        raise ValueError(f"check {check.name} needs an instance number")
    slot = ctx.last_slot(target) if target is not None else 0
    found = {}
    out = []
    for expect in check.expects:
        results = run_expect(ctx, expect, target, slot, found.get(expect.after) if expect.after else None, clock, sleep)
        found[expect.label] = results
        out.extend(results[i] for i in sorted(results))
    counters = ctx.counters()
    for name in check.hub_zero:
        out.append(Result(counters.get(name, 0) == 0, None, f"hub {name}", f"{counters.get(name, 0)} since the last mark"))
    for included in check.includes:
        out.extend(run_check(ctx, checks[included], checks, target, clock, sleep))
    return out


def format_result(result):
    who = "hub" if result.instance is None else label_of(result.instance)
    return f"{'PASS' if result.passed else 'FAIL'} {who:7} {result.label}: {result.detail}"
