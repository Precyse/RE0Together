r"""Builds the Saints Row 2 Lua overrides for 3 or more players from the game's own scripts.

usage: python tools/sr2/lua_overrides.py <dir with the original .lua files> <dir with Juiced's TU3 Mission Fixes> <output dir>

The adapter (script_override, AF9) loads a script from <game>\coop\scripts when a file of that name is there. The
scripts are the game's, so they are never kept in the repo: this tool applies the patches of tools/sr2/lua_patches to copies
that were extracted from the game's archives (read-only) and writes the results to the output dir, normally the build's
coop\scripts folder. em01.lua and rn07.lua start from Juiced's fixed copies (second dir), every other script from the first.
Every edit must find its text exactly the expected number of times, else nothing is written.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lua_params import rewrite as apply_parameter_rule, wrapped_names  # noqa: E402
from lua_patch import apply_block_edits, apply_each, apply_edits, apply_line_edits, apply_rules  # noqa: E402
from lua_patches import GENERIC_RULES, PATCHES  # noqa: E402

LUA_DIR = Path(__file__).with_name("lua")
LIBRARY_FILE = "ug_lib.lua"
LIBRARY_PARTS = (
    "coop_helpers.lua", "coop_missions.lua", "coop_outfit_sync.lua", "coop_slots.lua", "coop_wrappers.lua", "coop_seats.lua", "coop_rules.lua",
    "coop_waypoints.lua",
)
WRAPPED = wrapped_names((LUA_DIR / "coop_rules.lua").read_text(encoding="utf-8"))
FIXED_FILES = ("em01.lua", "rn07.lua")


def read(path):
    """The game's scripts are not valid UTF-8 (Windows-1252 quotes); latin-1 keeps every byte as it is."""
    return path.read_bytes().decode("latin-1")


def edited_body(name, text, patch):
    """The text of script `name` (line feeds only) with its patch and the generic rules applied."""
    body = apply_line_edits(name, text.replace("\r\n", "\n"), patch.lines)
    body = apply_block_edits(name, body, patch.blocks)
    body = apply_edits(name, body, patch.edits)
    body = apply_each(name, body, patch.each)
    if patch.generic:
        body, _ = apply_rules(body, GENERIC_RULES)
    return body


def patched_text(name, text, patch):
    """(the text of script `name` with its patch applied, in the file's own line endings; the number of calls the parameter
    rule changed). The library gets the Lua library, every other script the parameter rule (lua_params.py)."""
    body = edited_body(name, text, patch)
    parameter_calls = 0
    if name == LIBRARY_FILE:
        body += "".join((LUA_DIR / part).read_text(encoding="utf-8") for part in LIBRARY_PARTS)
    else:
        body, parameter_calls = apply_parameter_rule(body, WRAPPED)
    return (body.replace("\n", "\r\n") if "\r\n" in text else body), parameter_calls


def build(source, fixes, out):
    """Writes every patched script to out; returns their names, sorted, and what the parameter rule changed as (calls,
    scripts). Nothing is written when a patch does not fit."""
    results = {}
    parameter_calls = []
    for name, patch in PATCHES.items():
        path = (fixes if name in FIXED_FILES else source) / name
        if not path.is_file():
            raise FileNotFoundError(path)
        results[name], changed = patched_text(name, read(path), patch)
        parameter_calls.append(changed)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in results.items():
        (out / name).write_bytes(text.encode("latin-1"))
    return sorted(results), (sum(parameter_calls), sum(1 for changed in parameter_calls if changed))


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    try:
        written, (calls, scripts) = build(Path(argv[1]), Path(argv[2]), Path(argv[3]))
    except (ValueError, FileNotFoundError) as error:
        print(f"lua_overrides: {error}")
        return 1
    print(f"lua_overrides: params: {calls} calls in {scripts} scripts")
    print(f"lua_overrides: wrote {len(written)} scripts to {argv[3]}: {', '.join(written)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
