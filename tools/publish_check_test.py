"""Checks tools/publish_check.py's rules for game Lua: by name, and end to end in a throwaway git repo.

usage: python tools/publish_check_test.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import publish_check  # noqa: E402

CHECK_SCRIPT = Path(__file__).with_name("publish_check.py")
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def test_names():
    check(publish_check.path_problems("adapters/sr2/scripts/bh01.lua") != [], "a generated script is refused")
    check(publish_check.path_problems("adapters/sr2/scripts/notes.txt") != [], "anything under adapters/sr2/scripts is refused")
    check(publish_check.path_problems("docs/mission.LUA") != [], "Lua outside tools/sr2/lua is refused, whatever the case")
    check(publish_check.path_problems("adapters/sr2/ug_lib.lua") != [], "a library copy next to the adapter is refused")
    check(publish_check.path_problems("tools/sr2/lua/coop_rules.lua") == [], "our own Lua library is allowed")
    check(publish_check.path_problems("adapters/sr2/scripts.delta.json") == [], "the delta that builds the scripts is allowed")


def test_repo():
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)

        def git(*arguments):
            subprocess.run(["git", *arguments], cwd=root, check=True, capture_output=True)

        def run_check():
            return subprocess.run([sys.executable, str(CHECK_SCRIPT)], cwd=root, capture_output=True, text=True)

        git("init", "-q")
        (root / "tools/sr2/lua").mkdir(parents=True)
        (root / "tools/sr2/lua/coop_rules.lua").write_text("function f() end\n")
        git("add", "-A")
        check(run_check().returncode == 0, "a repo with only our Lua is clean")
        (root / "adapters/sr2/scripts").mkdir(parents=True)
        (root / "adapters/sr2/scripts/bh01.lua").write_text("-- the game's script\n")
        git("add", "-A")
        result = run_check()
        check(result.returncode == 1 and "adapters/sr2/scripts/bh01.lua" in result.stdout, "a committed generated script fails the check and is named")


test_names()
test_repo()
print("publish_check_test: " + ("all checks passed" if failures == 0 else f"{failures} failed"))
sys.exit(1 if failures else 0)
