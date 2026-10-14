"""Brotherhood story missions (bh01 to bh11). Line numbers are those of the archive's script.
Not listed here: the tables, the name tests, the thread starts and every call the wrappers answer, unchanged."""
from lua_patch import Patch

BOAT_SPAWN = 'MP.."boat_spawn1"'
IN_VEHICLE = "character_is_in_vehicle"
HAS_SHIELD = "character_has_specific_human_shield"
RANGE = "Players_out_of_current_boat_range"

PATCHES = {
    "bh02.lua": Patch(
        lines=[(1053, "REMOTE_PLAYER", "coop_nearest_remote(attacker)")],
    ),
    "bh03.lua": Patch(
        lines=[
            (552, f"{IN_VEHICLE}( REMOTE_PLAYER, {BOAT_SPAWN} )", f"coop_first_remote( {IN_VEHICLE}, REMOTE_PLAYER, {BOAT_SPAWN} )"),
            (553, "( REMOTE_PLAYER )", f"( coop_first_remote( {IN_VEHICLE}, REMOTE_PLAYER, {BOAT_SPAWN} ) )"),
            (624, "bh03_player_in_water_travel_vehicle( REMOTE_PLAYER )", "coop_any_remote( bh03_player_in_water_travel_vehicle, REMOTE_PLAYER )"),
            (1099, "Players_near_helipad[REMOTE_PLAYER]", "coop_all_remotes_in(Players_near_helipad)"),
            (2134, f"{IN_VEHICLE}( REMOTE_PLAYER, ESCAPE_HELICOPTER )", f"coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER, ESCAPE_HELICOPTER )"),
        ],
        blocks=[
            (782, 785, f"{IN_VEHICLE}( REMOTE_PLAYER )", "coop_stop_remote_vehicles()"),
            (
                1316, 1319, "Player_failure_timers_threads[REMOTE_PLAYER]",
                "for i, p in pairs( coop_remote_players() ) do\n"
                "\tif ( Player_failure_timers_threads[p] ~= INVALID_THREAD_HANDLE ) then\n"
                "\t\tthread_kill( Player_failure_timers_threads[p] )\n"
                "\t\tPlayer_failure_timers_threads[p] = INVALID_THREAD_HANDLE\n"
                "\tend\n"
                "end",
            ),
        ],
    ),
    "bh04.lua": Patch(
        lines=[
            (399, "REMOTE_PLAYER", "coop_nearest_remote(CHAR_DONNIE)"),
            (476, f"{HAS_SHIELD}(REMOTE_PLAYER, CHAR_DONNIE)", f"coop_any_remote({HAS_SHIELD}, REMOTE_PLAYER, CHAR_DONNIE)"),
        ],
        each=("update_message",),
    ),
    "bh05.lua": Patch(
        lines=[
            (464, "Players_in_bank_interior[REMOTE_PLAYER]", "coop_all_remotes_in(Players_in_bank_interior)"),
            (478, "Players_in_bank_interior[REMOTE_PLAYER] == false", "not coop_any_remote_in(Players_in_bank_interior)"),
            (491, "Players_in_jessicas_car[REMOTE_PLAYER] == true", "coop_any_remote_in(Players_in_jessicas_car)"),
            (731, "Players_in_bank_interior[REMOTE_PLAYER] == false", "coop_first_remote_equal(Players_in_bank_interior, false) ~= nil"),
            (732, "REMOTE_PLAYER", "coop_first_remote_equal(Players_in_bank_interior, false)"),
            (911, f"not {IN_VEHICLE}( REMOTE_PLAYER, VEH_JESSICAS_CAR )", f"not coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER, VEH_JESSICAS_CAR )"),
            (916, "Players_in_jessicas_car[REMOTE_PLAYER] = true", "coop_set_remotes(Players_in_jessicas_car, true)"),
            (1101, f"{IN_VEHICLE}(REMOTE_PLAYER, VEH_JESSICAS_CAR)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, VEH_JESSICAS_CAR)"),
            (1106, f"{IN_VEHICLE}(REMOTE_PLAYER, VEH_JESSICAS_CAR)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, VEH_JESSICAS_CAR)"),
            (1119, f"{HAS_SHIELD}(REMOTE_PLAYER, CHAR_JESSICA)", f"coop_any_remote({HAS_SHIELD}, REMOTE_PLAYER, CHAR_JESSICA)"),
        ],
        blocks=[(1145, 1148, f"{IN_VEHICLE}( REMOTE_PLAYER )", "coop_stop_remote_vehicles()")],
    ),
    "bh07.lua": Patch(
        lines=[
            (590, f"{HAS_SHIELD}(REMOTE_PLAYER,npc)", f"coop_any_remote({HAS_SHIELD},REMOTE_PLAYER,npc)"),
            (839, f"{HAS_SHIELD}(REMOTE_PLAYER,npc)", f"coop_any_remote({HAS_SHIELD},REMOTE_PLAYER,npc)"),
            (850, f"{HAS_SHIELD}(REMOTE_PLAYER,npc)", f"coop_any_remote({HAS_SHIELD},REMOTE_PLAYER,npc)"),
        ],
    ),
    "bh08.lua": Patch(each=("update_bus_gps",)),
    "bh09.lua": Patch(
        lines=[
            (244, f"{RANGE}[REMOTE_PLAYER] == false", f"coop_all_remotes_equal({RANGE}, false)"),
            (446, f"{RANGE}[REMOTE_PLAYER] = false", f"coop_set_remotes({RANGE}, false)"),
            (
                1078, "get_dist_char_to_nav(REMOTE_PLAYER, NAV_SHIP) > distance_meters",
                "coop_any_remote_farther_than(get_dist_char_to_nav, NAV_SHIP, distance_meters)",
            ),
            (1084, f"{RANGE}[REMOTE_PLAYER] = true", f"coop_set_remotes({RANGE}, true)"),
            (
                1086, "get_dist_char_to_nav(REMOTE_PLAYER, NAV_SHIP) > distance_meters",
                "coop_any_remote_farther_than(get_dist_char_to_nav, NAV_SHIP, distance_meters)",
            ),
            (1090, f"{RANGE}[REMOTE_PLAYER] = false", f"coop_set_remotes({RANGE}, false)"),
        ],
        blocks=[
            (
                349, 351, "get_dist_char_to_nav(REMOTE_PLAYER, NAV_SHIP)",
                f"if ( IN_COOP ) then\n\tcoop_mark_remotes_near({RANGE}, get_dist_char_to_nav, NAV_SHIP, distance_meters, false)\nend",
            ),
        ],
    ),
    "bh10.lua": Patch(
        lines=[
            (946, "In_exploit_area[REMOTE_PLAYER][trigger] = false", "for i, p in pairs( coop_remote_players() ) do In_exploit_area[p][trigger] = false end"),
            (957, "In_spawn_area[REMOTE_PLAYER][trigger] = 0", "for i, p in pairs( coop_remote_players() ) do In_spawn_area[p][trigger] = 0 end"),
            (995, "In_exploit_area[REMOTE_PLAYER][exploit]", "coop_any_remote_at(In_exploit_area, exploit)"),
            (1007, "(In_spawn_area[REMOTE_PLAYER][trigger] > 0)", "coop_any_remote_above(In_spawn_area, trigger, 0)"),
        ],
    ),
    "bh11.lua": Patch(
        lines=[
            (443, "thread_new(", "coop_native(thread_new)("),
            (505, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (524, "thread_new(", "coop_native(thread_new)("),
            (528, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
        ],
    ),
}
