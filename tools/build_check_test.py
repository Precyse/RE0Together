"""Mismatched builds refuse to play: a guest on another launcher build or another game build (Steam buildid, read from
the appmanifest beside the game folder) than the host stops before the game starts, with a message naming both builds;
matching builds carry on. Uses two copies of the launcher with different version.txt and fake Steam libraries.
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
RE0_APP_ID = 339340
COPY_NAMES = ("CheckOld", "CheckNew", "CheckNew2", "CheckNew3")


def copy_with_build(name, build):
    target = ROOT / "launcher" / "bin" / name
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(BUILD_DIR, target)
    (target / "version.txt").write_text(str(build))
    return target / "coop-launcher.exe"


def fake_game(library, game_build):
    """A game folder at <library>/steamapps/common/game with the appmanifest Steam keeps beside it."""
    game = library / "steamapps" / "common" / "game"
    game.mkdir(parents=True)
    manifest = library / "steamapps" / f"appmanifest_{RE0_APP_ID}.acf"
    manifest.write_text(f'"AppState"\n{{\n\t"buildid"\t\t"{game_build}"\n}}\n')
    return game


def run(host_exe, guest_exe, work, host_game_build, guest_game_build):
    source = work / "source"
    source.mkdir(parents=True)
    (source / "data0.bin").write_bytes(os.urandom(1000))
    host_game = fake_game(work / "host_library", host_game_build)
    guest_game = fake_game(work / "guest_library", guest_game_build)
    common = ["--transport", "local", "--no-launch"]
    host = launcher(["host", "re0", *common, "--local-port", str(PORTS[0]), "--peer-port", str(PORTS[1]),
                     "--bridge-port", str(BRIDGE_PORTS[0]), "--save-source", str(source), "--game-dir", str(host_game)],
                    work / "host.log", host_exe)
    guest = launcher(["join", "0", "--game", "re0", *common, "--local-port", str(PORTS[1]), "--peer-port", str(PORTS[0]),
                      "--bridge-port", str(BRIDGE_PORTS[1]), "--game-dir", str(guest_game)], work / "guest.log", guest_exe)
    guest_log = work / "guest.log"
    wait_for(lambda: "mismatch" in guest_log.read_text() or "Set coop=1" in guest_log.read_text(), TRANSFER_TIMEOUT_S)
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
    mismatch = run(old, new, Path(tempfile.mkdtemp(prefix="cf_build_mismatch_")), 100, 100)
    if "Build mismatch: host is on build 5, you are on build 6" not in mismatch:
        failures.append("launcher build mismatch not reported")
    if "Set coop=1" in mismatch:
        failures.append("game would have started on a launcher build mismatch")
    same = run(new, copy_with_build("CheckNew2", 6), Path(tempfile.mkdtemp(prefix="cf_build_match_")), 100, 100)
    if "mismatch" in same or "Set coop=1" not in same:
        failures.append("matching builds did not carry on")
    game = run(new, copy_with_build("CheckNew3", 6), Path(tempfile.mkdtemp(prefix="cf_game_build_")), 100, 200)
    if "Game build mismatch: host has Resident Evil 0 HD build 100, you have build 200" not in game:
        failures.append("game build mismatch not reported")
    if "Set coop=1" in game:
        failures.append("game would have started on a game build mismatch")
    for name in COPY_NAMES:
        shutil.rmtree(ROOT / "launcher" / "bin" / name, ignore_errors=True)
    print("FAIL: " + "; ".join(failures) if failures else "build_check_test OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
