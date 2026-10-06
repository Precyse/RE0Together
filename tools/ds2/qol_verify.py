"""Live re-check of the ds2-qol features on the running game (needs the game lock): build guard line, partner health
label, edge arrow, death toast, F9 resync. Shots go to --out; read them afterwards.

usage: python qol_verify.py [--out DIR]
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).parent
GAME_DIR = Path(r"G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH")
LOG = GAME_DIR / "coop" / "adapter.log"
AHEAD_ALIVE = ("--offset", "25", "--status", "0x107f")   # 50 percent health, in front
BEHIND_DEAD = ("--offset", "-25", "--status", "0x1140")  # dead, 25 percent, behind Sam (arrow)
CONNECT_S = 6
TOAST_SHOTS_S = (1.5, 1.5, 1.5)
F9_WAIT_S = 1.5


def gamectl(*args):
    subprocess.run([sys.executable, str(HERE / "gamectl.py"), *args], check=True, stdout=subprocess.DEVNULL)


def fake_peer(*status_args):
    return subprocess.Popen([sys.executable, str(HERE / "fake_launcher.py"), "--follow", "--radius", "1.0", "--speed", "0",
                             *status_args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def log_lines(needle):
    return [l for l in LOG.read_text(errors="replace").splitlines() if needle in l]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "out"))
    out = Path(ap.parse_args().out)
    out.mkdir(parents=True, exist_ok=True)

    print("build guard:", (log_lines("build_guard:") or ["no line"])[-1])

    peer = fake_peer(*AHEAD_ALIVE)
    time.sleep(CONNECT_S)
    gamectl("shot", str(out / "qol_health.png"))
    peer.terminate()
    peer.wait()
    time.sleep(2)

    peer = fake_peer(*BEHIND_DEAD)
    time.sleep(CONNECT_S - 3)  # the peer is announced after about this long: shoot while the death toast shows
    for index, wait in enumerate(TOAST_SHOTS_S):
        gamectl("shot", str(out / f"qol_dead_{index}.png"))
        time.sleep(wait)
    before = len(log_lines("manual request (F9)"))
    gamectl("key", "f9", "150")
    time.sleep(F9_WAIT_S)
    peer.terminate()
    peer.wait()
    print("F9 resync logged:", len(log_lines("manual request (F9)")) > before)
    print("shots in", out, "(qol_health.png: label with 50%; qol_dead_*.png: arrow with 25% DEAD, death toast in the first)")


if __name__ == "__main__":
    main()
