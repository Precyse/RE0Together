"""Tests of the assertion layer: every format string of the check table is still in adapters/sr2/src, every regex matches a
line made from its format, LogTail reads only after the mark, and the engine's rules (once, last, none, each remote, order,
hub counters, included checks) give PASS and FAIL on scripted logs."""
import re
import tempfile
import unittest
from pathlib import Path

import echo_checks as ck
import wire
from echo_check_table import CHECKS
from echo_session import steam_id

SOURCE_DIR = Path(__file__).resolve().parents[2] / "adapters" / "sr2" / "src"
TIMESTAMP = "2026-10-20 12:00:00.000 "
NAMES = ("link", "joined", "probe-a", "tunnel-clean", "pause", "kick", "leave", "hud", "waypoint", "refusal", "admission")
PORT = 27962
COUNT = 3


def all_expects():
    return [expect for check in CHECKS.values() for expect in check.expects]


def adapter_source():
    return "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in sorted(SOURCE_DIR.glob("*.[ch]*")))


class TableTest(unittest.TestCase):
    def test_the_brief_checks_exist(self):
        self.assertEqual(sorted(CHECKS), sorted(NAMES))

    def test_every_format_string_is_still_in_the_adapter_source(self):
        source = adapter_source()
        for expect in all_expects():
            for piece in ck.source_pieces(expect):
                self.assertIn(piece, source, f"{expect.label}: {piece!r} is no longer in adapters/sr2/src")

    def test_every_regex_matches_a_line_made_from_its_format(self):
        for expect in all_expects():
            line = TIMESTAMP + ck.sample_line(expect)
            self.assertRegex(line, re.compile(expect.regex.replace(ck.TARGET_TOKEN, "7")), f"{expect.label}: {line!r}")

    def test_every_row_names_its_source(self):
        for expect in all_expects():
            self.assertTrue(ck.source_pieces(expect) or expect.sample, expect.label)

    def test_included_checks_exist(self):
        for check in CHECKS.values():
            for name in check.includes:
                self.assertIn(name, CHECKS)

    def test_after_names_an_earlier_expectation_of_the_same_check(self):
        for check in CHECKS.values():
            labels = [expect.label for expect in check.expects]
            for position, expect in enumerate(check.expects):
                if expect.after:
                    self.assertIn(expect.after, labels[:position], expect.label)

    def test_sample_lines_substitute_every_specifier_kind(self):
        row = ck.Expect("x", ck.WHO_ALL, "", format="a %u b %zu c %llu d 0x%X e 0x%08X f %02X g %d h %s i %.*s", prefix="p: ",
                        samples=("one", None, 5, None, None, None, None, None, "two"))
        self.assertEqual(ck.sample_line(row), "p: a one b 7 c 5 d 0x1A e 0x1A f 1A g 7 h word i two")


class LogTailTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / "sr2_adapter.log"

    def tearDown(self):
        self.dir.cleanup()

    def test_old_lines_are_never_read(self):
        self.path.write_text("old 1\nold 2\n")
        tail = ck.LogTail(str(self.path))
        self.assertEqual(tail.lines(), [])
        with open(self.path, "a") as log:
            log.write("new 1\n")
        self.assertEqual(tail.lines(), ["new 1"])

    def test_a_mark_skips_what_was_written_before(self):
        tail = ck.LogTail(str(self.path))
        self.path.write_text("a\nb\n")
        self.assertEqual(tail.lines(), ["a", "b"])
        tail.mark()
        self.assertEqual(tail.lines(), [])
        with open(self.path, "a") as log:
            log.write("c\n")
        self.assertEqual(tail.lines(), ["c"])

    def test_a_truncated_log_is_read_from_its_start(self):
        self.path.write_text("a long first run\nsecond line\n")
        tail = ck.LogTail(str(self.path))
        self.path.write_text("run 2\n")
        self.assertEqual(tail.lines(), ["run 2"])


class FakeTail:
    def __init__(self, lines=()):
        self.log = list(lines)

    def lines(self):
        return list(self.log)


class FakeContext:
    """The hub as a check sees it: COUNT instances, instance k at slot k-1, one log and one status per instance."""

    def __init__(self, count=COUNT):
        self.instances = list(range(1, count + 1))
        self.port = PORT
        self.logs = {k: FakeTail() for k in self.instances}
        self.statuses = {k: FakeTail() for k in self.instances}
        self.drops = {}

    def tail(self, instance, source):
        return (self.statuses if source == ck.SOURCE_STATUS else self.logs)[instance]

    def slots(self):
        return {k: k - 1 for k in self.instances}

    def last_slot(self, instance):
        return instance - 1

    def address(self, instance):
        return wire.virtual_address(steam_id(instance))

    def linked(self):
        return set(self.instances)

    def counters(self):
        return self.drops

    def say(self, instance, text):
        self.logs[instance].log.append(TIMESTAMP + text)


class Clock:
    """A clock that only moves when the engine sleeps, so a failing window costs no real time."""

    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def run(ctx, name, target=None):
    clock = Clock()
    return ck.run_check(ctx, CHECKS[name], CHECKS, target, clock, clock.sleep)


def failures(results):
    return [r for r in results if not r.passed]


class EngineTest(unittest.TestCase):
    def setUp(self):
        self.ctx = FakeContext()

    def link_lines(self):
        for k in self.ctx.instances:
            self.ctx.say(k, f"tunnel: steam transport, launcher port {PORT}")
            self.ctx.say(k, f"tunnel: own address {self.ctx.address(k)}")
            self.ctx.say(k, "game socket: connection reset reports off")

    def test_link_passes_and_checks_the_address(self):
        self.link_lines()
        self.assertEqual(failures(run(self.ctx, "link")), [])
        self.ctx.logs[2].log[1] = TIMESTAMP + "tunnel: own address 10.9.9.9"
        failed = failures(run(self.ctx, "link"))
        self.assertEqual([(r.instance, r.label) for r in failed], [(2, "own address")])
        self.assertIn("10.9.9.9", failed[0].detail)

    def test_a_missing_line_fails_after_its_window_and_quotes_the_nearest_line(self):
        self.link_lines()
        del self.ctx.logs[3].log[2]
        self.ctx.say(3, "game socket: WSAIoctl failed 10022")
        failed = failures(run(self.ctx, "link"))
        self.assertEqual([(r.instance, r.label) for r in failed], [(3, "game socket bound")])
        self.assertIn("WSAIoctl failed", failed[0].detail)

    def test_joined_needs_the_linked_count_and_every_slot(self):
        ctx = self.ctx
        ctx.statuses[1].log.append(f"state=in_session detail={COUNT}")
        ctx.say(1, f"trace: session members {COUNT}")
        for k in ctx.instances:
            ctx.say(1, f"trace: member 0x{0x100 + k:X} slot {k - 1} state 4 {'local' if k == 1 else 'remote'}")
        for k in (2, 3):
            ctx.statuses[k].log.append("state=joined")
        self.assertEqual(failures(run(ctx, "joined")), [])
        ctx.say(1, "trace: remote member 0x9 slot 7 state 4 remote is outside the session slots 0..3")
        self.assertEqual([r.label for r in failures(run(ctx, "joined"))], ["slot inside the session"])

    def test_joined_fails_on_a_wrong_member_count(self):
        ctx = self.ctx
        ctx.statuses[1].log.append("state=in_session detail=2")
        self.assertIn("host status in_session", [r.label for r in failures(run(ctx, "joined"))])

    def test_each_remote_lists_the_slots_missing(self):
        ctx = self.ctx
        for k in ctx.instances:
            for other in ctx.instances:
                if other != k and not (k == 1 and other == 3):
                    ctx.say(k, f"trace: remote human 0x{0x200 + other:X} owner slot {other - 1} member 0x{0x300 + other:X} flags 0x00 downed 0")
        failed = [r for r in failures(run(ctx, "probe-a")) if r.label == "remote human per remote"]
        self.assertEqual([(r.instance, "[2]" in r.detail) for r in failed], [(1, True)])

    def test_a_remote_human_without_a_member_does_not_count(self):
        ctx = self.ctx
        ctx.say(1, "trace: remote human 0x201 owner slot 1 member none flags 0x00 downed 0")
        failed = [r for r in failures(run(ctx, "probe-a")) if r.instance == 1 and r.label == "remote human per remote"]
        self.assertEqual(len(failed), 1)

    def test_the_default_partner_must_be_the_hosts_human_for_a_guest(self):
        ctx = self.ctx
        ctx.say(2, "trace: remote human 0x201 owner slot 0 member 0x301 flags 0x00 downed 0")
        ctx.say(2, "trace: default partner now human 0x201 member 0x301")
        ctx.say(3, "trace: remote human 0x202 owner slot 2 member 0x302 flags 0x00 downed 0")
        ctx.say(3, "trace: default partner now human 0x202 member 0x302")
        failed = [r for r in failures(run(ctx, "probe-a")) if r.label == "guest partner is the host"]
        self.assertEqual([r.instance for r in failed], [3])

    def counters_line(self, **values):
        fields = dict(sent=10, received=10, no_peer=0, not_virtual=0, wrong_dst=0, inbox_full=0, oversize=0, large=0) | values
        return ("tunnel: sent {sent} received {received} dropped no_peer {no_peer} not_virtual {not_virtual} wrong_dst {wrong_dst} "
                "inbox_full {inbox_full} oversize {oversize}, large {large}").format(**fields)

    def test_tunnel_clean_reads_the_last_counters_and_the_hubs(self):
        ctx = self.ctx
        for k in ctx.instances:
            ctx.say(k, self.counters_line())
        self.assertEqual(failures(run(ctx, "tunnel-clean")), [])
        ctx.say(2, self.counters_line(no_peer=4))
        ctx.drops = {"not_linked": 2}
        failed = failures(run(ctx, "tunnel-clean"))
        self.assertEqual([(r.instance, r.label) for r in failed], [(2, "tunnel counters"), (None, "hub not_linked")])

    def pause_lines(self, order=("set", "clear", "apply")):
        lines = {"set": "trace: pause: slot 1 sent 1, mask 0x00 -> 0x02, local state 4, apply",
                 "clear": "trace: pause: slot 1 sent 0, mask 0x02 -> 0x00, local state 4, apply",
                 "apply": "trace: pause: apply 0x8CA190 with mask 0x00: count 1 -> 0, world paused 1 -> 0, remote pause 1 -> 0"}
        for key in order:
            self.ctx.say(1, lines[key])

    def test_pause_needs_set_then_clear_then_the_world_resumed(self):
        self.pause_lines()
        self.assertEqual(failures(run(self.ctx, "pause", 2)), [])

    def test_pause_in_the_wrong_order_fails(self):
        self.pause_lines(("clear", "apply", "set"))
        self.assertEqual([r.label for r in failures(run(self.ctx, "pause", 2))], ["slot resumed", "world resumed"])

    def test_pause_needs_the_instance_number(self):
        with self.assertRaises(ValueError):
            run(self.ctx, "pause")

    def test_pause_checks_the_slot_bit_of_the_target(self):
        self.pause_lines()
        self.ctx.say(1, "trace: pause: slot 2 sent 1, mask 0x00 -> 0x01, local state 4, apply")
        self.assertEqual(failures(run(self.ctx, "pause", 2)), [])
        failed = failures(run(self.ctx, "pause", 3))
        self.assertTrue(any("bit 2" in r.detail for r in failed))

    def test_kick_follows_the_picker_to_the_chosen_slot_and_the_guests_react(self):
        ctx = self.ctx
        ctx.say(1, "trace: kick: picker for 2 remote members: slots 1 2")
        ctx.say(1, "trace: kick: kicking slot 2 member 0x00000301 (chosen)")
        ctx.statuses[3].log += ["state=joined", "state=stopped"]
        ctx.say(2, "trace: leave: slot 2, dropped 1 locally, renamed 0, reissued 0")
        self.assertEqual(failures(run(ctx, "kick", 3)), [])
        ctx.statuses[3].log.pop()
        self.assertEqual([r.label for r in failures(run(ctx, "kick", 3))], ["kicked guest left the session"])

    def test_leave_includes_the_tunnel_clean_check(self):
        ctx = self.ctx
        ctx.say(1, "trace: pause: slot 1 left, mask 0x02 -> 0x00, apply")
        ctx.say(1, "trace: leave: slot 1, took over 2, renamed 1, reissued 3 to 1 member(s)")
        ctx.say(3, "trace: leave: slot 1, dropped 1 locally, renamed 0, reissued 0")
        labels = [r.label for r in failures(run(ctx, "leave", 2))]
        self.assertEqual(labels, ["tunnel counters"] * COUNT)

    def test_hud_lists_every_remote_slot(self):
        ctx = self.ctx
        ctx.say(1, "trace: hud: minimap arrows for 2 remote humans 0x00000201 (slot 1), 0x00000202 (slot 2)")
        ctx.say(1, "trace: hud: pause map arrows for 1 remote humans 0x00000201 (slot 1)")
        failed = failures(run(ctx, "hud"))
        self.assertEqual([r.label for r in failed], ["pause map arrows"])

    def test_waypoint_each_remote_and_no_fallback(self):
        ctx = self.ctx
        ctx.say(1, "trace: waypoint: to #PLAYER2# only (slot 1, member 0x301), 40 bytes")
        ctx.say(1, "trace: waypoint: to #PLAYER3# only (slot 2, member 0x302), 40 bytes")
        self.assertEqual(failures(run(ctx, "waypoint")), [])
        ctx.say(1, "trace: waypoint: #PLAYER4# names no remote member, sent to every remote")
        self.assertEqual([r.label for r in failures(run(ctx, "waypoint"))], ["every waypoint names a remote"])

    def test_refusal_names_the_different_player_counts(self):
        ctx = self.ctx
        reason = "version 2 players 3 flags 1 content 0x1F, ours version 2 players 4 flags 1 content 0x1F"
        ctx.say(1, f"version gate: join_req from 10.1.2.3:4200 refused, {reason}")
        ctx.statuses[3].log.append("state=failed detail=refused")
        self.assertEqual(failures(run(ctx, "refusal", 3)), [])
        ctx.logs[1].log[0] = ctx.logs[1].log[0].replace("players 3", "players 4")
        self.assertEqual([r.label for r in failures(run(ctx, "refusal", 3))], ["host refuses the guest"])

    def test_admission_waits_then_admits(self):
        ctx = self.ctx
        ctx.say(1, "trace: admission: member 0x301 is admitted now")
        ctx.say(1, "trace: admission: member 0x302 waits, member 0x301 is being admitted, request 5")
        self.assertEqual([r.label for r in failures(run(ctx, "admission"))], ["second guest admitted"])
        ctx.say(1, "trace: admission: member 0x302 is admitted now")
        self.assertEqual(failures(run(ctx, "admission")), [])

    def test_results_print_pass_or_fail_with_the_instance(self):
        ok = ck.Result(True, 1, "own address", "the line")
        bad = ck.Result(False, 3, "own address", "no line")
        hub = ck.Result(False, None, "hub no_slot", "2 since the last mark")
        self.assertEqual(ck.format_result(ok), "PASS host    own address: the line")
        self.assertEqual(ck.format_result(bad), "FAIL guest3  own address: no line")
        self.assertEqual(ck.format_result(hub), "FAIL hub     hub no_slot: 2 since the last mark")


if __name__ == "__main__":
    unittest.main()
