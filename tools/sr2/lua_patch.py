"""The edit operations shared by the Lua override patches (tools/sr2/lua_patches) and the builder (lua_overrides.py)."""
import re
from dataclasses import dataclass, field


def apply_line_edits(name, body, edits):
    """body with every (line number in the original, find, replace) edit applied; find must occur once on that line."""
    lines = body.split("\n")
    for number, find, replace in edits:
        line = lines[number - 1]
        if line.count(find) != 1:
            raise ValueError(f"{name}:{number}: expected one {find!r} in {line.strip()!r}")
        lines[number - 1] = line.replace(find, replace)
    return "\n".join(lines)


def apply_block_edits(name, body, edits):
    """body with every (first line, last line, text expected in the first line, replacement lines) edit applied.
    The lines are numbered as in the original; the replacement is indented like the first line it replaces."""
    lines = body.split("\n")
    for first, last, expected, replacement in sorted(edits, reverse=True):
        head = lines[first - 1]
        if expected not in head:
            raise ValueError(f"{name}:{first}: expected {expected!r} in {head.strip()!r}")
        indent = head[: len(head) - len(head.lstrip())]
        lines[first - 1:last] = [indent + line for line in replacement.split("\n")]
    return "\n".join(lines)


def apply_edits(name, body, edits):
    """body with every (find, replace, expected count) edit applied; each must change exactly the expected count."""
    for find, replace, expected in edits:
        found = body.count(find)
        body = body.replace(find, replace)
        if found != expected:
            raise ValueError(f"{name}: expected {expected} of {find!r}, found {found}")
    return body


def apply_rules(body, rules):
    """(body with every (pattern, replacement) rule applied, number of places changed per rule)."""
    counts = []
    for pattern, replace in rules:
        body, found = re.subn(pattern, replace, body, flags=re.M)
        counts.append(found)
    return body, counts


def apply_each(name, body, functions):
    """body with every call of one of the functions that has REMOTE_PLAYER among its arguments made once per remote player
    (coop_each_remote(function, arguments)); each function must be called that way at least once."""
    for function in functions:
        call = rf"\b{re.escape(function)}\s*\(([^()]*\bREMOTE_PLAYER\b[^()]*)\)"
        body, found = re.subn(call, lambda match: f"coop_each_remote({function}, {match.group(1).strip()})", body)
        if found == 0:
            raise ValueError(f"{name}: no call of {function} with REMOTE_PLAYER")
    return body


@dataclass
class Patch:
    """What one script gets: lines (line number in the original, find, replace) and blocks (first line, last line,
    expected text, replacement; see apply_block_edits), both applied first, then edits (find, replace, expected count),
    each (functions whose calls with REMOTE_PLAYER are made once per remote player) and whether the generic rules of
    lua_patches/generic.py apply."""
    lines: list = field(default_factory=list)
    blocks: list = field(default_factory=list)
    edits: list = field(default_factory=list)
    each: tuple = ()
    generic: bool = True
