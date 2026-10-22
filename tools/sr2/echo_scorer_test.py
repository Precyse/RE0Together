"""Self-tests of the echo scorer (docs/SR2_NOTES.md "Echo testing"): a known-bad log must FAIL. Every check has a golden log
that passes, and the scorer is then fed twins of it: each line removed in turn, the logs empty, unreadable or older than the
mark, and constructed bad lines (the wrong slot, the wrong count, a dirty counter, the wrong order...). Each twin must FAIL by
the label it is built for. A line that was never written, a missing file or an expectation that could not be checked never
counts as a pass: only a "no such line" row or one of the hub's own zero counters may pass on an empty log."""
import copy
import tempfile
import unittest
from pathlib import Path

import echo_checks as ck
from echo_check_table import CHECKS
from echo_checks_test import COUNT, FakeContext, TIMESTAMP, failures, link_report, run

HOST, GUEST2, GUEST3 = 1, 2, 3
PORT = FakeContext().port
SKIPPED_BY_DESIGN = ("link-active",)
NO_LINE_RULES = (ck.NONE,)

COUNTERS = ("tunnel: sent 10 received 10 dropped no_peer {no_peer} not_virtual {not_virtual} wrong_dst {wrong_dst} "
            "inbox_full {inbox_full} oversize {oversize}, large 0")
REFUSAL = "version gate: join_req from 10.1.2.3:4200 refused, version 2 players 3 flags 1 content 0x1F, ours version 2 players 4 flags 1 content 0x1F"


class Golden:
    """The lines every instance logged for one check, and the target of the check."""

    def __init__(self, name, target=None, logs=None, statuses=None, drops=None):
        self.name = name
        self.target = target
        logs, statuses = logs or {}, statuses or {}
        self.logs = {k: list(logs.get(k, ())) for k in range(1, COUNT + 1)}
        self.statuses = {k: list(statuses.get(k, ())) for k in range(1, COUNT + 1)}
        self.drops = dict(drops or {})

    def context(self):
        ctx = FakeContext()
        for k in ctx.instances:
            for line in self.logs[k]:
                ctx.say(k, line)
            ctx.statuses[k].log.extend(self.statuses[k])
        ctx.drops = dict(self.drops)
        return ctx

    def run(self):
        return run(self.context(), self.name, self.target)

    def lines(self):
        """(source, instance, index) of every line the golden has."""
        return [(source, k, i) for source, store in (("log", self.logs), ("status", self.statuses))
                for k in store for i in range(len(store[k]))]

    def without(self, source, instance, index):
        twin = copy.deepcopy(self)
        del (twin.logs if source == "log" else twin.statuses)[instance][index]
        return twin

    def edited(self, instance, needle, replacement, source="log"):
        """A twin whose first line holding `needle` has it replaced."""
        twin = copy.deepcopy(self)
        store = twin.logs if source == "log" else twin.statuses
        for i, line in enumerate(store[instance]):
            if needle in line:
                store[instance][i] = line.replace(needle, replacement, 1)
                return twin
        raise AssertionError(f"{self.name}: no line with {needle!r} on instance {instance}")

    def added(self, instance, line, source="log"):
        twin = copy.deepcopy(self)
        (twin.logs if source == "log" else twin.statuses)[instance].append(line)
        return twin


def counters(**dirty):
    return COUNTERS.format(**dict(no_peer=0, not_virtual=0, wrong_dst=0, inbox_full=0, oversize=0) | dirty)


def all_counters():
    return {k: [counters()] for k in range(1, COUNT + 1)}


def link_golden():
    logs = {k: [f"tunnel: steam transport, launcher port {PORT}", f"tunnel: own address {FakeContext().address(k)}",
                "game socket: connection reset reports off"] for k in range(1, COUNT + 1)}
    return Golden("link", logs=logs)


def joined_golden():
    host = ["trace: session members 3"] + [
        f"trace: member 0x{0x100 + k:X} slot {k - 1} state 4 {'local' if k == 1 else 'remote'}" for k in range(1, COUNT + 1)]
    return Golden("joined", logs={HOST: host},
                  statuses={HOST: ["state=in_session detail=3"], GUEST2: ["state=joined"], GUEST3: ["state=joined"]})


def probe_a_golden():
    logs = {}
    for k in range(1, COUNT + 1):
        logs[k] = [f"trace: remote human 0x{0x200 + o:X} owner slot {o - 1} member 0x{0x300 + o:X} flags 0x00 downed 0"
                   for o in range(1, COUNT + 1) if o != k]
    logs[HOST].append("trace: default partner now human 0x202 member 0x302")
    logs[GUEST2].append("trace: default partner now human 0x201 member 0x301")
    logs[GUEST3].append("trace: default partner now human 0x201 member 0x301")
    return Golden("probe-a", logs=logs)


def tunnel_clean_golden():
    return Golden("tunnel-clean", logs=all_counters())


def pause_golden():
    return Golden("pause", target=GUEST2, logs={HOST: [
        "trace: pause: slot 1 sent 1, mask 0x00 -> 0x02, local state 4, apply",
        "trace: pause: slot 1 sent 0, mask 0x02 -> 0x00, local state 4, apply",
        "trace: pause: apply 0x8CA190 with mask 0x00: count 1 -> 0, world paused 1 -> 0, remote pause 1 -> 0"]})


def kick_golden():
    return Golden("kick", target=GUEST3,
                  logs={HOST: ["trace: kick: picker for 2 remote members: slots 1 2",
                               "trace: kick: kicking slot 2 member 0x00000301 (chosen)"],
                        GUEST2: ["trace: leave: slot 2, dropped 1 locally, renamed 0, reissued 0"]},
                  statuses={GUEST3: ["state=stopped"]})


def leave_golden():
    logs = all_counters()
    logs[HOST] += ["trace: pause: slot 1 left, mask 0x02 -> 0x00, apply",
                   "trace: leave: slot 1, took over 2, renamed 1, reissued 3 to 1 member(s)"]
    logs[GUEST3] += ["trace: leave: slot 1, dropped 1 locally, renamed 0, reissued 0"]
    return Golden("leave", target=GUEST2, logs=logs)


def hud_golden():
    return Golden("hud", logs={HOST: [
        "trace: hud: minimap arrows for 2 remote humans 0x00000201 (slot 1), 0x00000202 (slot 2)",
        "trace: hud: pause map arrows for 2 remote humans 0x00000201 (slot 1), 0x00000202 (slot 2)"]})


def waypoint_golden():
    return Golden("waypoint", logs={HOST: ["trace: waypoint: to #PLAYER2# only (slot 1, member 0x301), 40 bytes",
                                           "trace: waypoint: to #PLAYER3# only (slot 2, member 0x302), 40 bytes"]})


def refusal_golden():
    return Golden("refusal", target=GUEST3, logs={HOST: [REFUSAL]}, statuses={GUEST3: ["state=failed detail=refused"]})


def admission_golden():
    return Golden("admission", logs={HOST: ["trace: admission: member 0x302 waits, member 0x301 is being admitted, request 5",
                                            "trace: admission: member 0x302 is admitted now"]})


GOLDENS = {golden.name: golden for golden in (
    link_golden(), joined_golden(), probe_a_golden(), tunnel_clean_golden(), pause_golden(), kick_golden(), leave_golden(),
    hud_golden(), waypoint_golden(), refusal_golden(), admission_golden())}


def failed_labels(golden):
    return {r.label for r in failures(golden.run())}


class GoldenTest(unittest.TestCase):
    def test_every_check_has_a_golden_log_or_is_a_hub_probe(self):
        self.assertEqual(sorted(GOLDENS), sorted(name for name in CHECKS if name not in SKIPPED_BY_DESIGN))

    def test_the_golden_logs_pass(self):
        for name, golden in GOLDENS.items():
            self.assertEqual([r.label for r in failures(golden.run())], [], name)

    def test_a_golden_log_passes_with_other_lines_in_between(self):
        for name, golden in GOLDENS.items():
            noisy = copy.deepcopy(golden)
            for k in noisy.logs:
                noisy.logs[k] = ["unrelated: line"] + noisy.logs[k] + ["trace: something else 5"]
            self.assertEqual(failures(noisy.run()), [], name)


class MissingLineTest(unittest.TestCase):
    def test_removing_any_single_line_of_a_golden_log_fails_its_check(self):
        for name, golden in GOLDENS.items():
            for source, instance, index in golden.lines():
                twin = golden.without(source, instance, index)
                with self.subTest(check=name, source=source, instance=instance, line=golden.logs[instance][index] if source == "log"
                                  else golden.statuses[instance][index]):
                    self.assertTrue(failed_labels(twin), "the check still passes without that line")

    def test_an_empty_log_fails_every_check_and_only_no_line_rows_pass(self):
        no_line_labels = {expect.label for check in CHECKS.values() for expect in check.expects if expect.rule in NO_LINE_RULES}
        for name in GOLDENS:
            results = Golden(name, target=GOLDENS[name].target).run()
            with self.subTest(check=name):
                self.assertTrue(failures(results), "an empty log passed")
                self.assertFalse([r.label for r in results if r.passed and r.label not in no_line_labels and r.instance is not None])

    def test_a_check_always_produces_results(self):
        for name, golden in GOLDENS.items():
            self.assertTrue(golden.run(), name)

    def test_unreadable_logs_fail_every_check(self):
        class Unreadable(FakeContext):
            def tail(self, instance, source):
                return ck.LogTail(str(Path(tempfile.gettempdir()) / "echo_scorer_no_such_dir" / f"{source}{instance}.log"))

        for name, golden in GOLDENS.items():
            with self.subTest(check=name):
                self.assertTrue(failures(run(Unreadable(), name, golden.target)))

    def test_lines_written_before_the_mark_never_pass(self):
        class Marked(FakeContext):
            """The golden lines sit in real files, but the tails were marked after they were written."""

            def __init__(self, golden, folder):
                super().__init__()
                self.tails = {}
                for source, store in (("log", golden.logs), ("status", golden.statuses)):
                    for k, lines in store.items():
                        path = Path(folder) / f"{source}{k}.txt"
                        path.write_text("".join(TIMESTAMP + line + "\n" for line in lines))
                        self.tails[(source, k)] = ck.LogTail(str(path))

            def tail(self, instance, source):
                return self.tails[(source, instance)]

        for name, golden in GOLDENS.items():
            with tempfile.TemporaryDirectory() as folder, self.subTest(check=name):
                self.assertTrue(failures(run(Marked(golden, folder), name, golden.target)))


# (check, twin builder, labels that must FAIL)
def bad_logs():
    link, joined, probe, tunnel = GOLDENS["link"], GOLDENS["joined"], GOLDENS["probe-a"], GOLDENS["tunnel-clean"]
    pause, kick, leave, hud = GOLDENS["pause"], GOLDENS["kick"], GOLDENS["leave"], GOLDENS["hud"]
    waypoint, refusal, admission = GOLDENS["waypoint"], GOLDENS["refusal"], GOLDENS["admission"]
    out = {
        "launcher port is not the hub's": (link.edited(GUEST2, f"launcher port {PORT}", "launcher port 4200"), {"steam transport"}),
        "own address is another instance's": (link.edited(GUEST3, FakeContext().address(3), FakeContext().address(2)), {"own address"}),
        "connection resets still reported": (link.edited(HOST, "reports off", "reports on"), {"game socket bound"}),
        "ioctl failure instead of the socket line": (link.edited(GUEST2, "connection reset reports off", "WSAIoctl failed 10022"),
                                                     {"game socket bound"}),
        "host sees one member too few": (joined.edited(HOST, "detail=3", "detail=2", "status"), {"host status in_session"}),
        "session members does not match the links": (joined.edited(HOST, "session members 3", "session members 4"), {"session members"}),
        "a member never got its slot": (joined.edited(HOST, "slot 2 state", "slot 7 state"), {"member per slot"}),
        "a guest is still dialog": (joined.edited(GUEST3, "state=joined", "state=dialog", "status"), {"guest status joined"}),
        "slot outside the session": (joined.added(HOST, "trace: remote member 0x9 slot 7 state 4 remote is outside the session slots 0..3"),
                                     {"slot inside the session"}),
        "a human without a member": (joined.added(HOST, "partner: human 0x201 owner slot 5 matches no member"),
                                     {"every human has a member"}),
        "the guest's partner is the other guest": (probe.edited(GUEST3, "default partner now human 0x201", "default partner now human 0x202"),
                                                   {"guest partner is the host"}),
        "the host's partner is itself": (probe.edited(HOST, "default partner now human 0x202", "default partner now human 0x201"),
                                         {"host partner is the first guest"}),
        "a remote human without a member": (probe.edited(HOST, "member 0x303", "member none"), {"remote human per remote"}),
        "a remote human with the wrong owner": (probe.edited(GUEST2, "owner slot 2", "owner slot 3"), {"remote human per remote"}),
        "dirty counters after clean ones": (tunnel.added(GUEST2, counters(no_peer=3)), {"tunnel counters"}),
        "hub drops while no peer is linked": (Golden("tunnel-clean", logs=tunnel.logs, drops={"not_linked": 1}), {"hub not_linked"}),
        "hub drops with no slot": (Golden("tunnel-clean", logs=tunnel.logs, drops={"no_slot": 4}), {"hub no_slot"}),
        "pause sets the wrong slot's bit": (pause.edited(HOST, "mask 0x00 -> 0x02", "mask 0x00 -> 0x01"), {"slot paused"}),
        "pause never clears the bit": (pause.edited(HOST, "mask 0x02 -> 0x00", "mask 0x02 -> 0x02"), {"slot resumed"}),
        "the world stays paused": (pause.edited(HOST, "count 1 -> 0, world paused 1 -> 0", "count 1 -> 1, world paused 1 -> 1"),
                                   {"world resumed"}),
        "another slot paused": (pause.edited(HOST, "pause: slot 1 sent 1", "pause: slot 2 sent 1"), {"slot paused"}),
        "the picker lists too few": (kick.edited(HOST, "slots 1 2", "slots 1"), {"picker lists every remote"}),
        "the wrong slot is kicked": (kick.edited(HOST, "kicking slot 2", "kicking slot 1"), {"picked slot kicked"}),
        "the kicked guest stays joined": (kick.edited(GUEST3, "state=stopped", "state=joined", "status"), {"kicked guest left the session"}),
        "the other guest names another slot": (kick.edited(GUEST2, "leave: slot 2", "leave: slot 1"), {"other guests dropped the leaver"}),
        "the host named another slot": (leave.edited(HOST, "leave: slot 1, took over", "leave: slot 2, took over"),
                                        {"host took the leaver over"}),
        "the pause slot did not go": (leave.edited(HOST, "pause: slot 1 left", "pause: slot 2 left"), {"host saw the pause slot go"}),
        "the guest dropped another slot": (leave.edited(GUEST3, "leave: slot 1, dropped", "leave: slot 2, dropped"),
                                           {"guests dropped the leaver"}),
        "a leave leaves a dirty tunnel": (leave.added(HOST, counters(inbox_full=2)), {"tunnel counters"}),
        "the minimap lists a slot too few": (hud.edited(HOST, "minimap arrows for 2 remote humans 0x00000201 (slot 1), 0x00000202 (slot 2)",
                                                        "minimap arrows for 1 remote humans 0x00000201 (slot 1)"), {"minimap arrows"}),
        "the pause map counts a human too many": (hud.edited(HOST, "pause map arrows for 2", "pause map arrows for 3"), {"pause map arrows"}),
        "a waypoint to a slot that is not a member": (waypoint.edited(HOST, "(slot 2,", "(slot 3,"), {"waypoint per remote"}),
        "a waypoint that fell back to everyone": (waypoint.added(HOST, "trace: waypoint: #PLAYER4# names no remote member, sent to every remote"),
                                                  {"every waypoint names a remote"}),
        "the refusal names equal player counts": (refusal.edited(HOST, "players 3", "players 4"), {"host refuses the guest"}),
        "the refused guest is joined": (refusal.edited(GUEST3, "state=failed", "state=joined", "status"), {"guest status failed"}),
        "the waiting line lost its request number": (admission.edited(HOST, "request 5", "request"), {"second guest waits"}),
        "admitted before it waited": (Golden("admission", logs={HOST: list(reversed(admission.logs[HOST]))}), {"second guest admitted"}),
    }
    return {name: (twin, labels) for name, (twin, labels) in out.items()}


class KnownBadLogTest(unittest.TestCase):
    def test_each_constructed_bad_log_fails_with_its_label(self):
        for description, (twin, labels) in bad_logs().items():
            with self.subTest(description):
                self.assertTrue(labels <= failed_labels(twin), f"{description}: failed {failed_labels(twin)}, expected {labels}")

    def test_each_tunnel_counter_failing_alone_fails(self):
        for counter in ("no_peer", "not_virtual", "wrong_dst", "inbox_full", "oversize"):
            twin = GOLDENS["tunnel-clean"].added(GUEST3, counters(**{counter: 1}))
            with self.subTest(counter):
                failed = failures(twin.run())
                self.assertEqual([(r.instance, r.label) for r in failed], [(GUEST3, "tunnel counters")])
                self.assertIn(counter, failed[0].detail)

    def test_the_newest_counters_decide(self):
        twin = Golden("tunnel-clean", logs={k: [counters(no_peer=9), counters()] for k in range(1, COUNT + 1)})
        self.assertEqual(failures(twin.run()), [])

    def test_every_constructed_twin_covers_a_row_of_its_check(self):
        labels = {expect.label for check in CHECKS.values() for expect in check.expects} | {"hub no_slot", "hub not_linked",
                                                                                           "tunnel counters"}
        for description, (_twin, wanted) in bad_logs().items():
            self.assertTrue(wanted <= labels, f"{description}: {wanted - labels} is not a row of the table")

    def test_every_row_of_every_check_has_a_bad_log_that_fails_it(self):
        covered = set()
        for _twin, labels in bad_logs().values():
            covered |= labels
        rows = {expect.label for check in CHECKS.values() for expect in check.expects}
        self.assertEqual(sorted(rows - covered), [], "rows no known-bad log fails")


class HubProbeTest(unittest.TestCase):
    def test_the_link_probe_never_passes_without_proof(self):
        ctx = FakeContext()
        self.assertTrue(failures(run(ctx, "link-active")), "a perfect link counted as an active profile")
        ctx.link = link_report(offered=0, lost=0)
        self.assertTrue(failures(run(ctx, "link-active")), "no traffic counted as an active profile")
        ctx.link = link_report()
        self.assertEqual(failures(run(ctx, "link-active")), [])

    def test_a_failed_result_is_never_counted_as_a_pass_in_the_summary_text(self):
        results = [ck.Result(True, 1, "a", "x"), ck.Result(False, 2, "b", "y"), ck.Result(False, None, "hub c", "z")]
        lines = [ck.format_result(r) for r in results]
        self.assertEqual([line.split()[0] for line in lines], ["PASS", "FAIL", "FAIL"])


if __name__ == "__main__":
    unittest.main()
