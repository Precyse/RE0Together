"""The runner (needs check, report, skips, the scenario API) and the command line over the scenario table."""
import io
import unittest
from contextlib import redirect_stderr, redirect_stdout

from fake_peer import cli, gates, wire
from fake_peer.runner import Options, Stopped, check_needs
from fake_peer.scenarios import ORDER, REGISTRY
from fake_peer.tests.support import deliver, make_context, scratch_path
from fake_peer.wire import Msg

T7 = REGISTRY["t7_flags"]


class NeedsTests(unittest.TestCase):
    def test_a_state_changing_seat_needs_scratch(self):
        self.assertEqual(check_needs(T7, Options(seat="guest")), [])
        problems = check_needs(T7, Options(seat="host"))
        self.assertIn("--scratch", problems[0])
        self.assertEqual(check_needs(T7, Options(seat="host", scratch=True)), [])

    def test_a_seat_the_scenario_does_not_support_is_refused(self):
        self.assertIn("runs as guest", check_needs(REGISTRY["t2_echo"], Options(seat="host"))[0])


class TransportTests(unittest.TestCase):
    def test_the_standin_is_only_for_a_game_that_is_the_guest(self):
        with self.assertRaises(ValueError):
            make_context_for(Options(seat="guest", transport="standin", no_game=True, out=scratch_path()))

    def test_bridge_transport_needs_a_port(self):
        with self.assertRaises(ValueError):
            make_context_for(Options(seat="guest", transport="bridge", no_game=True, out=scratch_path()))


def make_context_for(options):
    from fake_peer.runner import Context
    return Context(options)


class ScenarioTableTests(unittest.TestCase):
    def test_every_scenario_is_complete_and_named_once(self):
        ids = [module.ID for module in ORDER]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(set(ids), set(REGISTRY))
        for module in ORDER:
            for name in ("ID", "ROWS", "SEATS", "SCRATCH_SEATS", "SUMMARY", "gates", "run"):
                self.assertTrue(hasattr(module, name), f"{module.ID} lacks {name}")
            for seat in module.SEATS:
                ids = [gate.id for gate in module.gates(seat)]
                self.assertEqual(len(ids), len(set(ids)), f"{module.ID} {seat}")

    def test_gates_print_as_a_table(self):
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(cli.print_gates(REGISTRY["t4_join"], "guest"), 0)
        text = out.getvalue()
        self.assertIn("T4.host-echo", text)
        self.assertIn("expect adapter line", text)
        with redirect_stderr(io.StringIO()):
            self.assertEqual(cli.print_gates(REGISTRY["t2_echo"], "host"), 2)

    def test_list_names_every_scenario_without_decoration_characters(self):
        out = io.StringIO()
        with redirect_stdout(out):
            cli.list_scenarios()
        for module in ORDER:
            self.assertIn(module.ID, out.getvalue())
        self.assertNotIn("·", out.getvalue())
        self.assertNotIn("•", out.getvalue())

    def test_the_parser_reads_a_run_command(self):
        args = cli.build_parser().parse_args(["run", "t7_flags", "--seat", "host", "--scratch", "--delay-ms", "40"])
        options = cli.options_from(args)
        self.assertEqual((options.scenario, options.seat, options.scratch, options.shaper.delay_ms), ("t7_flags", "host", True, 40.0))

    def test_every_gate_of_every_scenario_has_a_describable_matcher(self):
        for module in ORDER:
            for seat in module.SEATS:
                for gate in module.gates(seat):
                    self.assertTrue(all(m.describe() for m in gate.expect + gate.forbid), gate.id)


class ContextTests(unittest.TestCase):
    def test_the_report_has_one_row_per_gate_skips_manual_questions_and_the_global_row(self):
        ctx = make_context("guest")
        ctx.store.add("adapter", "join_sync: snapshot sent (Billy scene 0x2d, Rebecca scene 0x2d)")
        ctx.add_gates(REGISTRY["t4_join"].gates("guest")[:1])
        ctx.add_gates([gates.Gate("T4.F9", "second pair", expect=[gates.Line("never")])])
        ctx.skip("T4.F9", "needs something")
        ctx.manual("T4.look", "Billy stands in the right room")
        ctx._ask = lambda question: "unanswered"
        report = ctx.build_report()
        verdicts = {gate_id: verdict for gate_id, verdict, _ in report.rows}
        self.assertEqual(verdicts["T4.host-echo"], gates.MISSING)  # the snapshot line is there, the extras line is not
        self.assertEqual((verdicts["T4.F9"], verdicts["T4.look"], verdicts["global"]), (gates.SKIP, gates.MANUAL, gates.PASS))
        self.assertTrue(report.failed)

    def test_require_skips_gates_naming_the_run_that_fills_the_library(self):
        ctx = make_context("guest")
        self.assertFalse(ctx.require("enemy clip", "T5.enter"))
        self.assertIn("t5_observe", ctx.skipped["T5.enter"])

    def test_wait_until_records_a_miss_and_the_stop_flag_ends_a_wait(self):
        ctx = make_context("guest")
        self.assertFalse(ctx.wait_until(lambda: False, 0.1, "a thing"))
        self.assertEqual(ctx.waits_missed, ["a thing"])
        ctx.stopped.set()
        with self.assertRaises(Stopped):
            ctx.wait(1.0)

    def test_wait_frame_finds_the_games_frame_after_the_call(self):
        ctx = make_context("guest")
        import threading
        threading.Timer(0.1, lambda: deliver(ctx, Msg.ROOM_STATE, wire.RoomState.pack(scene=3))).start()
        found = ctx.wait_frame(Msg.ROOM_STATE, lambda entry: wire.RoomState.unpack(entry.payload).scene == 3, timeout=3.0)
        self.assertIsNotNone(found)

    def test_marks_and_the_shaper_api(self):
        ctx = make_context("guest")
        ctx.mark("cut")
        ctx.shape(cut=1.0, delay_ms=40.0)
        self.assertIn("cut", ctx.marks)
        self.assertEqual(ctx.shaper.config.delay_ms, 40.0)
        self.assertFalse(ctx.shaper.idle())
        self.assertEqual(ctx.duration(120.0), 120.0)
        ctx.options.seconds = 5.0
        self.assertEqual(ctx.duration(120.0), 5.0)


if __name__ == "__main__":
    unittest.main()
