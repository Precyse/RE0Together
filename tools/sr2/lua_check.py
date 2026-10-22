"""Checks Lua scripts for the Saints Row 2 interpreter (Lua 5.0, base library only): they must parse, and the code the mod adds must stay 5.0.

usage: python tools/sr2/lua_check.py <dir of generated scripts> [<dir of the original scripts>]

Parsing uses the Lua 5.1 parser of the `lupa` package (pip install lupa); 5.1 accepts everything 5.0 does except what the
style check below rejects. With the second dir, only the lines the patches added or changed are style-checked.
"""
import difflib
import re
import sys
from pathlib import Path

STRIP = re.compile(r"--\[\[.*?\]\]|--[^\n]*|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)
# What SR2 Lua lacks: the length operator, `...` in a function body, select, gmatch and the table, string, math, os, io
# and debug libraries (the game registers the base library only)
NOT_IN_LUA_50 = [
    (re.compile(r"(?<![\w\)\]\"'])#\s*[\w(]|[\w\)\]]\s*#\w"), "length operator #"),
    (re.compile(r"\bselect\s*\("), "select"),
    (re.compile(r":gmatch\b"), "gmatch"),
    (re.compile(r"(?<![\w.])(?:table|string|math|os|io|debug)\s*\.\s*[A-Za-z_]"), "library not in SR2 Lua"),
    (re.compile(r"\.\.\.(?!\s*\))(?!\s*,)"), "... outside a parameter list"),
]


def lua_runtime():
    import lupa.lua51
    return lupa.lua51.LuaRuntime()


def parse_error(runtime, text):
    """The parser's message for the script text, or None when it parses."""
    return runtime.eval("function(s) local f, e = loadstring(s) return e end")(text)


def style_problems(text):
    """(description, line number) for each 5.1-only construct in the code of text (strings and comments ignored)."""
    code = STRIP.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)
    problems = []
    for number, line in enumerate(code.split("\n"), 1):
        for pattern, description in NOT_IN_LUA_50:
            if pattern.search(line):
                problems.append((description, number))
    return problems


def added_lines(original, changed):
    """The lines of changed that are not in original, as text (line numbers do not matter to the style check)."""
    matcher = difflib.SequenceMatcher(None, original.split("\n"), changed.split("\n"), autojunk=False)
    lines = changed.split("\n")
    return "\n".join("\n".join(lines[j1:j2]) for tag, i1, i2, j1, j2 in matcher.get_opcodes() if tag in ("replace", "insert"))


def read(path):
    return path.read_bytes().decode("latin-1").replace("\r\n", "\n")


def check_dir(generated, originals=None):
    """The list of problems found in every script of the dir generated."""
    runtime = lua_runtime()
    found = []
    for path in sorted(generated.glob("*.lua")):
        text = read(path)
        error = parse_error(runtime, text)
        if error:
            found.append(f"{path.name}: {error}")
        original = originals / path.name if originals else None
        added = added_lines(read(original), text) if original and original.is_file() else text
        found.extend(f"{path.name}: {what} (added line {n})" for what, n in style_problems(added))
    return found


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    problems = check_dir(Path(argv[1]), Path(argv[2]) if len(argv) == 3 else None)
    print("\n".join(problems) or f"lua_check: {len(list(Path(argv[1]).glob('*.lua')))} scripts parse, no Lua 5.1-only code added")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
