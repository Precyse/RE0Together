"""Checks tools/sr2/lua_overrides.py, its patch engine and the Lua checker against made-up scripts (no game files).

usage: python tools/sr2/lua_overrides_test.py        (needs the `lupa` package for the parse checks)

With SR2_LUA_SOURCE (the extracted scripts) and SR2_LUA_FIXES (Juiced's TU3 Mission Fixes) set, it also builds every real
override into a temporary dir and runs the parse and style checks on them.
"""
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import lua_check  # noqa: E402
import lua_overrides  # noqa: E402
import lua_params  # noqa: E402
import lua_patch  # noqa: E402
from lua_patch import Patch  # noqa: E402
from lua_patches import GENERIC_RULES, PATCHES  # noqa: E402
from lua_patches.waypoints import per_player_waypoints  # noqa: E402

failures = 0
REAL_SCRIPT_COUNT = 50
# The calls of tools/sr2/lua/coop_waypoints.lua per script: the generic rules (10) and lua_patches/waypoints.py (25); ss01 also holds the M16 block's two
WAYPOINT_CALLS = {"bh08": 5, "ep01": 2, "ep02": 4, "rn01": 2, "rn02": 10, "rn03": 2, "tss02": 4, "ss01": 3, "ss06": 3, "ss10": 2}
# Decided to stay broadcast: tss04 keeps one remote tow, so its garage GPS belongs to every remote (docs/SR2_NOTES.md)
WAYPOINT_SYNC_CALL = re.compile(r"\b(mission_)?waypoint_(add|remove)\([^()]*\bsync(_type)?\s*\)")
TSS04_REMOTE_TOW_CALLS = [("tss04", "waypoint_add(TOWED_VEHICLE_TRIGGER, sync_type)"), ("tss04", "waypoint_remove( sync_type )"), ("tss04", "waypoint_remove( sync_type )")]


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def raises(function, *arguments):
    try:
        function(*arguments)
    except ValueError:
        return True
    return False


def test_engine():
    body = "a = 1\nif x == REMOTE_PLAYER then\n\tf(REMOTE_PLAYER, 2)\nend\nb = 1\n"
    check(lua_patch.apply_line_edits("t", body, [(3, "REMOTE_PLAYER", "p")]).split("\n")[2] == "\tf(p, 2)", "line edit changes only its line")
    check(raises(lua_patch.apply_line_edits, "t", body, [(1, "REMOTE_PLAYER", "p")]), "line edit whose text is not on the line is refused")
    check(raises(lua_patch.apply_line_edits, "t", body, [(2, "x", "y"), (2, "=", "~")]), "line edit whose text is on the line twice is refused")
    block = lua_patch.apply_block_edits("t", body, [(2, 4, "REMOTE_PLAYER", "g()\nh()")])
    check(block == "a = 1\ng()\nh()\nb = 1\n", "block edit replaces the lines, indented like the first")
    check(raises(lua_patch.apply_block_edits, "t", body, [(1, 2, "REMOTE_PLAYER", "g()")]), "block edit whose first line differs is refused")
    check(lua_patch.apply_edits("t", body, [("b = 1", "b = 2", 1)]).endswith("b = 2\n"), "literal edit")
    check(raises(lua_patch.apply_edits, "t", body, [("REMOTE_PLAYER", "p", 1)]), "an edit that changes a different count is refused")
    each = lua_patch.apply_each("t", "f(a, REMOTE_PLAYER)\nf(a, LOCAL_PLAYER)\n", ["f"])
    check(each == "coop_each_remote(f, a, REMOTE_PLAYER)\nf(a, LOCAL_PLAYER)\n", "each rewrites only the calls with REMOTE_PLAYER")
    check(raises(lua_patch.apply_each, "t", "f(a)\n", ["f"]), "each of a function never called with REMOTE_PLAYER is refused")
    waypoint_body = "a\nmission_waypoint_remove(sync)\nmission_waypoint_add(nav, sync)\n"
    waypoint_lines = lua_patch.apply_line_edits("t", waypoint_body, per_player_waypoints("who", (2,), (3,), "mission_waypoint"))
    check(waypoint_lines == "a\ncoop_waypoint_remove_for(who, mission_waypoint_remove, sync)\ncoop_waypoint_add_for(who, mission_waypoint_add, nav, sync)\n",
          "per_player_waypoints names the player on the remove and add lines")
    check(raises(lua_patch.apply_line_edits, "t", waypoint_body, per_player_waypoints("who", (1,))), "a waypoint edit on a line without the call is refused")


def test_generic_rules():
    def rewrite(text):
        return lua_patch.apply_rules(text, GENERIC_RULES)[0]

    check(rewrite("if who == REMOTE_PLAYER then") == "if coop_is_remote_player(who) then", "== REMOTE_PLAYER")
    check(rewrite("if who ~= REMOTE_PLAYER then") == "if not coop_is_remote_player(who) then", "~= REMOTE_PLAYER")
    check(rewrite("a.b == LOCAL_PLAYER or a.b == REMOTE_PLAYER") == "a.b == LOCAL_PLAYER or coop_is_remote_player(a.b)", "a field name")
    check(rewrite("T = { [LOCAL_PLAYER] = false, [REMOTE_PLAYER] = false }") == "T = coop_player_table(false, false)", "two-key table")
    check(rewrite("T = {\n [LOCAL_PLAYER] = REMOTE_PLAYER,\n [REMOTE_PLAYER] = LOCAL_PLAYER,\n}") == "T = coop_player_table(REMOTE_PLAYER, LOCAL_PLAYER)", "multi-line table")
    check(rewrite("T = {[LOCAL_PLAYER] = {}, [REMOTE_PLAYER] = {}}") == "T = coop_player_table({}, {})", "table of tables")
    check(rewrite("S = { [LOCAL_PLAYER] = true; [REMOTE_PLAYER] = true; }") == "S = coop_player_table(true, true)", "semicolons")
    check(rewrite("if A[LOCAL_PLAYER] and A[REMOTE_PLAYER] then") == "if coop_all_players_in(A) then", "both players")
    check(rewrite("if A[LOCAL_PLAYER] or\n A[REMOTE_PLAYER] then") == "if coop_any_player_in(A) then", "either player")
    check(rewrite("teleport(REMOTE_PLAYER, nav)") == "teleport(REMOTE_PLAYER, nav)", "a call is left to the wrappers")
    wear = "customization_item_wear(NAME,OPTION,VARIANT, PLAYER_SYNC[player])"
    check(rewrite(wear) == "customization_item_wear(NAME,OPTION,VARIANT, coop_sync_for(PLAYER_SYNC, player))", "item wear takes a targeted mask")
    slots = "inv_weapon_enable_or_disable_all_slots(false, PLAYER_SYNC[placer])"
    check(rewrite(slots) == "inv_weapon_enable_or_disable_all_slots(false, coop_sync_for(PLAYER_SYNC, placer))", "weapon slots take a targeted mask")
    check(rewrite("mission_help_table(a, PLAYER_SYNC[p])") == "mission_help_table(a, PLAYER_SYNC[p])", "a help text mask is left alone")
    check(rewrite("local sync = PLAYER_SYNC[p]") == "local sync = PLAYER_SYNC[p]", "a stored mask is left alone")
    check(rewrite("mission_waypoint_add(new_gps_bus, PLAYER_SYNC[player])") == "coop_waypoint_add_for(player, mission_waypoint_add, new_gps_bus, PLAYER_SYNC[player])", "a mission waypoint add is named to its player")
    check(rewrite("waypoint_add(waypoint,PLAYER_SYNC[player])") == "coop_waypoint_add_for(player, waypoint_add, waypoint, PLAYER_SYNC[player])", "a waypoint add without spaces")
    check(rewrite("mission_waypoint_remove(PLAYER_SYNC[player])") == "coop_waypoint_remove_for(player, mission_waypoint_remove, PLAYER_SYNC[player])", "a mission waypoint remove")
    check(rewrite("waypoint_remove( PLAYER_SYNC[player] )") == "coop_waypoint_remove_for(player, waypoint_remove, PLAYER_SYNC[player])", "a waypoint remove with spaces")
    check(rewrite("waypoint_add(nav, SYNC_REMOTE)") == "waypoint_add(nav, SYNC_REMOTE)", "a waypoint with a fixed mask is left alone")
    check(rewrite("waypoint_add(nav, sync)") == "waypoint_add(nav, sync)", "a waypoint with a stored mask is left alone")
    check(rewrite("waypoint_add(get_nav(a), PLAYER_SYNC[p])") == "waypoint_add(get_nav(a), PLAYER_SYNC[p])", "a waypoint whose target is a call is left alone")


def test_lua_check():
    check(lua_check.style_problems("local n = #list") != [], "the length operator is Lua 5.1 only")
    check(lua_check.style_problems("x = select(2, a)") != [], "select is Lua 5.1 only")
    check(lua_check.style_problems("n = table.getn(list)") != [], "table is not in SR2 Lua")
    check(lua_check.style_problems("s = string.format(a)") != [], "string is not in SR2 Lua")
    check(lua_check.style_problems("m = math.floor(a)") != [], "math is not in SR2 Lua")
    check(lua_check.style_problems('x = {} -- table.getn\ny = "math.floor"') == [], "library names in comments and strings are fine")
    check(lua_check.style_problems("a = my_table.n + b.table.x") == [], "library names inside longer names are fine")
    check(lua_check.style_problems("function f(...) return arg end") == [], "... in a parameter list is Lua 5.0")
    check(lua_check.style_problems("function f() return ... end") != [], "... in a body is Lua 5.1 only")
    check(lua_check.style_problems('-- #list\nx = "#a" .. \'#b\'') == [], "comments and strings are ignored")
    runtime = lua_check.lua_runtime()
    check(lua_check.parse_error(runtime, "x = 1") is None and lua_check.parse_error(runtime, "x = = 1") is not None, "the parser tells good from bad")


def made_up(directory, line_ending):
    text = "-- made up\nfunction f(who)\n\tif who == REMOTE_PLAYER then\n\t\tg(REMOTE_PLAYER)\n\tend\n\tteleport(who)\nend\n"
    (directory / "demo.lua").write_bytes(text.replace("\n", line_ending).encode("latin-1"))
    (directory / "ug_lib.lua").write_bytes(("-- library\n".replace("\n", line_ending)).encode("latin-1"))
    (directory / "fixed.lua").write_bytes(b"x = 1\n")


def test_build():
    saved_patches, saved_fixed = dict(lua_overrides.PATCHES), lua_overrides.FIXED_FILES
    lua_overrides.PATCHES.clear()
    lua_overrides.PATCHES.update({"demo.lua": Patch(each=("g",)), "ug_lib.lua": Patch(generic=False), "fixed.lua": Patch(edits=[("x = 1", "x = 2", 1)])})
    lua_overrides.FIXED_FILES = ("fixed.lua",)
    try:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixes = root / "fixes"
            fixes.mkdir()
            (fixes / "fixed.lua").write_bytes(b"x = 1\r\n")
            for label, ending in (("LF", "\n"), ("CRLF", "\r\n")):
                source, out = root / ("source_" + label), root / ("out_" + label)
                source.mkdir()
                made_up(source, ending)
                written, parameter_rule = lua_overrides.build(source, fixes, out)
                check(written == ["demo.lua", "fixed.lua", "ug_lib.lua"], f"{label}: every patched script is written")
                demo = (out / "demo.lua").read_bytes().decode("latin-1")
                check("coop_is_remote_player(who)" in demo and "coop_each_remote(g, REMOTE_PLAYER)" in demo, f"{label}: the patch and the generic rules apply")
                check("coop_native(teleport)(who)" in demo and parameter_rule == (1, 1), f"{label}: the parameter rule applies and is counted")
                check(demo.count("\r\n") == (demo.count("\n") if ending == "\r\n" else 0), f"{label}: line endings kept")
                library = (out / "ug_lib.lua").read_bytes().decode("latin-1")
                check("function coop_wrap(" in library and "function coop_player_table(" in library and "COOP_RULES" in library, f"{label}: ug_lib.lua gets the library")
                check(lua_check.check_dir(out) == [], f"{label}: the generated scripts parse and are Lua 5.0 style")
            check((root / "out_LF" / "fixed.lua").read_bytes() == b"x = 2\r\n", "a script in FIXED_FILES starts from the fixed copy")

            broken = root / "broken"
            broken.mkdir()
            made_up(broken, "\n")
            lua_overrides.PATCHES["demo.lua"] = Patch(edits=[("never in the script", "x", 1)])
            out = root / "out_broken"
            check(raises(lua_overrides.build, broken, fixes, out) and not out.exists(), "a patch that does not fit stops the build before anything is written")
    finally:
        lua_overrides.PATCHES.clear()
        lua_overrides.PATCHES.update(saved_patches)
        lua_overrides.FIXED_FILES = saved_fixed


def real_edited_bodies(source, fixes):
    """(script name, its text with the patch and generic rules applied, before the parameter rule) for every script but the library."""
    for name, patch in PATCHES.items():
        if name != lua_overrides.LIBRARY_FILE:
            path = (fixes if name in lua_overrides.FIXED_FILES else source) / name
            yield name, lua_overrides.edited_body(name, lua_overrides.read(path), patch)


def real_parameter_calls(source, fixes):
    return sum(lua_params.rewrite(body, lua_overrides.WRAPPED)[1] for _, body in real_edited_bodies(source, fixes))


def real_risky_calls(source, fixes):
    return [
        f"{name}: {helper}({argument})"
        for name, body in real_edited_bodies(source, fixes)
        for _, helper, argument in lua_params.risky_calls(body, lua_overrides.WRAPPED)
    ]


def test_real_scripts():
    source, fixes = os.environ.get("SR2_LUA_SOURCE"), os.environ.get("SR2_LUA_FIXES")
    if not (source and fixes):
        print("skip real scripts (set SR2_LUA_SOURCE and SR2_LUA_FIXES)")
        return
    with tempfile.TemporaryDirectory() as temporary:
        out = Path(temporary)
        written, (parameter_calls, parameter_scripts) = lua_overrides.build(Path(source), Path(fixes), out)
        check(sorted(written) == sorted(PATCHES) and len(written) == REAL_SCRIPT_COUNT, f"all {REAL_SCRIPT_COUNT} real overrides are written")
        check(parameter_calls == real_parameter_calls(Path(source), Path(fixes)), f"params: {parameter_calls} calls in {parameter_scripts} scripts")
        check(real_risky_calls(Path(source), Path(fixes)) == ["rn05.lua: rn05_wait_for_vehicle_entry(passenger)"],
              "the only helper with a rewritten parameter that is given the remote literal is rn05_wait_for_vehicle_entry")
        problems = lua_check.check_dir(out, Path(source))
        sync_calls = sum((Path(temporary) / name).read_bytes().decode("latin-1").count("coop_sync_for(") for name in ("rn02.lua", "ep02.lua", "rn08.lua"))
        check(sync_calls == 5, f"rn02, ep02 and rn08 hold the 5 targeted sync masks (found {sync_calls})")
        generated = {name: (out / name).read_bytes().decode("latin-1") for name in written if name != "ug_lib.lua"}
        counts = {name[:-4]: text.count("coop_waypoint_add_for(") + text.count("coop_waypoint_remove_for(") for name, text in generated.items()}
        counts = {name: count for name, count in counts.items() if count}
        check(counts == WAYPOINT_CALLS, f"the per-player waypoint calls per script are {WAYPOINT_CALLS} (found {counts})")
        census = [
            (name[:-4], line.strip())
            for name, text in sorted(generated.items())
            for line in text.split("\n")
            if WAYPOINT_SYNC_CALL.search(line)
        ]
        check(census == TSS04_REMOTE_TOW_CALLS,
              f"the only waypoint calls left through a sync variable are the three tss04 remote tow ones, lines 1134, 1209 and 1273 of the source (found {census})")
        ss01 = (Path(temporary) / "ss01.lua").read_bytes().decode("latin-1")
        check(ss01.count("coop_waypoint_add_for( p, waypoint_add, closest_nav_remote, SYNC_REMOTE )") == 1 and ss01.count("coop_closest_navpoint( group_locations, p )") == 1,
              "ss01 sends each remote its own nearest dealer")
        rn02 = (Path(temporary) / "rn02.lua").read_bytes().decode("latin-1")
        check(rn02.count("COOP_NATIVE.inv_item_equip(FREE_WEAPON_GUN, p)") == 1, "rn02 equips the gun per remote")
        check(problems == [], "every real override parses and adds only Lua 5.0 code" + ("" if not problems else ": " + "; ".join(problems[:3])))


def main():
    for test in (test_engine, test_generic_rules, test_lua_check, test_build, test_real_scripts):
        test()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
