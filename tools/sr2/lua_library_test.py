"""Runs the Lua code that lua_overrides.py appends to ug_lib.lua (tools/sr2/lua) against mock natives.

usage: python tools/sr2/lua_library_test.py        (needs the `lupa` package: pip install lupa)

With two players every wrapped call must reach the original once, unchanged; with three or four it follows its rule.
"""
import re
import sys
from pathlib import Path

import lupa.lua51

LUA_DIR = Path(__file__).with_name("lua")
LIBRARY_PARTS = ("coop_helpers.lua", "coop_missions.lua", "coop_wrappers.lua")
RULES_FILE = "coop_rules.lua"
LOCAL = "#PLAYER1#"
PLAYERS = ["#PLAYER1#", "#PLAYER2#", "#PLAYER3#", "#PLAYER4#"]

PRELUDE = """
LOCAL_PLAYER = "#PLAYER1#"
REMOTE_PLAYER = "#PLAYER2#"
LOG = {}
RESULT = {}
PLAYERS = {}
function player_names_get_all() return PLAYERS end
function rand_int(low, high) return low end
local function show(name, args)
	local parts = {}
	for i = 1, args.n do parts[i] = tostring(args[i]) end
	LOG[table.getn(LOG) + 1] = name .. "(" .. table.concat(parts, ",") .. ")"
end
function mock(name)
	_G[name] = function(...)
		show(name, arg)
		local result = RESULT[name]
		if type(result) == "function" then return result(unpack(arg, 1, arg.n)) end
		return result
	end
end
"""

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def new_runtime(player_count):
    """A Lua state with the library loaded over mock natives, `player_count` players in the game."""
    lua = lupa.lua51.LuaRuntime()
    lua.execute(PRELUDE)
    names = re.findall(r'"([a-z_0-9]+)"', (LUA_DIR / RULES_FILE).read_text(encoding="utf-8"))
    dist_aliases = {"get_dist_char_to_nav", "get_dist_char_to_char", "get_dist_char_to_vehicle"}
    for name in names:
        if name in dist_aliases:
            lua.execute(f'function {name}(a, b) return get_dist(a, b) end')
        else:
            lua.execute(f'mock("{name}")')
    for part in LIBRARY_PARTS:
        lua.execute((LUA_DIR / part).read_text(encoding="utf-8"))
    set_players(lua, player_count)
    lua.execute((LUA_DIR / RULES_FILE).read_text(encoding="utf-8"))
    return lua


def set_players(lua, count):
    lua.execute("PLAYERS = {" + ",".join(f'"{p}"' for p in PLAYERS[:count]) + "}")


def call(lua, code):
    """The log of the natives reached by running `code` (a fresh log), and the value of the last expression."""
    lua.execute("LOG = {}")
    value = lua.eval(f"(function() return {code} end)()")
    log = list(lua.eval("LOG").values())
    return log, value


def test_two_players():
    lua = new_runtime(2)
    calls = {
        "fanout": ('teleport(REMOTE_PLAYER, "nav", true)', '#PLAYER2#,nav,true', "teleport"),
        "callback": ('on_vehicle_enter("cb", REMOTE_PLAYER)', 'cb,#PLAYER2#', "on_vehicle_enter"),
        "thread": ('thread_new("fn", 1, REMOTE_PLAYER)', 'fn,1,#PLAYER2#', "thread_new"),
        "seats": ('vehicle_enter_teleport(REMOTE_PLAYER, "veh", 2)', '#PLAYER2#,veh,2', "vehicle_enter_teleport"),
        "force seat": ("player_force_vehicle_seat(REMOTE_PLAYER, 3, \"veh\")", '#PLAYER2#,3,veh', "player_force_vehicle_seat"),
        "group": ('vehicle_enter_group_teleport(REMOTE_PLAYER, LOCAL_PLAYER, "veh")', '#PLAYER2#,#PLAYER1#,veh', "vehicle_enter_group_teleport"),
        "target": ('attack("npc", REMOTE_PLAYER)', 'npc,#PLAYER2#', "attack"),
        "all": ("character_is_in_vehicle(REMOTE_PLAYER)", '#PLAYER2#', "character_is_in_vehicle"),
        "nearest": ('get_dist(REMOTE_PLAYER, "nav")', '#PLAYER2#,nav', "get_dist"),
        "alias": ('get_dist_char_to_nav(REMOTE_PLAYER, "nav")', '#PLAYER2#,nav', "get_dist"),
    }
    lua.execute("RESULT.character_is_in_vehicle = false; RESULT.get_dist = 7.5; RESULT.teleport = 'done'")
    for label, (code, arguments, native) in calls.items():
        log, _ = call(lua, code)
        check(log == [f"{native}({arguments})"], f"2 players: {label} reaches the original once, unchanged")
    log, value = call(lua, 'character_is_in_vehicle(REMOTE_PLAYER)')
    check(value is False, "2 players: a query returns the original's value")
    _, value = call(lua, 'get_dist(REMOTE_PLAYER, "nav")')
    check(value == 7.5, "2 players: get_dist returns the original's number")
    _, value = call(lua, 'teleport(REMOTE_PLAYER, "nav")')
    check(value == "done", "2 players: a call returns the original's result")
    log, _ = call(lua, 'teleport(LOCAL_PLAYER, "nav")')
    check(log == ["teleport(#PLAYER1#,nav)"], "2 players: a call without REMOTE_PLAYER is untouched")


def test_alone():
    lua = new_runtime(1)
    log, _ = call(lua, 'teleport(REMOTE_PLAYER, "nav")')
    check(log == ["teleport(#PLAYER2#,nav)"], "single player: the call is the two-player call")
    check(lua.eval("coop_is_remote_player(REMOTE_PLAYER)") is True, "single player: REMOTE_PLAYER is still the remote player")
    state = lua.eval("coop_player_table(false, true)")
    check(state["#PLAYER1#"] is False and state["#PLAYER2#"] is True, "single player: a player table has both entries")


def test_three_players():
    lua = new_runtime(3)
    log, _ = call(lua, 'teleport(REMOTE_PLAYER, "nav", true)')
    check(log == ["teleport(#PLAYER2#,nav,true)", "teleport(#PLAYER3#,nav,true)"], "3 players: fanout calls once per remote")
    log, _ = call(lua, 'on_take_damage("cb", REMOTE_PLAYER)')
    check(log == ["on_take_damage(cb,#PLAYER2#)", "on_take_damage(cb,#PLAYER3#)"], "3 players: a callback is registered per remote")
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "veh", 2)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,veh,2)", "vehicle_enter_teleport(#PLAYER3#,veh,3)"], "3 players: the next remote takes the next seat")
    log, _ = call(lua, "player_force_vehicle_seat(REMOTE_PLAYER, -1)")
    check(log == ["player_force_vehicle_seat(#PLAYER2#,-1)", "player_force_vehicle_seat(#PLAYER3#,-1)"], "3 players: releasing a seat keeps the number")
    log, _ = call(lua, 'vehicle_enter_group_teleport(REMOTE_PLAYER, LOCAL_PLAYER, "veh")')
    check(log == ["vehicle_enter_group_teleport(#PLAYER2#,#PLAYER3#,#PLAYER1#,veh)"], "3 players: a group holds every remote")
    lua.execute("RESULT.get_dist = function(a, b) if a == '#PLAYER3#' or b == '#PLAYER3#' then return 5 else return 9 end end")
    log, _ = call(lua, 'attack("npc", REMOTE_PLAYER)')
    check(log[-1] == "attack(npc,#PLAYER3#)", "3 players: target is the remote nearest to the attacker")
    _, value = call(lua, 'get_dist(REMOTE_PLAYER, "nav")')
    check(value == 5, "3 players: nearest is the smallest distance")
    _, value = call(lua, 'get_dist_char_to_nav(REMOTE_PLAYER, "nav")')
    check(value == 5, "3 players: a get_dist alias is nearest too, without widening twice")
    lua.execute("RESULT.character_is_in_vehicle = function(p) return p == '#PLAYER2#' end")
    _, value = call(lua, "character_is_in_vehicle(REMOTE_PLAYER)")
    check(value is False, "3 players: all is false when one remote fails")
    _, value = call(lua, "coop_any_remote(character_is_in_vehicle, REMOTE_PLAYER)")
    check(value is True, "3 players: coop_any_remote is true when one remote does")
    _, value = call(lua, "coop_all_remotes(character_is_in_vehicle, REMOTE_PLAYER)")
    check(value is False, "3 players: coop_all_remotes is false when one remote fails")
    _, value = call(lua, "coop_first_remote(character_is_in_vehicle, REMOTE_PLAYER)")
    check(value == "#PLAYER2#", "3 players: coop_first_remote names the remote that satisfies it")
    lua.execute("RESULT.get_dist = function(p, o) if p == '#PLAYER2#' then return 3 else return 8 end end")
    check(lua.eval('coop_farthest_remote_dist(get_dist, "nav")') == 8, "3 players: coop_farthest_remote_dist reads each remote itself")
    check(lua.eval('coop_any_remote_farther_than(get_dist, "nav", 5)') is True, "3 players: coop_any_remote_farther_than sees the far remote")
    expanded = list(lua.eval('coop_expand_remote({LOCAL_PLAYER, "npc", REMOTE_PLAYER})').values())
    check(expanded == [LOCAL, "npc", "#PLAYER2#", "#PLAYER3#"], "3 players: coop_expand_remote lists every remote")


def test_tables_and_state():
    lua = new_runtime(3)
    state = lua.eval("coop_player_table(false, {})")
    check(state["#PLAYER1#"] is False and state["#PLAYER2#"] is not state["#PLAYER3#"],
          "tables: every remote has its own copy of the default")
    set_players(lua, 4)
    check(lua.eval("coop_player_table(1, 2)[ '#PLAYER4#' ]") == 2, "tables: a remote added later reads the default")
    lua.execute("S = coop_player_table(false, false)")
    check(lua.eval("coop_all_players_in(S)") is False and lua.eval("coop_any_player_in(S)") is False, "tables: nobody in")
    lua.execute("S['#PLAYER1#'] = true; S['#PLAYER2#'] = true; S['#PLAYER3#'] = true; S['#PLAYER4#'] = true")
    check(lua.eval("coop_all_players_in(S)") is True, "tables: everybody in")
    lua.execute("S['#PLAYER3#'] = false")
    check(lua.eval("coop_all_players_in(S)") is False and lua.eval("coop_any_remote_in(S)") is True, "tables: one remote out")
    check(lua.eval("coop_first_remote_equal(S, false)") == "#PLAYER3#", "tables: the first remote with a value")
    lua.execute("coop_set_remotes(S, 7)")
    check(lua.eval("coop_all_remotes_equal(S, 7)") is True, "tables: coop_set_remotes sets every remote")


def test_wrapping_twice():
    lua = new_runtime(3)
    lua.execute((LUA_DIR / RULES_FILE).read_text(encoding="utf-8"))
    log, _ = call(lua, 'teleport(REMOTE_PLAYER, "nav")')
    check(len(log) == 2, "wrapping twice does not wrap a wrapper")


def main():
    for test in (test_two_players, test_alone, test_three_players, test_tables_and_state, test_wrapping_twice):
        test()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
