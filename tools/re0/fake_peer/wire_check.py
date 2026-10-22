"""Compares wire.py with the adapter headers: struct sizes, message ids, limits, enemy vtables, and the log anchors the
gates rely on. Reads sources only; every function returns a list of problems (empty = consistent)."""
import re
from pathlib import Path

from fake_peer import wire

REPO = Path(__file__).resolve().parents[3]
ADAPTER_SRC = REPO / "adapters" / "re0" / "src"
SOURCE_DIRS = (ADAPTER_SRC, REPO / "adapters" / "ds2" / "src")

SIZE_ASSERT = r"static_assert\(sizeof\({name}\) == (\d+)\)"
ID_CONSTANT = r"constexpr uint16_t {name} = (?:proto::)?kFirstGameType(?: \+ (0x[0-9a-fA-F]+|\d+))?;"
VTABLE_LIST = r"{name} = \{{([^}}]*)\}}"
JOIN_SNAPSHOT_ASSERT = "static_assert(sizeof(JoinSnapshot) == 2 * 56 + 128 + flag_diff::kWords * 4);"

# (header, regex with one numeric group, value in wire.py)
LIMITS = (
    ("scene.h", r"kSceneIdLimit = (0x[0-9a-f]+|\d+);", wire.SCENE_LIMIT),
    ("flag_diff.h", r"kWords = (0x[0-9a-f]+|\d+);", wire.FLAG_WORDS),
    ("enemy_observer_wire.h", r"kMaxEntries = (\d+);", wire.MAX_ENEMY_ENTRIES),
    ("weapon_fx_wire.h", r"kMaxEntries = (\d+);", wire.MAX_WEAPON_FX_ENTRIES),
    ("boss_wire.h", r"kMaxMembers = (\d+);", wire.MAX_BOSS_MEMBERS),
    ("room_move_wire.h", r"kAutoSpot = (0x[0-9a-f]+|\d+);", wire.AUTO_SPOT),
    ("prop_wire.h", r"kObstacleKind = (0x[0-9a-f]+|\d+);", wire.PROP_OBSTACLE_KIND),
)


def source_text(header):
    for directory in SOURCE_DIRS:
        path = directory / header
        if path.exists():
            return path.read_text(encoding="utf-8")
    return None


def check_sizes():
    problems = []
    for layout in wire.LAYOUTS:
        if layout.header is None:
            continue
        text = source_text(layout.header)
        found = re.search(SIZE_ASSERT.format(name=layout.name), text) if text else None
        if not found:
            problems.append(f"{layout.header}: no static_assert for {layout.name}")
        elif int(found.group(1)) != layout.size:
            problems.append(f"{layout.header}: {layout.name} is {found.group(1)} bytes, wire.py has {layout.size}")
    join_text = source_text("join_sync.h") or ""
    if JOIN_SNAPSHOT_ASSERT not in join_text:
        problems.append("join_sync.h: JoinSnapshot size expression changed")
    return problems


def check_ids():
    problems = []
    for message, (header, constant) in wire.CPP_IDS.items():
        text = source_text(header)
        found = re.search(ID_CONSTANT.format(name=constant), text) if text else None
        if not found:
            problems.append(f"{header}: no {constant}")
            continue
        value = wire.FIRST_GAME_TYPE + int(found.group(1) or "0", 0)
        if value != message.value:
            problems.append(f"{header}: {constant} is 0x{value:04x}, wire.py has {message.name} 0x{message.value:04x}")
    return problems


def check_limits():
    problems = []
    for header, pattern, expected in LIMITS:
        found = re.search(pattern, source_text(header) or "")
        if not found or int(found.group(1), 0) != expected:
            problems.append(f"{header}: limit {pattern} is not {expected}")
    return problems


def vtables(name):
    """The addresses listed in game.h under `name` (kEnemyVtables, kBossVtables)."""
    found = re.search(VTABLE_LIST.format(name=name), source_text("game.h") or "")
    if not found:
        raise ValueError(f"game.h: no {name}")
    return [int(item, 16) for item in re.findall(r"0x[0-9a-fA-F]+", found.group(1))]


def check_all():
    return check_sizes() + check_ids() + check_limits()


LAUNCHER_DIR = REPO / "launcher"
LAUNCHER_SOURCES = ("host_launcher", "join_launcher")
LINE_BREAK = "\n"


def log_sources(source):
    """All text a log line of `source` can come from: the adapter's .cpp files or the launcher's .cs files."""
    if source in LAUNCHER_SOURCES:
        files = LAUNCHER_DIR.glob("*.cs")
    else:
        files = ADAPTER_SRC.glob("*.cpp")
    return LINE_BREAK.join(path.read_text(encoding="utf-8", errors="replace") for path in files)


def check_anchors(anchors):
    """(source, anchor) pairs whose literal text no source file of that log contains."""
    texts, problems = {}, []
    for source, anchor in sorted(set(anchors)):
        if source not in texts:
            texts[source] = log_sources(source)
        if anchor not in texts[source]:
            problems.append(f"anchor not in the {source} sources: {anchor!r}")
    return problems
