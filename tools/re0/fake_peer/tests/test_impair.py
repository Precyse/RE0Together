"""The seeded link model (impair.py) and the two-direction shaper (shaper.py), in virtual time."""
import unittest

from fake_peer.impair import Impairment, Params, Rng, load_profiles, make_params
from fake_peer.shaper import BOTH, IN, OUT, Shaper, TimedQueue

FRAMES = 20000
FRAME_BYTES = 100
FRAME_GAP_MS = 10.0
RELIABLE_FRAMES = 200
FRAME_STEP_S = 0.001


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


def run_frames(impairment, count=FRAMES, size=FRAME_BYTES):
    return [impairment.decide(index * FRAME_GAP_MS, size) for index in range(count)]


class RngTests(unittest.TestCase):
    def test_a_seed_and_label_repeat_and_differ(self):
        draw = lambda seed, label: Rng(seed, label).u64()
        self.assertEqual(draw(1, "out"), draw(1, "out"))
        self.assertNotEqual(draw(1, "out"), draw(2, "out"))
        self.assertNotEqual(draw(1, "out"), draw(1, "in"))

    def test_floats_stay_in_the_unit_range(self):
        rng = Rng(7, "x")
        self.assertTrue(all(0.0 <= rng.random() < 1.0 for _ in range(1000)))


class ProfileTests(unittest.TestCase):
    def test_the_named_profiles_load(self):
        profiles = load_profiles()
        self.assertEqual(list(profiles), ["lan", "good_wifi", "bad_wifi", "mobile_tether", "transatlantic", "hell"])
        self.assertEqual(profiles["hell"].latency_ms, 200)
        self.assertEqual(profiles["mobile_tether"].bandwidth_bps, 2000000)

    def test_overrides_go_on_top_of_a_profile_and_unknown_names_are_errors(self):
        self.assertEqual(make_params("lan", loss=0.3).loss, 0.3)
        self.assertEqual(make_params("lan", loss=0.3).latency_ms, 0.5)
        self.assertTrue(make_params().idle())
        with self.assertRaises(KeyError):
            make_params("nope")
        with self.assertRaises(KeyError):
            make_params(speed=3)


class ImpairmentTests(unittest.TestCase):
    def test_the_same_seed_gives_the_same_verdicts_and_another_seed_does_not(self):
        params = make_params("bad_wifi")
        first = run_frames(Impairment(params, 9, OUT), 500)
        self.assertEqual(first, run_frames(Impairment(params, 9, OUT), 500))
        self.assertNotEqual(first, run_frames(Impairment(params, 10, OUT), 500))

    def test_delay_stays_within_latency_and_jitter(self):
        verdicts = run_frames(Impairment(Params(latency_ms=40.0, jitter_ms=10.0), 1, OUT), 2000)
        delays = [v.delays_ms[0] for v in verdicts]
        self.assertTrue(all(30.0 <= d <= 50.0 for d in delays))
        self.assertGreater(max(delays) - min(delays), 10.0)

    def test_jitter_larger_than_latency_never_goes_below_zero(self):
        verdicts = run_frames(Impairment(Params(latency_ms=5.0, jitter_ms=50.0), 1, OUT), 2000)
        self.assertTrue(all(v.delays_ms[0] >= 0.0 for v in verdicts))

    def test_flat_loss_matches_its_rate(self):
        verdicts = run_frames(Impairment(Params(loss=0.1), 3, OUT))
        self.assertAlmostEqual(sum(v.dropped for v in verdicts) / FRAMES, 0.1, delta=0.01)

    def test_burst_loss_matches_the_long_run_rate_and_clusters(self):
        params = Params(burst_enter=0.02, burst_exit=0.2, burst_loss=0.9)
        verdicts = run_frames(Impairment(params, 4, OUT))
        dropped = [v.dropped for v in verdicts]
        self.assertAlmostEqual(sum(dropped) / FRAMES, params.expected_loss(), delta=0.02)
        after_drop = [dropped[i + 1] for i in range(FRAMES - 1) if dropped[i]]
        self.assertGreater(sum(after_drop) / len(after_drop), 3 * params.expected_loss())
        self.assertTrue(all(v.reason == "burst" for v in verdicts if v.dropped))

    def test_reorder_lets_some_frames_skip_the_latency(self):
        verdicts = run_frames(Impairment(Params(latency_ms=100.0, reorder=0.2), 5, OUT))
        skipped = sum(v.delays_ms[0] == 0.0 for v in verdicts) / FRAMES
        self.assertAlmostEqual(skipped, 0.2, delta=0.02)

    def test_duplicates_deliver_twice(self):
        verdicts = run_frames(Impairment(Params(latency_ms=10.0, duplicate=0.1), 6, OUT))
        twice = sum(len(v.delays_ms) == 2 for v in verdicts) / FRAMES
        self.assertAlmostEqual(twice, 0.1, delta=0.01)

    def test_a_bandwidth_cap_queues_then_tail_drops(self):
        params = Params(bandwidth_bps=80000, buffer_ms=50.0)  # 100 byte frames: 10 ms each on the wire
        impairment = Impairment(params, 1, OUT)
        verdicts = [impairment.decide(0.0, FRAME_BYTES) for _ in range(10)]
        self.assertEqual([round(v.delays_ms[0]) for v in verdicts[:6]], [10, 20, 30, 40, 50, 60])
        self.assertTrue(all(v.dropped and v.reason == "bandwidth" for v in verdicts[6:]))
        self.assertFalse(impairment.decide(1000.0, FRAME_BYTES).dropped)

    def test_the_idle_model_delivers_everything_at_once(self):
        verdicts = run_frames(Impairment(Params(), 1, OUT), 200)
        self.assertTrue(all(v.delays_ms == [0.0] for v in verdicts))


class ShaperTests(unittest.TestCase):
    def make(self, **changes):
        self.clock = FakeClock()
        return Shaper(make_params(**changes), seed=1, clock=self.clock)

    def test_an_idle_shaper_has_nothing_to_do(self):
        self.assertTrue(Shaper().idle())
        self.assertFalse(self.make(latency_ms=40.0).idle())

    def test_delay_and_jitter_move_the_delivery_time(self):
        shaper = self.make(latency_ms=40.0, jitter_ms=10.0)
        for _ in range(20):
            (due,) = shaper.out.plan(reliable=False, size=FRAME_BYTES)
            self.assertTrue(self.clock.now + 0.03 <= due <= self.clock.now + 0.05)

    def test_reliable_frames_get_delay_only_and_stay_in_order(self):
        shaper = self.make(latency_ms=100.0, jitter_ms=90.0, loss=1.0, duplicate=1.0, reorder=1.0)
        dues = []
        for _ in range(RELIABLE_FRAMES):
            plan = shaper.out.plan(reliable=True, size=FRAME_BYTES)
            self.assertEqual(len(plan), 1)
            self.assertGreaterEqual(plan[0], self.clock.now + 0.01)
            dues.extend(plan)
            self.clock.now += FRAME_STEP_S
        self.assertEqual(dues, sorted(dues))

    def test_unreliable_frames_get_the_full_model(self):
        self.assertEqual(self.make(loss=1.0).out.plan(reliable=False, size=FRAME_BYTES), [])
        self.assertEqual(len(self.make(latency_ms=5.0, duplicate=1.0).out.plan(reliable=False, size=FRAME_BYTES)), 2)

    def test_the_two_directions_have_their_own_streams(self):
        shaper = self.make(loss=0.5)
        out = [bool(shaper.out.plan(False, FRAME_BYTES)) for _ in range(64)]
        inbound = [bool(shaper.inbound.plan(False, FRAME_BYTES)) for _ in range(64)]
        self.assertNotEqual(out, inbound)

    def test_the_same_seed_repeats_a_run(self):
        runs = []
        for _ in range(2):
            shaper = self.make(loss=0.3, latency_ms=20.0, jitter_ms=5.0)
            runs.append([shaper.out.plan(False, FRAME_BYTES) for _ in range(100)])
        self.assertEqual(runs[0], runs[1])

    def test_a_cut_drops_unreliable_and_holds_reliable_in_order(self):
        clock = FakeClock()
        shaper = Shaper(clock=clock)
        shaper.cut(2.0)
        self.assertEqual(shaper.out.plan(False, FRAME_BYTES), [])
        first, second = shaper.out.plan(True, FRAME_BYTES)[0], shaper.out.plan(True, FRAME_BYTES)[0]
        self.assertEqual(first, clock.now + 2.0)
        self.assertLessEqual(first, second)
        clock.now += 2.5
        self.assertTrue(shaper.idle())

    def test_a_cut_is_one_way(self):
        shaper = Shaper(clock=FakeClock())
        shaper.cut(2.0, IN)
        self.assertEqual(len(shaper.out.plan(False, FRAME_BYTES)), 1)
        self.assertEqual(shaper.inbound.plan(False, FRAME_BYTES), [])
        self.assertFalse(shaper.idle())
        shaper.cut(2.0, BOTH)
        self.assertEqual(shaper.out.plan(False, FRAME_BYTES), [])

    def test_a_stall_holds_everything_until_it_ends(self):
        clock = FakeClock()
        shaper = Shaper(clock=clock)
        shaper.stall(1.0)
        self.assertEqual(shaper.out.plan(False, FRAME_BYTES), [clock.now + 1.0])
        self.assertEqual(shaper.out.plan(True, FRAME_BYTES), [clock.now + 1.0])
        self.assertEqual(len(shaper.inbound.plan(False, FRAME_BYTES)), 1)
        self.assertTrue(shaper.inbound.idle())

    def test_configure_changes_one_direction_or_both(self):
        shaper = Shaper(clock=FakeClock())
        shaper.configure(OUT, latency_ms=30.0)
        self.assertEqual((shaper.out.params.latency_ms, shaper.inbound.params.latency_ms), (30.0, 0.0))
        shaper.configure(BOTH, loss=0.2)
        self.assertEqual((shaper.out.params.loss, shaper.inbound.params.loss), (0.2, 0.2))
        with self.assertRaises(KeyError):
            shaper.configure(OUT, speed=1)
        with self.assertRaises(ValueError):
            shaper.cut(1.0, "sideways")


class TimedQueueTests(unittest.TestCase):
    def test_items_come_out_in_due_order_and_only_when_due(self):
        queue = TimedQueue()
        queue.push(3.0, "c")
        queue.push(1.0, "a")
        queue.push(1.0, "b")
        self.assertTrue(queue)
        self.assertEqual(queue.pop_due(2.0), ["a", "b"])
        self.assertEqual(queue.pop_due(2.0), [])
        self.assertEqual(queue.pop_due(3.0), ["c"])
        self.assertFalse(queue)


if __name__ == "__main__":
    unittest.main()
