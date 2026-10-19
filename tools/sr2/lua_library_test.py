"""Runs the Lua code that lua_overrides.py appends to ug_lib.lua (tools/sr2/lua) against mock natives.

usage: python tools/sr2/lua_library_test.py        (needs the `lupa` package: pip install lupa)

With two players every wrapped call must reach the original once, unchanged; with three or four it follows its rule.
"""
import re
import sys
from pathlib import Path

import lupa.lua51

LUA_DIR = Path(__file__).with_name("lua")
LIBRARY_PARTS = ("coop_helpers.lua", "coop_missions.lua", "coop_outfit_sync.lua", "coop_slots.lua", "coop_wrappers.lua")
RULES_FILE = "coop_rules.lua"
LOCAL = "#PLAYER1#"
PLAYERS = ["#PLAYER1#", "#PLAYER2#", "#PLAYER3#", "#PLAYER4#"]

PRELUDE = """
LOCAL_PLAYER = "#PLAYER1#"
REMOTE_PLAYER = "#PLAYER2#"
SYNC_LOCAL = 1
SYNC_REMOTE = 2
SYNC_ALL = 3
function log_reset() LOG = {}; LOG_SIZE = 0 end
log_reset()
RESULT = {}
PLAYERS = {}
function player_names_get_all() return PLAYERS end
function rand_int(low, high) return low end
SEATS_IN_VEHICLE = 4
OCCUPIED = {}
TRACE = {}
TRACE_COUNT = 0
function vehicle_get_num_seats(vehicle) return SEATS_IN_VEHICLE end
function get_char_in_vehicle(vehicle, seat) return OCCUPIED[seat] end
function mission_debug(text) TRACE_COUNT = TRACE_COUNT + 1 TRACE[TRACE_COUNT] = text return 0 end
function get_other_coop_player(name) if name == LOCAL_PLAYER then return REMOTE_PLAYER end return LOCAL_PLAYER end
local concat = table.concat
local function show(name, args)
	local parts = {}
	for i = 1, args.n do parts[i] = tostring(args[i]) end
	LOG_SIZE = LOG_SIZE + 1
	LOG[LOG_SIZE] = name .. "(" .. concat(parts, ",") .. ")"
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

# The game registers the base library of Lua 5.0 only: no table, string, math or os, and no select.
ABSENT_LIBRARIES = "table = nil; string = nil; math = nil; os = nil; io = nil; debug = nil; package = nil; select = nil"

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
    lua.execute(ABSENT_LIBRARIES)
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
    lua.execute("log_reset()")
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


def test_sr2_runtime():
    """The runtime is the game's: base library only; the library, a fanout, a target and a group wrapper still work."""
    for count in (2, 3):
        lua = new_runtime(count)
        check(all(lua.eval(f"{name} == nil") for name in ("table", "string", "math", "select")), f"{count} players: the runtime has no table, string, math or select")
        check(lua.eval("coop_count(coop_remote_players())") == count - 1, f"{count} players: coop_count counts the remotes")
        check(lua.eval("coop_count({})") == 0 and lua.eval("coop_count({1, 2, nil, 4})") == 2, f"{count} players: coop_count stops at the first nil")
        check(lua.eval("coop_random_remote()") == "#PLAYER2#", f"{count} players: coop_random_remote picks a remote")
        check(list(lua.eval("coop_all_players()").values()) == PLAYERS[:count], f"{count} players: coop_all_players lists the local player then every remote")
        log, _ = call(lua, 'teleport(REMOTE_PLAYER, "nav")')
        check(len(log) == count - 1, f"{count} players: a fanout wrapper makes one call per remote")
        log, _ = call(lua, 'attack("npc", REMOTE_PLAYER)')
        check(len(log) >= 1 and log[-1].startswith("attack(npc,#PLAYER"), f"{count} players: a target wrapper makes its call")
        log, _ = call(lua, 'vehicle_enter_group_teleport(REMOTE_PLAYER, LOCAL_PLAYER, "veh")')
        check(len(log) == 1, f"{count} players: a group wrapper makes one call")


def test_sync_targets():
    """coop_sync_for narrows the remote part of a sync mask to one player number (16 * n above the two shipped bits)."""
    lua = new_runtime(2)
    lua.execute("S = coop_player_table(SYNC_LOCAL, SYNC_REMOTE)")
    check(lua.eval("coop_sync_for(S, '#PLAYER1#')") == 1 and lua.eval("coop_sync_for(S, '#PLAYER2#')") == 2,
          "sync: with 2 players a mask is the table value")
    lua = new_runtime(3)
    lua.execute("S = coop_player_table(SYNC_LOCAL, SYNC_REMOTE); A = coop_player_table(SYNC_LOCAL, SYNC_ALL); L = coop_player_table(SYNC_LOCAL, SYNC_LOCAL)")
    check(lua.eval("coop_sync_for(S, '#PLAYER1#')") == 1, "sync: the local player keeps SYNC_LOCAL")
    check(lua.eval("coop_sync_for(S, '#PLAYER2#')") == 34 and lua.eval("coop_sync_for(S, '#PLAYER3#')") == 50,
          "sync: with 3 players a remote mask carries its player number")
    check(lua.eval("coop_sync_for(A, '#PLAYER3#')") == 51, "sync: SYNC_ALL keeps both bits under the number")
    check(lua.eval("coop_sync_for(L, '#PLAYER3#')") == 1, "sync: a remote entry without the remote bit is not narrowed")
    check(lua.eval("coop_sync_for(S, '#PLAYER9#')") is None, "sync: a name that is not in the game gives the table value")
    set_players(lua, 4)
    check(lua.eval("coop_sync_for(S, '#PLAYER4#')") == 66, "sync: with 4 players the fourth player is 2 + 16 * 4")
    lua = new_runtime(1)
    lua.execute("S = coop_player_table(SYNC_LOCAL, SYNC_REMOTE)")
    check(lua.eval("coop_sync_for(S, '#PLAYER2#')") == 2, "sync: alone the mask is the two-player mask")


def trace_lines(lua):
    """The text of every mission_debug call since the last clear_trace."""
    return list(lua.eval("TRACE").values())


def clear_trace(lua):
    lua.execute("TRACE = {}; TRACE_COUNT = 0")


def lua_list(lua, code):
    return list(lua.eval(code).values())


def test_player_numbers():
    lua = new_runtime(4)
    check(all(lua.eval(f'coop_player_number("#PLAYER{n}#")') == n for n in range(1, 13)), "numbers: #PLAYER1# to #PLAYER12# are 1 to 12")
    check(lua.eval('coop_player_number("#NPC#")') is None and lua.eval("coop_player_number(nil)") is None, "numbers: any other name has none")


def test_hud_slots():
    lua = new_runtime(4)
    check(lua.eval('coop_hud_slot("#PLAYER1#", 0, {1, 2, 3})') == 0, "slots: the local player takes the local slot")
    check([lua.eval(f'coop_hud_slot("#PLAYER{n}#", 0, {{1, 2, 3}})') for n in (2, 3, 4)] == [1, 2, 3], "slots: remote n takes remote_slots[n - 1]")
    check(lua.eval('coop_hud_slot("#PLAYER4#", 1, {2, 3})') == 2, "slots: a remote without an entry shares the first remote slot")
    clear_trace(lua)
    lua.execute("S = coop_hud_slots(0, {1, 2, 3})")
    check(lua.eval("S[LOCAL_PLAYER]") == 0 and lua.eval("S[REMOTE_PLAYER]") == 1, "slots: the table holds the local and the two-player remote slot")
    check(trace_lines(lua) == [], "slots: those two entries trace nothing")
    check(lua.eval('S["#PLAYER3#"]') == 2 and lua.eval('S["#PLAYER3#"]') == 2, "slots: a remote read gives its slot")
    check(trace_lines(lua) == ["coop: hud slot #PLAYER3# 2"], "slots: the first read of a remote traces once")
    check(lua.eval('S["#NPC#"]') is None, "slots: a name that is not a remote player has no slot")


def test_each_remote_slot():
    lua = new_runtime(4)
    lua.execute("N = 0; CALLED = {}; function note(slot) N = N + 1; CALLED[N] = slot end")
    lua.execute("coop_each_remote_slot(note, coop_hud_slots(0, {1, 2, 3}))")
    check(lua_list(lua, "CALLED") == [1, 2, 3], "each slot: 4 players call slots 1, 2, 3")
    lua.execute("N = 0; CALLED = {}; coop_each_remote_slot(note, coop_hud_slots(0, {2, 3}))")
    check(lua_list(lua, "CALLED") == [2, 3], "each slot: a shared slot is called once")
    for count in (1, 2):
        lua = new_runtime(count)
        lua.execute("N = 0; CALLED = {}; function note(slot) N = N + 1; CALLED[N] = slot end")
        lua.execute("coop_each_remote_slot(note, coop_hud_slots(0, {1, 2, 3}))")
        check(lua_list(lua, "CALLED") == [1], f"each slot: {count} players call slot 1 only")


def test_other_players():
    lua = new_runtime(2)
    check(lua_list(lua, 'coop_other_players("#PLAYER1#")') == ["#PLAYER2#"] and lua_list(lua, 'coop_other_players("#PLAYER2#")') == [LOCAL],
          "others: with 2 players exactly the other one")
    lua.execute('V = {["#PLAYER1#"] = 5, ["#PLAYER2#"] = 7}')
    check(lua.eval('coop_any_other_equal(V, "#PLAYER1#", 7)') is True and lua.eval('coop_any_other_equal(V, "#PLAYER2#", 7)') is False,
          "others: coop_any_other_equal looks at the other player only")
    check(lua_list(lua, "coop_remotes_equal(V, 7)") == ["#PLAYER2#"], "others: 2 players, coop_remotes_equal lists the remote")
    check(lua.eval("coop_all_remotes_at_least(V, 7)") is True and lua.eval("coop_all_remotes_at_least(V, 8)") is False, "others: 2 players, at least")
    lua.execute('function sees(p, who) return p == who end')
    check(lua.eval('coop_any_other(sees, "#PLAYER1#", "#PLAYER2#")') is True, "others: coop_any_other passes the player and the extra arguments")
    lua = new_runtime(4)
    check(lua_list(lua, 'coop_other_players("#PLAYER2#")') == ["#PLAYER1#", "#PLAYER3#", "#PLAYER4#"], "others: 4 players, the local player first")
    lua.execute('V = {["#PLAYER1#"] = 0, ["#PLAYER2#"] = 7, ["#PLAYER3#"] = 3, ["#PLAYER4#"] = 7}')
    check(lua_list(lua, "coop_remotes_equal(V, 7)") == ["#PLAYER2#", "#PLAYER4#"], "others: 4 players, remotes with a value in order")
    check(lua.eval("coop_all_remotes_at_least(V, 3)") is True and lua.eval("coop_all_remotes_at_least(V, 4)") is False, "others: 4 players, at least")
    lua.execute('CAR = {["#PLAYER1#"] = "a", ["#PLAYER2#"] = "a", ["#PLAYER3#"] = "a", ["#PLAYER4#"] = "b"}')
    check(lua.eval('coop_all_in_vehicle_of(CAR, "#PLAYER1#")') is False and lua.eval('coop_all_in_vehicle_of(CAR, "#PLAYER4#")') is False,
          "others: coop_all_in_vehicle_of is false when one player is in another vehicle")
    lua.execute('CAR["#PLAYER4#"] = "a"')
    check(lua.eval('coop_all_in_vehicle_of(CAR, "#PLAYER2#")') is True, "others: coop_all_in_vehicle_of is true when all share it")


def test_next_player():
    lua = new_runtime(4)
    order = [lua.eval(f'coop_next_player("#PLAYER{n}#")') for n in (1, 2, 3, 4)]
    check(order == ["#PLAYER2#", "#PLAYER3#", "#PLAYER4#", "#PLAYER1#"], "next: cycles P1, P2, P3, P4, P1")
    lua.execute('CLEAN = {["#PLAYER3#"] = true}')
    check(lua.eval('coop_next_player_where("#PLAYER4#", function(p) return CLEAN[p] end)') == "#PLAYER3#", "next: where wraps around")
    check(lua.eval('coop_next_player_where("#PLAYER3#", function(p) return CLEAN[p] end)') is None, "next: where never returns the start")
    check(lua.eval("coop_next_player_where(LOCAL_PLAYER, function(p) return false end)") is None, "next: where gives nil when none fits")
    two = new_runtime(2)
    check(two.eval('coop_next_player("#PLAYER2#")') == LOCAL and two.eval("coop_next_player(LOCAL_PLAYER)") == "#PLAYER2#", "next: 2 players, the other one")


def test_nearest_players():
    lua = new_runtime(4)
    lua.execute("RESULT.get_dist = function(a, b) if a == '#PLAYER2#' then return 1 elseif a == '#PLAYER3#' then return 3 else return 7 end end")
    lua.execute("RESULT.character_is_dead = function(p) return p == '#PLAYER2#' end")
    check(lua.eval('coop_nearest_other_player("truck", "#PLAYER1#")') == "#PLAYER3#", "nearest: a living player before a nearer dead one")
    lua.execute("RESULT.character_is_dead = true")
    check(lua.eval('coop_nearest_other_player("truck", "#PLAYER1#")') == "#PLAYER2#", "nearest: all dead gives the nearest dead")
    lua.execute('IN = {["#PLAYER3#"] = true, ["#PLAYER4#"] = true}')
    check(lua.eval('coop_nearest_other_in(IN, "truck", "#PLAYER1#")') == "#PLAYER3#", "nearest: the smallest distance among those marked")
    check(lua.eval('coop_nearest_other_in({}, "truck", "#PLAYER1#")') == "#PLAYER2#", "nearest: none marked falls back to get_other_coop_player")
    check(lua.eval('coop_nearest_dist_of({"#PLAYER3#", "#PLAYER4#"}, "nav")') == 3, "nearest: coop_nearest_dist_of is the smallest distance")
    check(lua.eval('coop_nearest_dist_of({}, "nav")') == lua.eval("COOP_FAR_DISTANCE"), "nearest: an empty list is far")
    lua.execute('function measure(navs, p) return "nav" .. p, 1, COOP_NATIVE.get_dist(p, "x") end')
    check(lua.eval("(coop_best_remote(measure, 3, {}, REMOTE_PLAYER))") == "nav#PLAYER2#", "nearest: coop_best_remote gives the results of the best remote")


def test_seat_rule():
    lua = new_runtime(4)
    lua.execute('coop_reserve_seats("boat", {0, 1, 3})')
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "boat", 2)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,boat,2)"], f"seats: P2 keeps the given seat, P3 and P4 find none ({log})")
    check(lua.eval('coop_seat_of("boat", "#PLAYER2#", 9)') == 2 and lua.eval('coop_seat_of("boat", "#PLAYER3#", 2)') is None, "seats: the record answers coop_seat_of")
    lua.execute("SEATS_IN_VEHICLE = 6; coop_reserve_seats('heli', {1, 2})")
    expected = ["vehicle_enter_teleport(#PLAYER2#,heli,0)", "vehicle_enter_teleport(#PLAYER3#,heli,5)", "vehicle_enter_teleport(#PLAYER4#,heli,4)"]
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "heli", 0)')
    check(log == expected, f"seats: the others take the highest free seats ({log})")
    lua.execute("OCCUPIED[5] = 'npc'")
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "heli", 0)')
    check(log == expected, "seats: a retry keeps the recorded seats even though they now hold somebody")
    lua.execute("OCCUPIED[5] = 'npc'; OCCUPIED[4] = 'npc'; coop_reserve_seats('van', {0})")
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "van", 0)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,van,0)", "vehicle_enter_teleport(#PLAYER3#,van,3)", "vehicle_enter_teleport(#PLAYER4#,van,2)"],
          f"seats: a seat that holds somebody is skipped ({log})")
    lua.execute("OCCUPIED = {}; SEATS_IN_VEHICLE = 4")
    log, _ = call(lua, 'player_force_vehicle_seat(REMOTE_PLAYER, -1, "boat")')
    check(log == ["player_force_vehicle_seat(#PLAYER2#,-1,boat)", "player_force_vehicle_seat(#PLAYER3#,-1,boat)", "player_force_vehicle_seat(#PLAYER4#,-1,boat)"],
          "seats: releasing (-1) is passed on for every remote")
    log, _ = call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "other")')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,other)", "vehicle_enter_teleport(#PLAYER3#,other)", "vehicle_enter_teleport(#PLAYER4#,other)"],
          "seats: no seat given is passed on for every remote")
    log, _ = call(lua, "player_force_vehicle_seat(REMOTE_PLAYER, 3)")
    check(log == ["player_force_vehicle_seat(#PLAYER2#,3)", "player_force_vehicle_seat(#PLAYER3#,4)", "player_force_vehicle_seat(#PLAYER4#,5)"],
          "seats: no vehicle keeps seat + k - 1")
    one = new_runtime(2)
    log, _ = call(one, 'vehicle_enter_teleport(REMOTE_PLAYER, "boat", 2)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,boat,2)"], "seats: one remote makes the plain call")
    check(one.eval('coop_seat_of("boat", "#PLAYER2#", 9)') == 9, "seats: one remote has the script seat")
    alone = new_runtime(1)
    check(alone.eval('coop_seat_of("boat", "#PLAYER2#", 2)') == 2, "seats: alone the first remote has the script seat")
    check(lua.eval('coop_seat_of("never", "#PLAYER2#", 2)') == 2 and lua.eval('coop_seat_of("never", "#PLAYER3#", 2)') is None,
          "seats: without a record only the first remote has the script seat")


def test_in_vehicle_rule():
    lua = new_runtime(4)
    lua.execute('coop_reserve_seats("boat", {0, 1, 3})')
    call(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "boat", 2)')
    lua.execute('RESULT.character_is_in_vehicle = function(p, v) return p == "#PLAYER2#" end')
    clear_trace(lua)
    _, value = call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "boat")')
    check(value is True, "in vehicle: a remote without a seat counts as in")
    call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "boat")')
    left_out = [line for line in trace_lines(lua) if "leaves out" in line]
    check(left_out == ["coop: in vehicle boat leaves out #PLAYER3#, it has no seat", "coop: in vehicle boat leaves out #PLAYER4#, it has no seat"],
          f"in vehicle: each left-out remote is traced once ({left_out})")
    lua.execute('RESULT.character_is_in_vehicle = function(p, v) return p ~= "#PLAYER3#" end')
    _, value = call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "unknown")')
    check(value is False, "in vehicle: no record is the all rule")
    _, value = call(lua, "character_is_in_vehicle(REMOTE_PLAYER)")
    check(value is False, "in vehicle: without a vehicle it is the all rule")
    lua.execute('function in_p2(p, v) return p == "#PLAYER2#" end; function nobody(p, v) return false end')
    check(lua.eval('coop_all_seated(nobody, "boat")') is False, "seated: the seated remote is checked")
    check(lua.eval('coop_all_seated(in_p2, "boat")') is True, "seated: remotes without a seat are not asked")


def test_wrapping_twice():
    lua = new_runtime(3)
    lua.execute((LUA_DIR / RULES_FILE).read_text(encoding="utf-8"))
    log, _ = call(lua, 'teleport(REMOTE_PLAYER, "nav")')
    check(len(log) == 2, "wrapping twice does not wrap a wrapper")


def main():
    for test in (
        test_two_players, test_alone, test_three_players, test_tables_and_state, test_sr2_runtime, test_sync_targets,
        test_player_numbers, test_hud_slots, test_each_remote_slot, test_other_players, test_next_player, test_nearest_players,
        test_seat_rule, test_in_vehicle_rule, test_wrapping_twice,
    ):
        test()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
