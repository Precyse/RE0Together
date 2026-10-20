"""Guest folders for the echo rig (docs/SR2_NOTES.md "Echo testing"): hardlink clones of the SR2 game folder, so several real
instances of SR2_pc.exe can run on one PC, each with its own adapter files, saves and settings. The main folder is only read.

Usage: python tools/sr2/echo_clones.py [--game-dir DIR] [--root DIR] [--count 4] [--ini-override guest4:max_players=3] [--dry-run] [-v]
Re-run it whenever the main folder changed (a rebuilt dinput8.dll, new scripts): files of 1 MiB or more are hardlinks and
are relinked when the main file was replaced, smaller files are copied when their size or time differs."""
import argparse
import os
import re
import shutil
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

DEFAULT_GAME_DIR = r"G:\SteamLibrary\steamapps\common\Saints Row 2"
DEFAULT_ROOT = r"G:\SR2_echo"
DEFAULT_COUNT = 4
LINK_MIN_BYTES = 1 << 20
MTIME_TOLERANCE_S = 2.0
GUEST_NAME = "guest{}"
LOCAL_SAVES = Path(os.environ.get("LOCALAPPDATA", "")) / "THQ" / "Saints Row 2"

COOP_DIR = "coop"
USERDATA_DIR = "userdata"
SCRIPTS_DIR = "coop/scripts"
ADAPTER_INI = "coop/sr2_adapter.ini"
ADAPTER_DLL = "dinput8.dll"
JUICED_INI = "juiced.ini"
EMPTY_DIRS = ("coop", "juiced/logs", "juiced/crashdumps")
EXCLUDED_DIRS = frozenset(EMPTY_DIRS + (USERDATA_DIR,))
EXCLUDED_NAMES = frozenset(("debug.txt",))
EXCLUDED_SUFFIXES = (".log", ".bak", ".cfbak", ".cfbuild", ".cfown")
HANDLED_FILES = frozenset((ADAPTER_DLL, JUICED_INI))
JUICED_KEYS = (("Debug", "AllowMultipleSR2Windows", "1"), ("Misc", "portable", "1"), ("Misc", "RichPresence", "0"))
TRANSPORT_LINE = re.compile(r"^\s*transport\s*=\s*steam\s*$", re.IGNORECASE | re.MULTILINE)
ALLOW_MULTIPLE_LINE = re.compile(r"^\s*AllowMultipleSR2Windows\s*=\s*1\s*$", re.IGNORECASE | re.MULTILINE)
OVERRIDE = re.compile(r"^(guest\d+):([A-Za-z_]+)=(.*)$")

MKDIR, LINK, RELINK, COPY, DELETE, WRITE, SEED = "mkdir", "link", "relink", "copy", "delete", "write", "seed"


class CloneError(Exception):
    """A refusal the user can act on: a wrong volume, a main folder that is not set up for the steam transport."""


@dataclass(frozen=True)
class Action:
    kind: str
    dst: Path
    src: Path = None
    content: bytes = None

    def describe(self, clone):
        return f"{self.kind:7} {self.dst.relative_to(clone)}"


def relative_key(path, base):
    return path.relative_to(base).as_posix().lower()


def excluded_file(name):
    return name.lower() in EXCLUDED_NAMES or name.lower().endswith(EXCLUDED_SUFFIXES)


def same_file(a, b):
    return os.path.samestat(os.stat(a), os.stat(b))


def up_to_date_copy(src, dst):
    """A copy that has the main file's size and time; a hardlink to it does not count, a write would reach the main file."""
    if not dst.exists() or same_file(src, dst):
        return False
    a, b = src.stat(), dst.stat()
    return a.st_size == b.st_size and abs(a.st_mtime - b.st_mtime) < MTIME_TOLERANCE_S


def plan_file(src, dst):
    if src.stat().st_size >= LINK_MIN_BYTES:
        if not dst.exists():
            return [Action(LINK, dst, src)]
        return [] if same_file(src, dst) else [Action(RELINK, dst, src)]
    return [] if up_to_date_copy(src, dst) else [Action(COPY, dst, src)]


def plan_mirror(main, clone):
    """Directories, links and copies for everything the clone shares with the main folder."""
    actions = []
    for folder, dirs, files in os.walk(main):
        base = Path(folder)
        dirs[:] = [d for d in dirs if relative_key(base / d, main) not in EXCLUDED_DIRS]
        target = clone / base.relative_to(main)
        if not target.exists():
            actions.append(Action(MKDIR, target))
        for name in files:
            if excluded_file(name) or relative_key(base / name, main) in HANDLED_FILES:
                continue
            actions += plan_file(base / name, target / name)
    actions += [Action(MKDIR, clone / d) for d in EMPTY_DIRS if not (clone / d).exists()]
    return actions


def plan_adapter(main, clone):
    """The adapter DLL and the scripts are copies from the main folder every run, so the version gate's content hash is equal
    everywhere; a clone file the main folder no longer has is deleted."""
    actions = []
    dll = main / ADAPTER_DLL
    if dll.exists() and not up_to_date_copy(dll, clone / ADAPTER_DLL):
        actions.append(Action(COPY, clone / ADAPTER_DLL, dll))
    scripts, clone_scripts = main / SCRIPTS_DIR, clone / SCRIPTS_DIR
    wanted = set()
    for folder, _dirs, files in os.walk(scripts):
        for name in files:
            src = Path(folder) / name
            dst = clone_scripts / src.relative_to(scripts)
            wanted.add(dst)
            if not up_to_date_copy(src, dst):
                actions += [Action(MKDIR, dst.parent), Action(COPY, dst, src)]
    if clone_scripts.exists():
        actions += [Action(DELETE, Path(folder) / name) for folder, _d, files in os.walk(clone_scripts) for name in files
                    if Path(folder) / name not in wanted]
    return actions


def set_ini_value(text, key, value, section=None):
    """The ini text with `key` set in place (its first line, any section), else added under `section`, else at the end."""
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    line_key = re.compile(rf"^(\s*{re.escape(key)}\s*=\s*)", re.IGNORECASE)
    for i, line in enumerate(lines):
        match = line_key.match(line)
        if match:
            lines[i] = f"{match.group(1)}{value}{eol}"
            return "".join(lines)
    if lines and not lines[-1].endswith(("\n", "\r")):
        lines[-1] += eol
    header = re.compile(rf"^\s*\[{re.escape(section or '')}\]", re.IGNORECASE)
    at = next((i + 1 for i, line in enumerate(lines) if section and header.match(line)), None)
    if at is not None:
        lines.insert(at, f"{key}={value}{eol}")
    else:
        lines += ([f"[{section}]{eol}"] if section else []) + [f"{key}={value}{eol}"]
    return "".join(lines)


def plan_ini(main, clone, overrides):
    """juiced.ini with the three keys the clone needs, and the adapter ini with the clone's overrides, each only when it changed."""
    actions = []
    juiced = (main / JUICED_INI)
    if juiced.exists():
        text = juiced.read_bytes().decode("latin-1")
        for section, key, value in JUICED_KEYS:
            text = set_ini_value(text, key, value, section)
        actions += changed_write(clone / JUICED_INI, text)
    ini = main / ADAPTER_INI
    if ini.exists():
        text = ini.read_bytes().decode("latin-1")
        for key, value in overrides.items():
            text = set_ini_value(text, key, value)
        actions += changed_write(clone / ADAPTER_INI, text)
    return actions


def changed_write(dst, text):
    data = text.encode("latin-1")
    return [] if dst.exists() and dst.read_bytes() == data else [Action(WRITE, dst, content=data)]


def plan_userdata(clone, saves):
    """userdata is seeded once with the user's saves and settings; later runs never touch it."""
    if (clone / USERDATA_DIR).exists():
        return []
    return [Action(SEED, clone / USERDATA_DIR, saves) if saves.is_dir() else Action(MKDIR, clone / USERDATA_DIR)]


def plan_clone(main, clone, overrides=None, saves=LOCAL_SAVES):
    actions = plan_mirror(main, clone) + plan_adapter(main, clone) + plan_ini(main, clone, overrides or {}) + plan_userdata(clone, saves)
    return list(dict.fromkeys(actions))


def apply_action(clone, action):
    """Does one action. Every destination is inside the clone, never in the main folder."""
    if clone not in action.dst.parents and action.dst != clone:
        raise CloneError(f"{action.dst} is outside {clone}")
    dst = action.dst
    if action.kind == MKDIR:
        dst.mkdir(parents=True, exist_ok=True)
    elif action.kind == SEED:
        shutil.copytree(action.src, dst)
    elif action.kind == DELETE:
        dst.unlink()
    elif action.kind == WRITE:
        dst.parent.mkdir(parents=True, exist_ok=True)
        temp = dst.with_name(dst.name + ".tmp")
        temp.write_bytes(action.content)
        os.replace(temp, dst)
    else:
        if dst.exists():
            dst.unlink()
        if action.kind == COPY:
            shutil.copy2(action.src, dst)
        else:
            os.link(action.src, dst)


def run_actions(clone, actions):
    """Applies the actions in order; an error (a file a running game holds) is reported and the rest carries on."""
    errors = []
    for action in actions:
        try:
            apply_action(clone, action)
        except OSError as error:
            errors.append(f"{action.describe(clone)}: {error}")
    return errors


def nearest_existing(path):
    path = path.resolve()
    while not path.exists():
        path = path.parent
    return path


def same_volume(a, b):
    return os.stat(nearest_existing(a)).st_dev == os.stat(nearest_existing(b)).st_dev


def check_setup(main, root):
    """Refusals: a root on another volume (hardlinks need one), a root that contains the main folder or lies inside it, a main
    adapter ini without transport=steam. Returns warnings."""
    if not main.is_dir():
        raise CloneError(f"{main} is not a folder")
    if not same_volume(root, main):
        raise CloneError(f"{root} is on another volume than {main}, hardlinks need the same one")
    if main.resolve() in root.resolve().parents or root.resolve() in main.resolve().parents or root.resolve() == main.resolve():
        raise CloneError(f"{root} and {main} must not contain each other")
    ini = main / ADAPTER_INI
    if not ini.exists() or not TRANSPORT_LINE.search(ini.read_text(errors="replace")):
        raise CloneError(f"{ini} must say transport=steam")
    juiced = main / JUICED_INI
    if not juiced.exists() or not ALLOW_MULTIPLE_LINE.search(juiced.read_text(errors="replace")):
        return [f"{juiced} does not have AllowMultipleSR2Windows=1; the host is not changed, a second instance needs it"]
    return []


def parse_overrides(specs, names):
    """['guest4:max_players=3'] to {'guest4': {'max_players': '3'}}."""
    overrides = {}
    for spec in specs:
        match = OVERRIDE.match(spec)
        if not match or match.group(1) not in names:
            raise CloneError(f"--ini-override {spec!r}: expected <{'|'.join(names)}>:<key>=<value>")
        overrides.setdefault(match.group(1), {})[match.group(2)] = match.group(3)
    return overrides


def sync(main, root, count, overrides_specs=(), dry_run=False, verbose=False, saves=LOCAL_SAVES, out=print):
    """Creates or refreshes guest2..guest<count> under root; returns the folder of each guest instance, and the errors."""
    main, root = Path(main), Path(root)
    for warning in check_setup(main, root):
        out(f"warning: {warning}")
    names = [GUEST_NAME.format(k) for k in range(2, count + 1)]
    overrides = parse_overrides(overrides_specs, names)
    folders, errors = {}, []
    for k, name in zip(range(2, count + 1), names):
        clone = root / name
        folders[k] = clone
        actions = plan_clone(main, clone, overrides.get(name), saves)
        counts = Counter(action.kind for action in actions)
        out(f"{name}: " + (", ".join(f"{n} {kind}" for kind, n in sorted(counts.items())) or "up to date"))
        for action in actions:
            if verbose or dry_run and action.kind in (RELINK, WRITE, SEED, DELETE):
                out(f"  {action.describe(clone)}")
        if not dry_run:
            errors += run_actions(clone, actions)
    return folders, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help="instances, the host included")
    parser.add_argument("--ini-override", action="append", default=[], metavar="guestN:key=value")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    try:
        _folders, errors = sync(args.game_dir, args.root, args.count, args.ini_override, args.dry_run, args.verbose)
    except CloneError as error:
        print(f"FAIL: {error}")
        return 1
    for error in errors:
        print(f"error: {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
