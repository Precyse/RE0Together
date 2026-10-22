"""The assertion layer: matchers over the adapter log, the launcher stdout and the fake's own journal, gate verdicts and the
report files. A gate is {id, title, expect matchers, forbid matchers}; verdicts are PASS, FAIL (the offending line or frame
quoted), MISSING (an expected line never came), SKIP (a need not met) and MANUAL (the answer to a visual check)."""
import json
from collections import namedtuple

PASS, FAIL, MISSING, SKIP, MANUAL = "PASS", "FAIL", "MISSING", "SKIP", "MANUAL"
QUOTE_LIMIT = 160
MS = 1000.0
SOURCE_ADAPTER = "adapter"

Result = namedtuple("Result", "verdict detail")


class Evidence:
    """What a gate is judged on: the log store, the journal, the named time marks and the mirrors of the run."""

    def __init__(self, store, journal, marks=None, mirrors=None, new_dumps=()):
        self.store, self.journal = store, journal
        self.marks = marks or {}
        self.mirrors = mirrors or {}
        self.new_dumps = list(new_dumps)

    def mark(self, name, default=None):
        return self.marks.get(name, default)

    def window(self, since, until):
        return (self.marks.get(since, 0.0) if since else 0.0), (self.marks.get(until) if until else None)


def quote(text):
    return text if len(text) <= QUOTE_LIMIT else text[:QUOTE_LIMIT] + "..."


class Matcher:
    """One condition of a gate. `anchor` is the literal prefix of the adapter format string the regex relies on."""

    anchor = None

    def check(self, evidence):
        raise NotImplementedError

    def describe(self):
        raise NotImplementedError


class Line(Matcher):
    """At least `count` log lines matching `pattern`, between the marks `since` and `until`, or within `within` seconds
    after the mark `after`."""

    def __init__(self, pattern, anchor=None, source=SOURCE_ADAPTER, count=1, since=None, until=None, after=None, within=None):
        self.pattern, self.anchor, self.source, self.count = pattern, anchor, source, count
        self.since, self.until, self.after, self.within = since, until, after, within

    def describe(self):
        window = "".join(f" {name} {value}" for name, value in (("since", self.since), ("until", self.until), ("after", self.after))
                         if value)
        return f"{self.source} line /{self.pattern}/" + (f" x{self.count}" if self.count > 1 else "") + window

    def hits(self, evidence):
        start, end = evidence.window(self.since, self.until)
        if self.after:
            start = evidence.mark(self.after, start)
            if self.within is not None:
                end = start + self.within * MS
        return evidence.store.search(self.pattern, self.source, start, end)

    def check(self, evidence):
        hits = self.hits(evidence)
        if len(hits) < self.count:
            return Result(MISSING, f"{len(hits)} of {self.count} x {self.pattern}")
        return Result(PASS, quote(hits[0][0].text))


class Forbid(Line):
    """Fails when any line matches."""

    def check(self, evidence):
        hits = self.hits(evidence)
        if hits:
            return Result(FAIL, quote(hits[0][0].text))
        return Result(PASS, "")


class NoneWithin(Matcher):
    """No second line with the same first group within `seconds` of the previous one (no flip-flop, no double owner)."""

    def __init__(self, pattern, seconds, anchor=None, source=SOURCE_ADAPTER, since=None, until=None):
        self.pattern, self.seconds, self.anchor, self.source, self.since, self.until = pattern, seconds, anchor, source, since, until

    def describe(self):
        return f"{self.source} no second /{self.pattern}/ for one key within {self.seconds} s"

    def check(self, evidence):
        start, end = evidence.window(self.since, self.until)
        last = {}
        for line, found in evidence.store.search(self.pattern, self.source, start, end):
            key = found.group(1) if found.groups() else ""
            if key in last and line.t - last[key] <= self.seconds * MS:
                return Result(FAIL, quote(line.text))
            last[key] = line.t
        return Result(PASS, "")


class Order(Matcher):
    """A line matching `second` comes after one matching `first`."""

    def __init__(self, first, second, anchor=None, source=SOURCE_ADAPTER):
        self.first, self.second, self.anchor, self.source = first, second, anchor, source

    def describe(self):
        return f"{self.source} /{self.second}/ after /{self.first}/"

    def check(self, evidence):
        firsts = evidence.store.search(self.first, self.source)
        seconds = evidence.store.search(self.second, self.source)
        if not firsts or not seconds:
            return Result(MISSING, f"{self.first!r} then {self.second!r}")
        if seconds[-1][0].t < firsts[0][0].t:
            return Result(FAIL, f"{quote(seconds[-1][0].text)} before {quote(firsts[0][0].text)}")
        return Result(PASS, quote(seconds[-1][0].text))


class Wire(Matcher):
    """A predicate over the journal and mirrors: returns True, False, or (verdict, detail)."""

    def __init__(self, label, predicate):
        self.label, self.predicate = label, predicate

    def describe(self):
        return f"journal: {self.label}"

    def check(self, evidence):
        outcome = self.predicate(evidence)
        if isinstance(outcome, Result):
            return outcome
        return Result(PASS if outcome else FAIL, self.label)


class Agree(Matcher):
    """Every log line matching `pattern` has its groups equal to what `mirror(evidence, groups)` returns (None = no line
    to compare is not a failure, but at least one line must exist)."""

    def __init__(self, label, pattern, mirror, anchor=None, source=SOURCE_ADAPTER, since=None, until=None):
        self.label, self.pattern, self.mirror, self.anchor, self.source = label, pattern, mirror, anchor, source
        self.since, self.until = since, until

    def describe(self):
        return f"{self.source} /{self.pattern}/ agrees with the mirror: {self.label}"

    def check(self, evidence):
        start, end = evidence.window(self.since, self.until)
        hits = evidence.store.search(self.pattern, self.source, start, end)
        if not hits:
            return Result(MISSING, self.label)
        for line, found in hits:
            expected = self.mirror(evidence, found.groups())
            if expected is not None and expected != found.groups():
                return Result(FAIL, f"{quote(line.text)} (mirror {expected})")
        return Result(PASS, self.label)


class SumEquals(Matcher):
    """The sum of the first group of every matching line equals `expected(evidence)`."""

    def __init__(self, label, pattern, expected, anchor=None, source=SOURCE_ADAPTER):
        self.label, self.pattern, self.expected, self.anchor, self.source = label, pattern, expected, anchor, source

    def describe(self):
        return f"{self.source} sum of /{self.pattern}/ equals the journal: {self.label}"

    def check(self, evidence):
        hits = evidence.store.search(self.pattern, self.source)
        if not hits:
            return Result(MISSING, self.label)
        total, expected = sum(int(found.group(1)) for _, found in hits), self.expected(evidence)
        if total != expected:
            return Result(FAIL, f"{self.label}: log {total}, journal {expected}")
        return Result(PASS, f"{self.label} {total}")


class Latency(Matcher):
    """The first log line matching `pattern` follows the journal time `sent(evidence)` by at most `max_ms`."""

    def __init__(self, label, pattern, sent, max_ms, anchor=None, source=SOURCE_ADAPTER):
        self.label, self.pattern, self.sent, self.max_ms, self.anchor, self.source = label, pattern, sent, max_ms, anchor, source

    def describe(self):
        return f"{self.source} /{self.pattern}/ within {self.max_ms:.0f} ms of the send: {self.label}"

    def check(self, evidence):
        sent = self.sent(evidence)
        if sent is None:
            return Result(MISSING, f"{self.label}: nothing sent")
        hits = evidence.store.search(self.pattern, self.source, sent)
        if not hits:
            return Result(MISSING, self.label)
        delay = hits[0][0].t - sent
        verdict = PASS if delay <= self.max_ms else FAIL
        return Result(verdict, f"{self.label} {delay:.0f} ms")


class Gate:
    def __init__(self, gate_id, title, expect=(), forbid=()):
        self.id, self.title, self.expect, self.forbid = gate_id, title, tuple(expect), tuple(forbid)

    def anchors(self):
        """(log source, literal format-string prefix) of every matcher that names one."""
        return [(getattr(m, "source", SOURCE_ADAPTER), m.anchor) for m in self.expect + self.forbid if m.anchor]

    def table(self):
        """The gate as lines for `gates <scenario>`."""
        rows = [f"{self.id}  {self.title}"]
        rows += [f"    expect {m.describe()}" for m in self.expect]
        rows += [f"    forbid {m.describe()}" for m in self.forbid]
        return rows

    def judge(self, evidence):
        for matcher in self.forbid:
            result = matcher.check(evidence)
            if result.verdict == FAIL:
                return Result(FAIL, f"{matcher.pattern}: {result.detail}")
        results = [matcher.check(evidence) for matcher in self.expect]
        for wanted in (FAIL, MISSING):
            for result in results:
                if result.verdict == wanted:
                    return result
        return Result(PASS, ", ".join(r.detail for r in results if r.detail))


def forbid(pattern, anchor=None, **options):
    return Forbid(pattern, anchor=anchor, **options)


GLOBAL_FORBIDS = (
    forbid(r"game_tick: '.*' disabled", "game_tick: '%s' disabled"),
    forbid(r"raised exception 0x", "raised exception 0x%08lx"),
    forbid(r"\(reset faulted\)", "(reset faulted)"),
    forbid(r"Sync stopped:", "Sync stopped"),
    forbid(r"state_net: refused", "state_net: refused"),
    forbid(r"router: dropped", "router: dropped"),
    forbid(r"refused a malformed", "refused a malformed"),
    forbid(r"refused malformed", "refused malformed"),
)
GLOBAL_GATE_ID = "global"


def judge_global(evidence):
    """The forbids every scenario carries plus the new crash dumps, as one gate."""
    if evidence.new_dumps:
        return Result(FAIL, "new crash dump " + ", ".join(str(p.name) for p in evidence.new_dumps))
    return Gate(GLOBAL_GATE_ID, "global forbids", forbid=GLOBAL_FORBIDS).judge(evidence)


class Report:
    """Verdicts per gate, the totals, and the two files (report.txt, report.json)."""

    def __init__(self):
        self.rows = []  # (gate id, verdict, detail)
        self.notes = []

    def add(self, gate_id, verdict, detail=""):
        self.rows.append((gate_id, verdict, detail))

    def totals(self):
        counts = {}
        for _, verdict, _ in self.rows:
            counts[verdict] = counts.get(verdict, 0) + 1
        return counts

    @property
    def failed(self):
        return any(verdict in (FAIL, MISSING) for _, verdict, _ in self.rows)

    def text(self, folder=None):
        lines = [f"{verdict} {gate_id} {detail}".rstrip() for gate_id, verdict, detail in self.rows]
        totals = self.totals()
        lines.append("totals " + " ".join(f"{name} {totals.get(name, 0)}" for name in (PASS, FAIL, MISSING, SKIP, MANUAL)))
        lines.extend(self.notes)
        if folder:
            lines.append(f"run folder {folder}")
        return "\n".join(lines)

    def write(self, folder):
        (folder / "report.txt").write_text(self.text(folder) + "\n", encoding="utf-8")
        payload = {"rows": [{"gate": g, "verdict": v, "detail": d} for g, v, d in self.rows], "totals": self.totals(),
                   "notes": self.notes}
        (folder / "report.json").write_text(json.dumps(payload, indent=1), encoding="utf-8")

