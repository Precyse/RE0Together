"""Drive the Death Stranding 2 window for testing, sharing the PC with other game agents.

Reuses tools/re0/gamectl.py (keys only while the game is in front, screenshots of the game window only) with the
DS2 window title, and adds the shared window lock and the idle rule around launching.

usage: python gamectl.py idle                 # seconds since the user last touched mouse or keyboard
       python gamectl.py launch [--no-idle-check]  # take the lock (lock free; user idle >= 90 s unless the user
                                                   # lifted that rule), start DS2 via Steam
       python gamectl.py close                # ask the game window to close (WM_CLOSE), as the window's X would
       python gamectl.py release              # drop the lock (after the game has stopped)
       python gamectl.py shot [out.png]
       python gamectl.py key <name> [hold_ms] [repeat]
       python gamectl.py keys <name,name,...>
       python gamectl.py mouse <dx> <dy> [steps]  # relative mouse motion (camera), only while the game is in front
"""
import ctypes
import os
import subprocess
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "re0"))
import gamectl as shared  # noqa: E402

LOCK = Path(os.path.expanduser("~")) / ".claude" / "game-window.lock"
OWNER = "ds"
WM_CLOSE = 0x0010
INPUT_MOUSE = 0
MOUSEEVENTF_MOVE = 0x0001
MOUSE_STEP_MS = 10
DEFAULT_MOUSE_STEPS = 20
MIN_IDLE_S = 90
STEAM_APP_ID = 3280350
WINDOW_TITLE_PART = "DEATH STRANDING 2"
DEFAULT_SHOT = str(Path(__file__).resolve().parent / "out" / "shot.png")

shared.WINDOW_TITLE_PART = WINDOW_TITLE_PART
shared.DEFAULT_SHOT = DEFAULT_SHOT


def lock_owner():
    return LOCK.read_text().strip() if LOCK.exists() else None


def acquire(check_idle):
    owner = lock_owner()
    if owner not in (None, OWNER):
        sys.exit(f"game window lock held by {owner!r}; not launching")
    idle = shared.idle_seconds()
    if check_idle and idle < MIN_IDLE_S:
        sys.exit(f"user active {idle:.0f} s ago (< {MIN_IDLE_S} s); not launching")
    LOCK.write_text(OWNER)


def release():
    if lock_owner() == OWNER:
        LOCK.unlink()


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_long), ("dy", ctypes.c_long), ("mouseData", ctypes.c_ulong), ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong), ("dwExtraInfo", ctypes.c_size_t)]


def mouse(dx, dy, steps):
    """Relative motion split into small steps (games read raw deltas per frame)."""
    hwnd = shared.find_window()
    shared.focus(hwnd)
    for _ in range(steps):
        shared.ensure_foreground(hwnd)
        inp = shared.INPUT(type=INPUT_MOUSE)
        ctypes.memmove(ctypes.addressof(inp) + shared.INPUT.u.offset,
                       ctypes.byref(MOUSEINPUT(dx // steps, dy // steps, 0, MOUSEEVENTF_MOVE, 0, 0)),
                       ctypes.sizeof(MOUSEINPUT))
        shared.user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(shared.INPUT))
        time.sleep(MOUSE_STEP_MS / 1000)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "launch":
        acquire("--no-idle-check" not in sys.argv)
        subprocess.run(["cmd", "/c", "start", "", f"steam://rungameid/{STEAM_APP_ID}"], check=True)
        print("launched; lock held by", OWNER)
    elif cmd == "close":
        shared.user32.PostMessageW(shared.find_window(), WM_CLOSE, 0, 0)
    elif cmd == "mouse":
        if lock_owner() != OWNER:
            sys.exit("take the lock first (launch)")
        mouse(int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else DEFAULT_MOUSE_STEPS)
    elif cmd == "release":
        release()
    elif cmd in ("shot", "key", "keys", "idle"):
        if cmd != "idle" and lock_owner() != OWNER:
            sys.exit("take the lock first (launch)")
        Path(DEFAULT_SHOT).parent.mkdir(exist_ok=True)
        if cmd == "shot" and len(sys.argv) < 3:
            sys.argv.append(DEFAULT_SHOT)
        shared.main()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
