"""DS2 save sync end to end on local transport: the host announces and sends every *.dat of its save folder, the
guest ends with identical files in coop/session/Documents/DEATH STRANDING 2 - ON THE BEACH/<steamid64>/ (the folder
the adapter's session-save redirect makes the game use), files that do not match the pattern stay behind, and both
a save the host rewrites after the join reaches the guest's staging folder (or the session folder when no DS2 runs), and both
sides reset after Ctrl+Break. Works on temporary folders only; never reads or writes real saves.

Usage: python tools/ds2/save_sync_test.py   (COOP_LAUNCHER overrides the launcher exe)
Note: like every launcher start, it resets the adapter settings of every installed profile with save sync.
"""
import hashlib
import os
import signal
import subprocess
import sys
import tempfile
import time
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
LAUNCHER = Path(os.environ.get("COOP_LAUNCHER", ROOT / "launcher" / "bin" / "Release" / "net8.0-windows" / "coop-launcher.exe"))
SAVE_FILES = {"profile.dat": 8_324, "autosave0.dat": 1_100_000, "manualsave3.dat": 900_000}
OTHER_FILE = "bindings.cfg"
STEAM_ID64_BASE = 76561197960265728
GAME_FOLDER = "DEATH STRANDING 2 - ON THE BEACH"
PORTS = (27983, 27984)
TRANSFER_TIMEOUT_S = 30
EXIT_TIMEOUT_S = 15
LATER_SAVE_TIMEOUT_S = 30  # settle delay 3 s plus the transfer
POLL_S = 0.2


def steam_id64():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
        return STEAM_ID64_BASE + winreg.QueryValueEx(key, "ActiveUser")[0]


def launcher(args, log_path):
    log = open(log_path, "w")
    return subprocess.Popen([str(LAUNCHER)] + args, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def wait_for(condition, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(POLL_S)
    return False


def later_save_failures(source, guest_game, received_dir):
    """The host writes a save after the join: it reaches the guest's staging folder, and the session folder only once no
    DS2 process runs (so either place may hold it, depending on whether the real game is running on this machine)."""
    name = next(iter(SAVE_FILES))
    (source / name).write_bytes(os.urandom(SAVE_FILES[name]))
    staged = guest_game / "coop" / "session" / "staging" / name
    arrived = lambda: any(p.exists() and sha(p) == sha(source / name) for p in (staged, received_dir / name))
    if not wait_for(arrived, LATER_SAVE_TIMEOUT_S):
        return [f"the save rewritten after the join never reached the guest ({staged})"]
    return []


def main():
    work = Path(tempfile.mkdtemp(prefix="cf_ds2_save_sync_"))
    source, host_game, guest_game = work / "source", work / "host_game", work / "guest_game"
    for d in (source, host_game, guest_game):
        d.mkdir(parents=True)
    for name, size in SAVE_FILES.items():
        (source / name).write_bytes(os.urandom(size))
    (source / OTHER_FILE).write_text("personal key bindings")
    received_dir = guest_game / "coop" / "session" / "Documents" / GAME_FOLDER / str(steam_id64())

    common = ["--transport", "local", "--no-launch"]
    host = launcher(["host", "ds2", *common, "--local-port", str(PORTS[0]), "--peer-port", str(PORTS[1]),
                     "--bridge-port", "27991", "--save-source", str(source), "--game-dir", str(host_game)], work / "host.log")
    guest = launcher(["join", "0", "--game", "ds2", *common, "--local-port", str(PORTS[1]), "--peer-port", str(PORTS[0]),
                      "--bridge-port", "27992", "--game-dir", str(guest_game)], work / "guest.log")
    failures = []
    try:
        if not wait_for(lambda: all((received_dir / n).exists() for n in SAVE_FILES), TRANSFER_TIMEOUT_S):
            failures.append(f"guest never got every save in {received_dir}")
        else:
            failures += [f"{n} differs" for n in SAVE_FILES if sha(received_dir / n) != sha(source / n)]
            failures += later_save_failures(source, guest_game, received_dir)
        if (received_dir / OTHER_FILE).exists():
            failures.append(f"{OTHER_FILE} was sent although it does not match the pattern")
    finally:
        for proc in (guest, host):
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        for proc in (guest, host):
            try:
                proc.wait(EXIT_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                proc.kill()
                failures.append("launcher did not exit on Ctrl+Break")
    if (guest_game / "coop" / "session").exists():
        failures.append("guest session dir not removed")

    print("FAIL: " + "; ".join(failures) if failures else "ds2 save_sync_test OK")
    print(f"logs in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
