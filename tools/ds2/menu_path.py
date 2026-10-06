"""Drives the System menu with the game's own keys and shoots after every step, so the Load path to a chosen save can be
read from the screenshots (needs the game lock; keys go to the game window only).

usage: python menu_path.py OUT_DIR STEP[,STEP...]
STEP is a gamectl key name (esc, enter, up, down, left, right, f, c ...). Example: open the menu and go to Load:
       python menu_path.py out esc,down,down,enter
A shot out/step_<n>_<key>.png is taken after each key."""
import subprocess
import sys
import time
from pathlib import Path

GAMECTL = Path(__file__).with_name("gamectl.py")
KEY_HOLD_MS = 150
SETTLE_S = 1.2


def gamectl(*args):
    subprocess.run([sys.executable, str(GAMECTL), *args], check=True, stdout=subprocess.DEVNULL)


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for index, key in enumerate(sys.argv[2].split(",")):
        gamectl("key", key, str(KEY_HOLD_MS))
        time.sleep(SETTLE_S)
        gamectl("shot", str(out / f"step_{index}_{key}.png"))


if __name__ == "__main__":
    main()
