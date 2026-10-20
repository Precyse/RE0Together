"""Runs every test of the echo rig without the game: the session rules, the hub end to end with stand-in adapters, the clone
folders on a temporary tree and the assertion layer. Usage: python tools/sr2/echo_test.py"""
import sys
import unittest

MODULES = ("echo_session_test", "echo_hub_test", "echo_clones_test", "echo_checks_test")


def main():
    suite = unittest.defaultTestLoader.loadTestsFromNames(MODULES)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    print("echo_test OK" if result.wasSuccessful() else "FAIL: echo_test")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
