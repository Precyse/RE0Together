"""The assertion layer on synthetic adapter logs: pass, fail, missing, windows, repeat filtering, a log that shrinks."""
import unittest

from fake_peer import gates
from fake_peer.gates import (FAIL, MISSING, PASS, Agree, Evidence, Gate, Latency, Line, NoneWithin, Order, Report, SumEquals, Wire,
                             forbid)
from fake_peer.library import Journal
from fake_peer.logwatch import FileTail, LogStore, parse_stamp
from fake_peer.tests.support import scratch_path


def evidence(lines, marks=None, mirrors=None):
    store = LogStore()
    for t, text in lines:
        store.add("adapter", text, t)
    return Evidence(store, Journal(), marks or {}, mirrors or {})


class LineTests(unittest.TestCase):
    def test_an_expected_line_passes_and_a_missing_one_is_missing(self):
        ev = evidence([(1000, "join_extras: sent serial 1 (claims 0)")])
        self.assertEqual(Gate("g", "t", [Line(r"join_extras: sent serial 1")]).judge(ev).verdict, PASS)
        self.assertEqual(Gate("g", "t", [Line(r"session_ready: echoed")]).judge(ev).verdict, MISSING)

    def test_a_count_needs_that_many_lines(self):
        ev = evidence([(1000, "observed from slot 1"), (1001, "observed from slot 1")])
        self.assertEqual(Gate("g", "t", [Line(r"observed from slot 1", count=3)]).judge(ev).verdict, MISSING)
        self.assertEqual(Gate("g", "t", [Line(r"observed from slot 1", count=2)]).judge(ev).verdict, PASS)

    def test_a_forbidden_line_fails_and_quotes_it(self):
        ev = evidence([(1000, "state_net: refused type 0x0140 outside state mode")])
        result = Gate("g", "t", forbid=[forbid(r"state_net: refused")]).judge(ev)
        self.assertEqual(result.verdict, FAIL)
        self.assertIn("0x0140", result.detail)

    def test_windows_between_marks(self):
        ev = evidence([(1000, "stale, holding"), (5000, "stale, holding")], marks={"menu-start": 2000, "menu-end": 4000})
        window = forbid(r"stale, holding", since="menu-start", until="menu-end")
        self.assertEqual(Gate("g", "t", forbid=[window]).judge(ev).verdict, PASS)
        after = Line(r"stale, holding", after="menu-end", within=0.5)
        self.assertEqual(Gate("g", "t", [after]).judge(ev).verdict, MISSING)
        self.assertEqual(Gate("g", "t", [Line(r"stale, holding", after="menu-end")]).judge(ev).verdict, PASS)

    def test_a_source_is_kept_apart(self):
        store = LogStore()
        store.add("join_launcher", "Fresh channel: 3 types", 1000)
        ev = Evidence(store, Journal())
        self.assertEqual(Gate("g", "t", [Line(r"Fresh channel: 3 types", source="host_launcher")]).judge(ev).verdict, MISSING)
        self.assertEqual(Gate("g", "t", [Line(r"Fresh channel: 3 types", source="join_launcher")]).judge(ev).verdict, PASS)


class MatcherTests(unittest.TestCase):
    def test_none_within_flags_a_second_owner_line_for_the_same_scene(self):
        pattern = r"owner scene (0x\w+) slot \d"
        same = evidence([(1000, "owner scene 0x2d slot 0"), (2500, "owner scene 0x2d slot 1")])
        other = evidence([(1000, "owner scene 0x2d slot 0"), (1500, "owner scene 0x2e slot 1")])
        spaced = evidence([(1000, "owner scene 0x2d slot 0"), (4000, "owner scene 0x2d slot 1")])
        self.assertEqual(NoneWithin(pattern, 2).check(same).verdict, FAIL)
        self.assertEqual(NoneWithin(pattern, 2).check(other).verdict, PASS)
        self.assertEqual(NoneWithin(pattern, 2).check(spaced).verdict, PASS)

    def test_order(self):
        ev = evidence([(1000, "released"), (2000, "claimed")])
        self.assertEqual(Order("released", "claimed").check(ev).verdict, PASS)
        self.assertEqual(Order("claimed", "released").check(ev).verdict, FAIL)
        self.assertEqual(Order("released", "never").check(ev).verdict, MISSING)

    def test_agree_compares_groups_with_the_mirror(self):
        ev = evidence([(1000, "owner 0x2d epoch 3")])
        agree = Agree("epoch", r"owner (0x\w+) epoch (\d+)", lambda e, groups: ("0x2d", "3"))
        differ = Agree("epoch", r"owner (0x\w+) epoch (\d+)", lambda e, groups: ("0x2d", "4"))
        self.assertEqual(agree.check(ev).verdict, PASS)
        self.assertEqual(differ.check(ev).verdict, FAIL)

    def test_sum_equals_the_journal_count(self):
        ev = evidence([(1000, "sent 3 entries"), (2000, "sent 4 entries")])
        self.assertEqual(SumEquals("fx", r"sent (\d+) entries", lambda e: 7).check(ev).verdict, PASS)
        self.assertEqual(SumEquals("fx", r"sent (\d+) entries", lambda e: 8).check(ev).verdict, FAIL)

    def test_latency_against_a_send_time(self):
        ev = evidence([(1100, "room_move: Billy -> scene 0x2d")])
        self.assertEqual(Latency("move", r"room_move: Billy", lambda e: 1000.0, 500).check(ev).verdict, PASS)
        self.assertEqual(Latency("move", r"room_move: Billy", lambda e: 1000.0, 50).check(ev).verdict, FAIL)
        self.assertEqual(Latency("move", r"room_move: Billy", lambda e: None, 50).check(ev).verdict, MISSING)

    def test_wire_takes_a_bool_or_a_result(self):
        ev = evidence([])
        self.assertEqual(Wire("x", lambda e: True).check(ev).verdict, PASS)
        self.assertEqual(Wire("x", lambda e: False).check(ev).verdict, FAIL)
        self.assertEqual(Wire("x", lambda e: gates.Result(MISSING, "nothing")).check(ev).verdict, MISSING)


class GlobalAndReportTests(unittest.TestCase):
    def test_the_global_forbids_catch_the_test_plan_failure_lines(self):
        for text in ("game_tick: 'pose_capture' disabled until the next resync", "pose_capture raised exception 0xc0000005 at 0x1",
                     "puppet: Billy released (reset faulted)", "router: dropped type 0x0140 from slot 1 (guest only)",
                     "session_ready: refused a malformed message from slot 1"):
            self.assertEqual(gates.judge_global(evidence([(1000, text)])).verdict, FAIL, text)
        self.assertEqual(gates.judge_global(evidence([(1000, "scene: load serial 1 (scene 0x2d)")])).verdict, PASS)

    def test_a_new_crash_dump_fails(self):
        store = LogStore()
        ev = Evidence(store, Journal(), new_dumps=[scratch_path() / "crash-1.dmp"])
        self.assertEqual(gates.judge_global(ev).verdict, FAIL)

    def test_the_report_lists_one_line_per_gate_and_totals(self):
        report = Report()
        report.add("T4.host-echo", PASS, "echoed")
        report.add("T9.R2", FAIL, "bad line")
        report.add("T5.X2", gates.SKIP, "needs a recording")
        text = report.text("C:/run")
        self.assertIn("PASS T4.host-echo echoed", text)
        self.assertIn("totals PASS 1 FAIL 1 MISSING 0 SKIP 1 MANUAL 0", text)
        self.assertTrue(report.failed)
        self.assertNotIn("\u00b7", text)
        self.assertNotIn("\u2022", text)


class LogTailTests(unittest.TestCase):
    def test_stamps_are_parsed_as_local_time_milliseconds(self):
        t, text = parse_stamp("2026-10-20 10:00:00.250 config: partner_mode=state")
        self.assertEqual(text, "config: partner_mode=state")
        t2, _ = parse_stamp("2026-10-20 10:00:01.250 x")
        self.assertAlmostEqual(t2 - t, 1000.0)
        self.assertEqual(parse_stamp("no stamp here"), (None, "no stamp here"))

    def test_a_tail_reads_new_lines_only_and_rereads_a_shrunk_file(self):
        path = scratch_path() / "adapter.log"
        path.write_text("2026-10-20 10:00:00.000 old line\n", encoding="utf-8")
        store = LogStore()
        tail = FileTail(path, "adapter", store, stamped=True, from_end=True)
        tail.poll()
        self.assertEqual(len(store.lines), 0)
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("2026-10-20 10:00:01.000 first\n2026-10-20 10:00:02.000 half")
        tail.poll()
        self.assertEqual([line.text for line in store.lines], ["first"])
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(" line\n")
        tail.poll()
        self.assertEqual(store.lines[-1].text, "half line")
        path.write_text("2026-10-20 11:00:00.000 restarted\n", encoding="utf-8")
        tail.poll()
        self.assertEqual([line.text for line in store.lines][-2:], ["adapter log restarted", "restarted"])

    def test_wait_returns_a_line_that_arrives_later(self):
        store = LogStore()
        import threading
        threading.Timer(0.1, lambda: store.add("adapter", "late line")).start()
        found = store.wait(r"late", 2.0, "adapter")
        self.assertIsNotNone(found)
        self.assertIsNone(store.wait(r"never", 0.2, "adapter"))


if __name__ == "__main__":
    unittest.main()
