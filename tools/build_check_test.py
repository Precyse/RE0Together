"""Mismatched builds refuse to play: a guest on another build than the host stops before the game starts, with a
message naming both builds; matching builds carry on. Uses two copies of the launcher with different version.txt.
Usage: python tools/build_check_test.py   (after building the launcher into launcher/bin/Check)"""
import os
import shutil
import signal
import sys
import tempfile
from pathlib import Path

from save_sync_test import EXIT_TIMEOUT_S, ROOT, TRANSFER_TIMEOUT_S, launcher, wait_for

BUILD_DIR = ROOT / "launcher" / "bin" / "Check"
PORTS = (27985, 27986)
BRIDGE_PORTS = (27994, 27995)


def copy_with_build(name, build):
    target = ROOT / "launcher" / "bin" / name
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(BUILD_DIR, target)
    (target / "version.txt").write_text(str(build))
    return target / "coop-launcher.exe"


def run(host_exe, guest_exe, work):
    source, host_game, guest_game = work / "source", work / "host_game", work / "guest_game"
    for d in (source, host_game, guest_game):
        d.mkdir(parents=True)
    (source / "data0.bin").write_bytes(os.urandom(1000))
    common = ["--transport", "local", "--no-launch"]
    host = launcher(["host", "re0", *common, "--local-port", str(PORTS[0]), "--peer-port", str(PORTS[1]),
                     "--bridge-port", str(BRIDGE_PORTS[0]), "--save-source", str(source), "--game-dir", str(host_game)],
                    work / "host.log", host_exe)
    guest = launcher(["join", "0", "--game", "re0", *common, "--local-port", str(PORTS[1]), "--peer-port", str(PORTS[0]),
                      "--bridge-port", str(BRIDGE_PORTS[1]), "--game-dir", str(guest_game)], work / "guest.log", guest_exe)
    guest_log = work / "guest.log"
    wait_for(lambda: "Build mismatch" in guest_log.read_text() or "Set coop=1" in guest_log.read_text(), TRANSFER_TIMEOUT_S)
    for proc in (guest, host):
        if proc.poll() is None:
            proc.send_signal(signal.CTRL_BREAK_EVENT)
    for proc in (guest, host):
        try:
            proc.wait(EXIT_TIMEOUT_S)
        except Exception:
            proc.kill()
    return guest_log.read_text()


def main():
    failures = []
    old, new = copy_with_build("CheckOld", 5), copy_with_build("CheckNew", 6)
    mismatch = run(old, new, Path(tempfile.mkdtemp(prefix="cf_build_mismatch_")))
    if "Build mismatch: host is on build 5, you are on build 6" not in mismatch:
        failures.append("mismatch not reported")
    if "Set coop=1" in mismatch:
        failures.append("game would have started on a mismatch")
    same = run(new, copy_with_build("CheckNew2", 6), Path(tempfile.mkdtemp(prefix="cf_build_match_")))
    if "Build mismatch" in same or "Set coop=1" not in same:
        failures.append("matching builds did not carry on")
    for name in ("CheckOld", "CheckNew", "CheckNew2"):
        shutil.rmtree(ROOT / "launcher" / "bin" / name, ignore_errors=True)
    print("FAIL: " + "; ".join(failures) if failures else "build_check_test OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
