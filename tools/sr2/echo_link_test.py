"""Tests of the echo hub's seeded fault model without sockets or a clock: the port of the SplitMix64 stream, determinism per
seed, each fault (loss, burst loss, jitter, reorder, duplicates, bandwidth cap) over thousands of virtual datagrams, the named
profiles, one-way cuts and the report a check reads to prove the profile was active."""
import json
import tempfile
import unittest
from pathlib import Path

import echo_impair as im
from echo_link import LinkModel

SPLITMIX_SEED_ZERO_FIRST = 0xE220A8397B1DCDAF
DATAGRAMS = 20000
SIZE = 200
STEP_MS = 1.0
TOLERANCE = 0.02
PROFILE_NAMES = ("none", "lan", "good_wifi", "bad_wifi", "mobile_tether", "transatlantic", "hell")


def stream(params, seed=7, count=DATAGRAMS, step_ms=STEP_MS, size=SIZE):
    """The verdicts of one direction for `count` datagrams sent `step_ms` apart, as (send time, verdict)."""
    direction = im.Impairment(params, seed, "1>2")
    return [(i * step_ms, direction.decide(i * step_ms, size)) for i in range(count)]


def loss_rate(verdicts):
    return sum(v.dropped for _t, v in verdicts) / len(verdicts)


def loss_runs(verdicts):
    """The lengths of the runs of consecutive drops."""
    runs, current = [], 0
    for _t, verdict in verdicts:
        if verdict.dropped:
            current += 1
        elif current:
            runs.append(current)
            current = 0
    return runs


class RngTest(unittest.TestCase):
    def test_the_stream_is_splitmix64(self):
        rng = im.Rng(1, "x")
        rng.state = 0
        self.assertEqual(rng.next_u64(), SPLITMIX_SEED_ZERO_FIRST)

    def test_a_seed_and_label_give_one_stream(self):
        first = [im.Rng(5, "1>2").random() for _ in range(3)]
        self.assertEqual(first, [im.Rng(5, "1>2").random() for _ in range(3)])
        self.assertNotEqual(im.Rng(5, "1>2").random(), im.Rng(6, "1>2").random())
        self.assertNotEqual(im.Rng(5, "1>2").random(), im.Rng(5, "2>1").random())

    def test_random_is_in_the_unit_interval(self):
        rng = im.Rng(3, "u")
        values = [rng.random() for _ in range(5000)]
        self.assertTrue(all(0.0 <= v < 1.0 for v in values))
        self.assertAlmostEqual(sum(values) / len(values), 0.5, delta=0.03)


class FaultTest(unittest.TestCase):
    def test_a_perfect_link_delivers_everything_at_once(self):
        verdicts = stream(im.Params(), count=1000)
        self.assertTrue(all(v.delays_ms == (0.0,) and not v.reordered for _t, v in verdicts))

    def test_the_same_seed_gives_the_same_verdicts_and_another_seed_does_not(self):
        params = im.profile_params("bad_wifi")
        self.assertEqual(stream(params, 11, 3000), stream(params, 11, 3000))
        self.assertNotEqual(stream(params, 11, 3000), stream(params, 12, 3000))

    def test_loss_matches_its_rate(self):
        rate = loss_rate(stream(im.Params(loss=0.1)))
        self.assertAlmostEqual(rate, 0.1, delta=TOLERANCE)

    def test_the_reason_of_a_drop_is_named(self):
        reasons = {v.reason for _t, v in stream(im.Params(loss=0.5)) if v.dropped}
        self.assertEqual(reasons, {im.REASON_LOSS})

    def test_burst_loss_matches_the_two_state_model_and_comes_in_runs(self):
        params = im.Params(burst_enter=0.02, burst_exit=0.2, burst_loss=1.0)
        verdicts = stream(params)
        self.assertAlmostEqual(loss_rate(verdicts), params.expected_loss(), delta=TOLERANCE)
        runs = loss_runs(verdicts)
        self.assertGreater(sum(runs) / len(runs), 3)
        flat = loss_runs(stream(im.Params(loss=params.expected_loss())))
        self.assertLess(sum(flat) / len(flat), 1.5)
        self.assertEqual({v.reason for _t, v in verdicts if v.dropped}, {im.REASON_BURST})

    def test_latency_and_jitter_bound_the_delay(self):
        delays = [v.delays_ms[0] for _t, v in stream(im.Params(latency_ms=50, jitter_ms=10), count=3000)]
        self.assertGreaterEqual(min(delays), 40)
        self.assertLessEqual(max(delays), 60)
        self.assertGreater(max(delays) - min(delays), 15)

    def test_jitter_never_makes_a_delay_negative(self):
        delays = [v.delays_ms[0] for _t, v in stream(im.Params(latency_ms=2, jitter_ms=20), count=3000)]
        self.assertEqual(min(delays), 0.0)

    def test_reordered_datagrams_arrive_before_earlier_ones(self):
        verdicts = stream(im.Params(latency_ms=50, reorder=0.3), count=3000)
        arrivals = [(t + v.delays_ms[0], i) for i, (t, v) in enumerate(verdicts)]
        inversions = sum(1 for (a, _i), (b, _j) in zip(arrivals, arrivals[1:]) if b < a)
        self.assertGreater(inversions, 300)
        self.assertAlmostEqual(sum(v.reordered for _t, v in verdicts) / len(verdicts), 0.3, delta=0.03)

    def test_no_reorder_keeps_the_order(self):
        arrivals = [t + v.delays_ms[0] for t, v in stream(im.Params(latency_ms=50), count=2000)]
        self.assertEqual(arrivals, sorted(arrivals))

    def test_duplicates_give_a_second_delay(self):
        verdicts = stream(im.Params(latency_ms=10, duplicate=0.2))
        self.assertAlmostEqual(sum(len(v.delays_ms) == 2 for _t, v in verdicts) / len(verdicts), 0.2, delta=TOLERANCE)
        self.assertTrue(all(len(v.delays_ms) in (1, 2) for _t, v in verdicts))

    def test_a_bandwidth_cap_queues_then_tail_drops(self):
        params = im.Params(bandwidth_bps=80000, buffer_ms=100)
        verdicts = stream(params, count=2000, size=1000)
        kept = [(t, v) for t, v in verdicts if not v.dropped]
        self.assertEqual({v.reason for _t, v in verdicts if v.dropped}, {im.REASON_BANDWIDTH})
        self.assertAlmostEqual(len(kept) / (2000 * STEP_MS / 1000), 10, delta=2)
        self.assertLessEqual(max(v.delays_ms[0] for _t, v in kept), 200)
        self.assertGreater(max(v.delays_ms[0] for _t, v in kept), 100)

    def test_a_slow_sender_never_hits_the_cap(self):
        verdicts = stream(im.Params(bandwidth_bps=80000, buffer_ms=100), count=200, step_ms=500, size=1000)
        self.assertFalse(any(v.dropped for _t, v in verdicts))

    def test_expected_loss_of_the_two_state_model(self):
        self.assertAlmostEqual(im.Params(loss=0.02).expected_loss(), 0.02)
        self.assertAlmostEqual(im.Params(burst_enter=0.1, burst_exit=0.4, burst_loss=1.0).expected_loss(), 0.2)
        self.assertEqual(im.Params(burst_enter=0.0, burst_exit=0.0).expected_loss(), 0.0)


class ProfileTest(unittest.TestCase):
    def test_every_named_profile_loads_and_none_does_nothing(self):
        profiles = im.load_profiles()
        self.assertEqual(sorted(profiles), sorted(PROFILE_NAMES))
        self.assertTrue(profiles["none"].is_perfect())
        self.assertTrue(all(not profiles[name].is_perfect() for name in PROFILE_NAMES if name != "none"))

    def test_the_profiles_keep_the_shape_of_the_original_names(self):
        profiles = im.load_profiles()
        self.assertEqual((profiles["bad_wifi"].latency_ms, profiles["bad_wifi"].loss, profiles["bad_wifi"].burst_loss), (35, 0.02, 0.6))
        self.assertEqual(profiles["mobile_tether"].bandwidth_bps, 2000000)
        self.assertGreater(profiles["hell"].expected_loss(), profiles["bad_wifi"].expected_loss())

    def test_an_unknown_profile_names_the_known_ones(self):
        with self.assertRaises(KeyError) as raised:
            im.profile_params("nowhere")
        self.assertIn("bad_wifi", str(raised.exception))

    def test_an_unknown_key_in_a_profile_file_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "p.json"
            path.write_text(json.dumps({"odd": {"latency": 5}}))
            with self.assertRaises(KeyError):
                im.load_profiles(path)

    def test_the_file_carries_the_original_notice(self):
        notice = json.loads(im.PROFILES_PATH.read_text(encoding="utf-8"))["_notice"]
        self.assertIn("Copyright (c) 2026 cranebeaker and contributors", notice)
        self.assertIn("Copyright (c) 2026 cranebeaker and contributors", Path(im.__file__).read_text(encoding="utf-8"))


class LinkModelTest(unittest.TestCase):
    def test_a_one_way_cut_drops_one_direction_only(self):
        link = LinkModel.named("none", 1)
        link.cut(1, 2)
        self.assertEqual(link.decide(1, 2, 0.0, SIZE).reason, im.REASON_CUT)
        self.assertFalse(link.decide(2, 1, 0.0, SIZE).dropped)
        self.assertFalse(link.decide(1, 3, 0.0, SIZE).dropped)
        link.uncut(1, 2)
        self.assertFalse(link.decide(1, 2, 0.0, SIZE).dropped)

    def test_directions_have_their_own_random_streams(self):
        link = LinkModel.named("hell", 5)
        forward = [link.decide(1, 2, i * STEP_MS, SIZE) for i in range(300)]
        backward = [link.decide(2, 1, i * STEP_MS, SIZE) for i in range(300)]
        self.assertNotEqual(forward, backward)
        again = LinkModel.named("hell", 5)
        self.assertEqual(forward, [again.decide(1, 2, i * STEP_MS, SIZE) for i in range(300)])

    def test_the_report_counts_loss_duplicates_and_reorders_and_leaves_cuts_out_of_the_rate(self):
        link = LinkModel.named("bad_wifi", 9)
        link.cut(3, 4)
        for i in range(DATAGRAMS):
            link.decide(1, 2, i * STEP_MS, SIZE)
        for i in range(100):
            link.decide(3, 4, i * STEP_MS, SIZE)
        report = link.report()
        self.assertEqual((report.offered, report.cut, report.impaired), (DATAGRAMS + 100, 100, DATAGRAMS))
        self.assertAlmostEqual(report.observed_loss, report.expected_loss, delta=TOLERANCE)
        self.assertGreater(report.duplicated, 0)
        self.assertGreater(report.reordered, 0)
        self.assertFalse(report.perfect)
        self.assertIn("bad_wifi seed 9", report.describe())
        self.assertIn("100 cut", report.describe())

    def test_clearing_the_counters_starts_a_new_count(self):
        link = LinkModel.named("hell", 2)
        for i in range(500):
            link.decide(1, 2, i, SIZE)
        link.clear_counters()
        self.assertEqual(link.report().offered, 0)

    def test_an_override_replaces_one_field_and_shows_in_the_profile_name(self):
        link = LinkModel.named("lan", 3, latency_ms=80)
        self.assertEqual(link.params.latency_ms, 80)
        self.assertEqual(link.params.jitter_ms, im.profile_params("lan").jitter_ms)
        self.assertEqual(link.profile, "lan+latency_ms")

    def test_switching_the_profile_changes_every_direction_and_keeps_the_streams(self):
        link = LinkModel.named("none", 4)
        self.assertFalse(link.decide(1, 2, 0.0, SIZE).dropped)
        link.switch("blackout", im.Params(loss=1.0))
        self.assertTrue(link.decide(1, 2, 1.0, SIZE).dropped)
        self.assertTrue(link.decide(2, 3, 1.0, SIZE).dropped)
        self.assertEqual(link.report().profile, "blackout")

    def test_the_perfect_link_reports_itself_as_perfect(self):
        link = LinkModel.named("none", 1)
        link.decide(1, 2, 0.0, SIZE)
        self.assertTrue(link.report().perfect)
        self.assertEqual(link.report().lost, 0)


if __name__ == "__main__":
    unittest.main()
