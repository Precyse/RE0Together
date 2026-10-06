"""One gear-only change and the autosave it triggers, for the save-format diff (docs/DS2_NOTES.md "Save chunk map").
Needs the game lock (tools/ds2/gamectl.py rules), Sam standing at a terminal prompt, adapter.ini test_commands=1.

usage: python gear_pair.py [WEAPON_ID]   (default 39, an id from `addweapon.txt` = -1)
Copies the session saves to G:/coop-scratch/ds2/savefmt/pair_<time>/before, gives Sam the weapon as cargo
(test command addweapon.txt), presses F at the terminal (opening a terminal autosaves), copies the saves again to
.../after, and prints which files changed. Never touches the real saves: the adapter redirects them to coop/session."""
import shutil
import subprocess
import sys
import time
from pathlib import Path

GAMECTL = Path(__file__).with_name("gamectl.py")
GAME_DIR = Path(r"G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH")
SESSION_SAVES = GAME_DIR / "coop" / "session" / "Documents" / "DEATH STRANDING 2 - ON THE BEACH"
OUT_ROOT = Path(r"G:\coop-scratch\ds2\savefmt")
DEFAULT_WEAPON = "39"
COMMAND_SETTLE_S = 4
TERMINAL_HOLD_MS = 400
AUTOSAVE_WAIT_S = 8


def copy_saves(target):
    target.mkdir(parents=True)
    for save in SESSION_SAVES.glob("*/*.dat"):
        shutil.copy2(save, target / save.name)


def main():
    weapon = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_WEAPON
    out = OUT_ROOT / time.strftime("pair_%H%M%S")
    copy_saves(out / "before")
    (GAME_DIR / "coop" / "addweapon.txt").write_text(weapon)
    time.sleep(COMMAND_SETTLE_S)
    subprocess.run([sys.executable, str(GAMECTL), "key", "f", str(TERMINAL_HOLD_MS)], check=True)
    time.sleep(AUTOSAVE_WAIT_S)
    copy_saves(out / "after")
    changed = [p.name for p in (out / "after").iterdir()
               if not (out / "before" / p.name).exists() or (out / "before" / p.name).read_bytes() != p.read_bytes()]
    print(f"{out}: changed saves {changed or 'none (the terminal did not open)'}")


if __name__ == "__main__":
    main()
