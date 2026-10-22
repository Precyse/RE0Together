"""Runs the vehicle seating Lua (tools/sr2/lua/coop_seats.lua) against a mock world of vehicles with seats.

usage: python tools/sr2/lua_seats_test.py        (needs the `lupa` package: pip install lupa)

With two players every seat call must reach the original once, unchanged, with no query. With three or four every remote gets a seat of
its own, or stays on foot, and no seat ever holds two characters.
"""
import re
import sys

import lupa.lua51

import lua_library_test as library

PLAYERS = library.PLAYERS
SEAT_NATIVES = (
    "vehicle_enter_teleport", "vehicle_enter", "vehicle_exit_teleport", "player_force_vehicle_seat", "character_is_in_vehicle",
    "set_player_can_enter_exit_vehicles",
)

# Vehicles are { n = seat count, seat = { [number] = character } }. A character in MIDENTER sits in its seat, but the occupant
# lookup does not report it and its enter is not done, as the game does while a character is entering.
WORLD = """
local concat = table.concat
VEHICLES = {}
MIDENTER = {}
TRACE = {}
CONFLICTS = 0
local function logged(name, fn)
	_G[name] = function(...)
		local parts = {}
		for i = 1, arg.n do parts[i] = tostring(arg[i]) end
		LOG_SIZE = LOG_SIZE + 1
		LOG[LOG_SIZE] = name .. "(" .. concat(parts, ",") .. ")"
		return fn(unpack(arg, 1, arg.n))
	end
end
local function seat_of(c)
	for name, v in pairs(VEHICLES) do
		for s = 0, v.n - 1 do
			if v.seat[s] == c then return name, s end
		end
	end
	return nil, nil
end
local function enter(c, v, s)
	if VEHICLES[v] == nil then return end
	local name, at = seat_of(c)
	if name then VEHICLES[name].seat[at] = nil end
	if VEHICLES[v].seat[s] ~= nil then CONFLICTS = CONFLICTS + 1 end
	VEHICLES[v].seat[s] = c
end
function seat_vehicle(name, n) VEHICLES[name] = { n = n, seat = {} } end
function seat_char(c, v, s) enter(c, v, s) end
function seat_where(c) return seat_of(c) end
logged("vehicle_enter_teleport", function(c, v, s) enter(c, v, s) end)
logged("vehicle_enter", function(c, v, s) enter(c, v, s) end)
logged("vehicle_exit_teleport", function(c)
	local name, at = seat_of(c)
	if name then VEHICLES[name].seat[at] = nil end
end)
logged("player_force_vehicle_seat", function() end)
logged("set_player_can_enter_exit_vehicles", function() end)
logged("character_is_in_vehicle", function(c, v)
	if MIDENTER[c] then return false end
	local name = seat_of(c)
	if v == nil then return name ~= nil end
	return name == v
end)
logged("get_char_in_vehicle", function(v, s)
	local c = VEHICLES[v] and VEHICLES[v].seat[s]
	if c == nil or MIDENTER[c] then return nil end
	return c
end)
logged("vehicle_get_num_seats", function(v) return VEHICLES[v] and VEHICLES[v].n or 0 end)
logged("get_char_vehicle_name", function(c) return (seat_of(c)) end)
logged("vehicle_enter_check_done", function(c) return not MIDENTER[c] end)
function mission_debug(text) TRACE[coop_count(TRACE) + 1] = text end
"""

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def new_world(player_count, seats_known=True):
    """A Lua state with the library over the mock world; `seats_known` False leaves the seat query natives out."""
    lua = lupa.lua51.LuaRuntime()
    lua.execute(library.PRELUDE)
    lua.execute(WORLD)
    if not seats_known:
        for name in ("get_char_in_vehicle", "vehicle_get_num_seats", "get_char_vehicle_name", "vehicle_enter_check_done", "mission_debug"):
            lua.execute(f"{name} = nil")
    lua.execute(library.ABSENT_LIBRARIES)
    names = re.findall(r'"([a-z_0-9]+)"', (library.LUA_DIR / library.RULES_FILE).read_text(encoding="utf-8"))
    for name in names:
        if name in SEAT_NATIVES:
            continue
        if name in ("get_dist_char_to_nav", "get_dist_char_to_char", "get_dist_char_to_vehicle"):
            lua.execute(f"function {name}(a, b) return get_dist(a, b) end")
        else:
            lua.execute(f'mock("{name}")')
    for part in library.LIBRARY_PARTS:
        lua.execute((library.LUA_DIR / part).read_text(encoding="utf-8"))
    library.set_players(lua, player_count)
    lua.execute((library.LUA_DIR / library.RULES_FILE).read_text(encoding="utf-8"))
    return lua


def vehicles(lua, **seats):
    for name, count in seats.items():
        lua.execute(f'seat_vehicle("{name}", {count})')


def seated(lua, character, vehicle, seat):
    lua.execute(f'seat_char("{character}", "{vehicle}", {seat})')


QUERIES = ("get_char_in_vehicle(", "vehicle_get_num_seats(", "get_char_vehicle_name(", "vehicle_enter_check_done(", "character_is_in_vehicle(")


def run_all(lua, code):
    """Runs script code with a fresh log and returns every native it reached."""
    lua.execute("log_reset()")
    lua.execute(code)
    return list(lua.eval("LOG").values())


def run(lua, code):
    """The natives run_all reaches, without the seat queries."""
    return [line for line in run_all(lua, code) if not line.startswith(QUERIES)]


def place_of(lua, character):
    """(vehicle, seat) of the character in the mock world, (None, None) when it is in none."""
    vehicle, seat = lua.eval(f'(function() local v, s = seat_where("{character}") return v or "", s or -1 end)()')
    return (vehicle, seat) if vehicle else (None, None)


def on_foot(lua, character):
    return lua.eval(f'COOP_SEAT_LAST["{character}"]') == "on foot"


def invariant(lua, label):
    """No seat holds two characters, and every remote is seated or recorded on foot."""
    check(lua.eval("CONFLICTS") == 0, f"{label}: no seat holds two characters")
    count = lua.eval("coop_count(coop_remote_players())")
    ok = True
    for remote in PLAYERS[1 : count + 1]:
        ok = ok and (place_of(lua, remote)[0] is not None or on_foot(lua, remote))
    check(ok, f"{label}: every remote is seated or on foot")


def trace(lua):
    return list(lua.eval("TRACE").values())


def test_two_players():
    lua = new_world(2)
    vehicles(lua, veh=4)
    calls = {
        "seat": ('vehicle_enter_teleport(REMOTE_PLAYER, "veh", 2)', "vehicle_enter_teleport(#PLAYER2#,veh,2)"),
        "enter": ('vehicle_enter(REMOTE_PLAYER, "veh", 1)', "vehicle_enter(#PLAYER2#,veh,1)"),
        "force": ('player_force_vehicle_seat(REMOTE_PLAYER, 3, "veh")', "player_force_vehicle_seat(#PLAYER2#,3,veh)"),
        "release": ("player_force_vehicle_seat(REMOTE_PLAYER, -1)", "player_force_vehicle_seat(#PLAYER2#,-1)"),
        "lock": ("set_player_can_enter_exit_vehicles(REMOTE_PLAYER, false)", "set_player_can_enter_exit_vehicles(#PLAYER2#,false)"),
        "query": ('character_is_in_vehicle(REMOTE_PLAYER, "veh")', "character_is_in_vehicle(#PLAYER2#,veh)"),
        "local": ('vehicle_enter_teleport(LOCAL_PLAYER, "veh", 0)', "vehicle_enter_teleport(#PLAYER1#,veh,0)"),
    }
    for label, (code, expected) in calls.items():
        check(run_all(lua, code) == [expected], f"2 players: {label} reaches the original once, unchanged, with no seat query")
    check(trace(lua) == [], "2 players: nothing is logged")
    invariant(lua, "2 players")


def test_without_seat_natives():
    lua = new_world(4, seats_known=False)
    log = run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "veh", 2)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,veh,2)", "vehicle_enter_teleport(#PLAYER3#,veh,3)", "vehicle_enter_teleport(#PLAYER4#,veh,4)"],
          "4 players, no seat natives: the k-th remote takes seat + k - 1")
    log = run(lua, "player_force_vehicle_seat(REMOTE_PLAYER, 2, 'veh')")
    check(log[-1] == "player_force_vehicle_seat(#PLAYER4#,4,veh)", "4 players, no seat natives: a forced seat moves on the same way")


def test_candidates():
    lua = new_world(4)
    candidates = lambda n, scripted: list(lua.eval(f"coop_seat_candidates({n}, {scripted})").values())
    check(candidates(4, 2) == [3, 1], "candidates: the seats after the scripted one, then the ones before it")
    check(candidates(4, 0) == [1, 2, 3], "candidates: the driver's seat scripted gives every passenger seat")
    check(candidates(2, 1) == [], "candidates: a two seat vehicle has nothing left")
    check(candidates(3, 5) == [1, 2], "candidates: a scripted seat past the end only gives the seats that exist")
    check(0 not in candidates(8, 4) and 4 not in candidates(8, 4), "candidates: never the driver seat or the scripted seat")


def test_speedboat():
    """bh01 600: local 0, REMOTE 2, Maero 1, Carlos 3; the extra remotes lose their seats to the script's NPCs."""
    for seats, left_on_foot in ((4, 2), (5, 1)):
        lua = new_world(4)
        vehicles(lua, boat=seats)
        log = run(lua, '''
            vehicle_enter_teleport(LOCAL_PLAYER, "boat", 0)
            vehicle_enter_teleport(REMOTE_PLAYER, "boat", 2)
            vehicle_enter_teleport("maero", "boat", 1)
            vehicle_enter_teleport("carlos", "boat", 3)''')
        check(place_of(lua, "#PLAYER2#") == ("boat", 2), f"speedboat n={seats}: REMOTE_PLAYER keeps the scripted seat")
        check(place_of(lua, "maero") == ("boat", 1) and place_of(lua, "carlos") == ("boat", 3), f"speedboat n={seats}: the NPCs get the scripted seats")
        foot = sum(1 for p in ("#PLAYER3#", "#PLAYER4#") if place_of(lua, p)[0] is None)
        check(foot == left_on_foot, f"speedboat n={seats}: {left_on_foot} of the extra remotes are on foot")
        check("vehicle_exit_teleport(#PLAYER3#)" in log, f"speedboat n={seats}: the remote in Carlos' seat is taken out")
        check(any("evict #PLAYER3# from boat 3 for carlos" in line for line in trace(lua)), f"speedboat n={seats}: the eviction is logged")
        invariant(lua, f"speedboat n={seats}")


def test_parcel_truck():
    """bh06 377 with a 3 seat truck: REMOTE 0, Pierce 1, local 2."""
    lua = new_world(4)
    vehicles(lua, truck=3)
    run(lua, '''
        vehicle_enter_teleport(REMOTE_PLAYER, "truck", 0)
        vehicle_enter_teleport("pierce", "truck", 1)
        vehicle_enter_teleport(LOCAL_PLAYER, "truck", 2)''')
    check(place_of(lua, "#PLAYER2#") == ("truck", 0) and place_of(lua, "pierce") == ("truck", 1) and place_of(lua, "#PLAYER1#") == ("truck", 2),
          "parcel truck: the script's seats win")
    check(on_foot(lua, "#PLAYER3#") and on_foot(lua, "#PLAYER4#"), "parcel truck: P3 and P4 are on foot")
    invariant(lua, "parcel truck")
    log = run(lua, 'set_player_can_enter_exit_vehicles(REMOTE_PLAYER, false)')
    check(log == ["set_player_can_enter_exit_vehicles(#PLAYER2#,false)"], "parcel truck: the entry lock skips the players on foot")
    check(any("keep entry #PLAYER3# (on foot)" in line for line in trace(lua)), "parcel truck: the kept entry is logged")
    log = run(lua, 'set_player_can_enter_exit_vehicles(REMOTE_PLAYER, true)')
    check(len(log) == 3, "parcel truck: enabling entry reaches every remote")


def test_bikes():
    """rn03: two 2 seat bikes; P4 sits behind the local player."""
    lua = new_world(4)
    vehicles(lua, bike1=2, bike2=2)
    run(lua, '''
        vehicle_enter_teleport(LOCAL_PLAYER, "bike1", 0)
        vehicle_enter_teleport(REMOTE_PLAYER, "bike2", 0)''')
    check(place_of(lua, "#PLAYER3#") == ("bike2", 1), "bikes: P3 takes the second seat of REMOTE_PLAYER's bike")
    check(place_of(lua, "#PLAYER4#") == ("bike1", 1), "bikes: P4 takes the second seat of the local player's bike")
    log, value = library.call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "bike2")')
    check(value is True, "bikes: every remote is in bike2 as far as the script can ask, P4 included")
    check(any("ask #PLAYER4# in bike2 -> bike1" in line for line in trace(lua)), "bikes: the redirected answer is logged")
    invariant(lua, "bikes")


def test_cart():
    """rn05: a 2 seat cart is the local player's own vehicle; nobody else fits."""
    lua = new_world(4)
    vehicles(lua, cart=2)
    run(lua, '''
        vehicle_enter_teleport(LOCAL_PLAYER, "cart", 0)
        vehicle_enter_teleport(REMOTE_PLAYER, "cart", 1)''')
    check(on_foot(lua, "#PLAYER3#") and on_foot(lua, "#PLAYER4#"), "cart: P3 and P4 are on foot")
    _, value = library.call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "cart")')
    check(value is True, "cart: the wait for the vehicle entry ends")
    lua.execute('seat_char("#PLAYER2#", "cart", 1); MIDENTER["#PLAYER2#"] = true')
    _, value = library.call(lua, 'character_is_in_vehicle(REMOTE_PLAYER, "cart")')
    check(value is False, "cart: while REMOTE_PLAYER is not in, the wait goes on for everybody")
    log = run(lua, 'set_player_can_enter_exit_vehicles(REMOTE_PLAYER, false)')
    check(log == ["set_player_can_enter_exit_vehicles(#PLAYER2#,false)"], "cart: the entry lock skips P3 and P4")
    invariant(lua, "cart")


def test_mirror():
    """A remote on foot answers as REMOTE_PLAYER does, true and false."""
    lua = new_world(3)
    vehicles(lua, cart=2)
    run(lua, '''
        vehicle_enter_teleport(LOCAL_PLAYER, "cart", 0)
        vehicle_enter_teleport(REMOTE_PLAYER, "cart", 1)''')
    answer = lambda p: lua.eval(f'COOP_SEAT_RAW.in_vehicle("{p}", "cart")')
    mirrored = lambda: lua.eval('coop_seat_in_vehicle("#PLAYER3#", "cart")')
    check(mirrored() is True and answer("#PLAYER2#") is True, "mirror: REMOTE_PLAYER in the cart, the player on foot answers true")
    lua.execute('seat_vehicle("other", 2); seat_char("#PLAYER2#", "other", 0)')
    check(mirrored() is False, "mirror: REMOTE_PLAYER out of the cart, the player on foot answers false")
    check(lua.eval('coop_seat_in_vehicle("#PLAYER2#", "cart")') is False, "mirror: REMOTE_PLAYER itself is asked as the game does")


def test_forced_seats():
    """ss07 724 / 775: forced seats of the extra remotes; -1 clears them."""
    lua = new_world(4)
    vehicles(lua, boat=4, car=4)
    log = run(lua, '''
        player_force_vehicle_seat(LOCAL_PLAYER, 1, "boat")
        player_force_vehicle_seat(REMOTE_PLAYER, 2, "boat")''')
    check(log[1:] == ["player_force_vehicle_seat(#PLAYER2#,2,boat)", "player_force_vehicle_seat(#PLAYER3#,3,boat)", "player_force_vehicle_seat(#PLAYER4#,-1)"],
          "forced seats, boat: P2 2, P3 3, P4 -1 (the local player's forced seat 1 is taken)")
    log = run(lua, '''
        player_force_vehicle_seat(LOCAL_PLAYER, -1)
        player_force_vehicle_seat(REMOTE_PLAYER, -1)
        player_force_vehicle_seat(LOCAL_PLAYER, 2, "car")
        player_force_vehicle_seat(REMOTE_PLAYER, 3, "car")''')
    check(log[-3:] == ["player_force_vehicle_seat(#PLAYER2#,3,car)", "player_force_vehicle_seat(#PLAYER3#,1,car)", "player_force_vehicle_seat(#PLAYER4#,-1)"],
          "forced seats, car: P2 3, P3 1, P4 -1 once the boat's forced seats were released")
    log = run(lua, "player_force_vehicle_seat(REMOTE_PLAYER, 2)")
    check(log == ["player_force_vehicle_seat(#PLAYER2#,2)", "player_force_vehicle_seat(#PLAYER3#,-1)", "player_force_vehicle_seat(#PLAYER4#,-1)"],
          "forced seats: without a vehicle the extra remotes are released")
    check(any("force #PLAYER3# car 3 -> 1" in line for line in trace(lua)), "forced seats: the decision is logged")


def test_unresolved_vehicle():
    lua = new_world(4)
    log = run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "ghost", 2)')
    check(log == ["vehicle_enter_teleport(#PLAYER2#,ghost,2)", "vehicle_enter_teleport(#PLAYER3#,ghost,3)", "vehicle_enter_teleport(#PLAYER4#,ghost,4)"],
          "a vehicle that is not resolved (0 seats): the legacy seat + k - 1")
    check(any("(legacy)" in line for line in trace(lua)), "a vehicle that is not resolved: the legacy decision is logged")


def test_mid_enter():
    """An NPC that is entering seat 3 is not reported by the occupant lookup; its claim keeps the seat."""
    lua = new_world(3)
    vehicles(lua, van=4)
    run(lua, 'vehicle_enter_teleport("npc", "van", 3); MIDENTER["npc"] = true')
    check(lua.eval('get_char_in_vehicle("van", 3)') is None, "mid-enter: the occupant lookup does not report the entering NPC")
    run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "van", 2)')
    check(place_of(lua, "#PLAYER3#") == ("van", 1), "mid-enter: P3 does not take the seat the NPC is entering")
    invariant(lua, "mid-enter")
    lua.execute('MIDENTER["npc"] = nil')
    run(lua, 'vehicle_exit_teleport("npc")')
    check(lua.eval('coop_seat_taken("van", 3, {})') is False, "mid-enter: once the NPC left, its claim is gone")


def test_other_remote_vehicle():
    """With no seat left in the scripted vehicle the extra remotes sit in the vehicle another remote is in."""
    lua = new_world(4)
    vehicles(lua, buggy=4, boat=1)
    run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "buggy", 0)')
    run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "boat", 0)')
    check(place_of(lua, "#PLAYER2#") == ("boat", 0), "other remote vehicle: REMOTE_PLAYER takes the one seat of the boat")
    check(place_of(lua, "#PLAYER3#")[0] == "buggy" and place_of(lua, "#PLAYER4#")[0] == "buggy",
          "other remote vehicle: P3 and P4 stay in the buggy, a vehicle of another remote")
    invariant(lua, "other remote vehicle")


def test_seat_of():
    """The mission waits ask coop_seat_of for the seat each remote got in the vehicle the script names."""
    lua = new_world(4)
    vehicles(lua, boat=5)
    run(lua, '''
        vehicle_enter_teleport(LOCAL_PLAYER, "boat", 0)
        vehicle_enter_teleport(REMOTE_PLAYER, "boat", 2)
        vehicle_enter_teleport("maero", "boat", 1)
        vehicle_enter_teleport("carlos", "boat", 3)''')
    check(lua.eval('coop_seat_of("boat", "#PLAYER2#", 2)') == 2, "seat of: the first remote has the scripted seat")
    for remote in ("#PLAYER3#", "#PLAYER4#"):
        vehicle, seat = place_of(lua, remote)
        answer = lua.eval(f'coop_seat_of("boat", "{remote}", 9)')
        check(answer == (seat if vehicle == "boat" else None), f"seat of: {remote} has its seat in the boat, or none when it stayed on foot")
    check(lua.eval('coop_all_seated(function(p, v) return p == "#PLAYER2#" end, "boat")') is False, "seat of: a seated remote that fails the wait fails all seated")
    lua = new_world(4)
    vehicles(lua, buggy=4, boat=1)
    run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "buggy", 0)')
    run(lua, 'vehicle_enter_teleport(REMOTE_PLAYER, "boat", 0)')
    check(lua.eval('coop_seat_of("boat", "#PLAYER3#", 0)') is None, "seat of: a remote placed in another vehicle has no seat in the one the script names")
    check(lua.eval('coop_all_seated(function(p, v) return p == "#PLAYER2#" end, "boat")') is True, "seat of: all seated asks only the remotes seated in that vehicle")


def main():
    for test in (test_two_players, test_without_seat_natives, test_candidates, test_speedboat, test_parcel_truck, test_bikes, test_cart, test_mirror,
                 test_forced_seats, test_unresolved_vehicle, test_mid_enter, test_other_remote_vehicle, test_seat_of):
        test()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
