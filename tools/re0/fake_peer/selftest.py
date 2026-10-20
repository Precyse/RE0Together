"""selftest: the wire check against the adapter headers, the golden frames file, the log anchors of every gate, the unit tests
and, with --smoke, two fakes against each other through real launchers."""
import unittest
from pathlib import Path

from fake_peer import gates, golden, wire_check
from fake_peer.scenarios import ORDER

TESTS_DIR = Path(__file__).resolve().parent / "tests"
TEST_PATTERN = "test_*.py"
TOP_LEVEL = Path(__file__).resolve().parents[1]


def scenario_anchors():
    anchors = []
    for module in ORDER:
        for seat in module.SEATS:
            for gate in module.gates(seat):
                anchors += gate.anchors()
    for forbid in gates.GLOBAL_FORBIDS:
        if forbid.anchor:
            anchors.append((forbid.source, forbid.anchor))
    return anchors


def run_unit_tests():
    suite = unittest.defaultTestLoader.discover(str(TESTS_DIR), pattern=TEST_PATTERN, top_level_dir=str(TOP_LEVEL))
    result = unittest.TextTestRunner(verbosity=0).run(suite)
    return result.testsRun, len(result.failures) + len(result.errors)


def run(smoke=False):
    problems = []
    problems += wire_check.check_all()
    print("wire check " + ("ok" if not problems else f"{len(problems)} problems"))
    if not golden.is_current():
        problems.append("tests/golden_frames.bin is not what `golden` writes now")
    anchors = scenario_anchors()
    anchor_problems = wire_check.check_anchors(anchors)
    problems += anchor_problems
    print(f"anchors {len(set(anchors))} " + ("ok" if not anchor_problems else f"{len(anchor_problems)} missing"))
    ran, failed = run_unit_tests()
    print(f"unit tests {ran} run, {failed} failed")
    if failed:
        problems.append(f"{failed} unit tests failed")
    if smoke:
        from fake_peer import smoke as smoke_test
        problems += smoke_test.run()
    for problem in problems:
        print(f"FAIL {problem}")
    print("selftest " + ("passed" if not problems else "failed"))
    return 1 if problems else 0
