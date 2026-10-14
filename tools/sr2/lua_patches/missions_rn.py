"""Ronin story missions (rn01 to rn11). Line numbers are those of the archive's script (rn07: Juiced's copy)."""
from lua_patch import Patch

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
OCCUPIED = "Rn10_player_defense_objectives_occupied"
OCCUPIED_AT_CHECKPOINT = "Rn10_player_defense_objectives_occupied_at_checkpoint"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "

PATCHES = {
    "rn01.lua": Patch(
        lines=[
            (680, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER, radius)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER, radius)"),
            (
                1616, "rn01_wait_for_vehicle_entry(REMOTE_PLAYER, VEHICLE_TRUCK, 1)",
                "for k, p in ipairs( coop_remote_players() ) do rn01_wait_for_vehicle_entry(p, VEHICLE_TRUCK, k) end",
            ),
        ],
        each=("rn01_update_waypoint", "check_player_prereqs", "update_confessional_gps"),
    ),
    "rn02.lua": Patch(
        lines=[
            (356, "character_is_dead(REMOTE_PLAYER)", "coop_any_remote(character_is_dead, REMOTE_PLAYER)"),
            (403, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (500, f"{IN_VEHICLE}(REMOTE_PLAYER, VEHICLE_PLAYER_CAR)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, VEHICLE_PLAYER_CAR)"),
            (
                501, "Player_in_convertible[REMOTE_PLAYER] = true",
                FOR_EACH_REMOTE + f"Player_in_convertible[p] = COOP_NATIVE.{IN_VEHICLE}(p, VEHICLE_PLAYER_CAR) end",
            ),
            (
                651, 'wrong_car_threads[REMOTE_PLAYER] = thread_new("rn02_wrong_car_message", REMOTE_PLAYER)',
                FOR_EACH_REMOTE + 'wrong_car_threads[p] = coop_native(thread_new)("rn02_wrong_car_message", p) end',
            ),
            (
                1024, "rn02_wait_for_vehicle_entry(REMOTE_PLAYER, VEHICLE_PLAYER_CAR, 2)",
                "for k, p in ipairs( coop_remote_players() ) do rn02_wait_for_vehicle_entry(p, VEHICLE_PLAYER_CAR, k + 1) end",
            ),
            (1303, "not player_triggered_dealer(REMOTE_PLAYER)", "not coop_any_remote(player_triggered_dealer, REMOTE_PLAYER)"),
            (1791, "should_give_infinite_mass(REMOTE_PLAYER)", "coop_any_remote(should_give_infinite_mass, REMOTE_PLAYER)"),
        ],
        blocks=[
            (
                1191, 1193, "Current_repair_trigger[REMOTE_PLAYER]",
                "for i, p in pairs( coop_remote_players() ) do\n"
                '\tif (Current_repair_trigger[p] ~= "") then\n'
                "\t\tmarker_remove_navpoint(Current_repair_trigger[p], SYNC_REMOTE)\n"
                "\tend\n"
                "end",
            ),
        ],
        each=("rn02_update_player_prompts",),
    ),
    "rn03.lua": Patch(each=("update_bike_gps",)),
    "rn04.lua": Patch(
        lines=[
            (1420, "attack(ronin, REMOTE_PLAYER)", "attack(ronin, attacker)"),
            (1545, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER, radius)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER, radius)"),
        ],
    ),
    "rn05.lua": Patch(),
    "rn06.lua": Patch(each=("combat_ready",)),
    "rn07.lua": Patch(
        lines=[
            (1206, "thread_new(", "coop_native(thread_new)("),
            (1211, "Helicopter_entered[REMOTE_PLAYER]", "coop_any_remote_equal(Helicopter_entered, true)"),
            (1597, "Helicopter_entered[REMOTE_PLAYER] = true", "coop_set_remotes(Helicopter_entered, true)"),
            (2250, f"{IN_VEHICLE}(REMOTE_PLAYER, VEHICLE_HELICOPTER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, VEHICLE_HELICOPTER)"),
        ],
    ),
    "rn08.lua": Patch(
        lines=[(1202, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER)")],
    ),
    "rn09.lua": Patch(
        lines=[
            (
                534, "rn09_character_in_vehicle(REMOTE_PLAYER,VEHICLE_REMOTE_PLAYER_MOTORCYCLE)",
                "coop_all_remotes(rn09_character_in_vehicle, REMOTE_PLAYER, VEHICLE_REMOTE_PLAYER_MOTORCYCLE)",
            ),
        ],
    ),
    "rn10.lua": Patch(
        lines=[
            (427, f"for trigger, occupied in pairs({OCCUPIED}[REMOTE_PLAYER]) do", FOR_EACH_REMOTE + f"for trigger, occupied in pairs({OCCUPIED}[p]) do"),
            (428, f"{OCCUPIED_AT_CHECKPOINT}[REMOTE_PLAYER][trigger]", f"{OCCUPIED_AT_CHECKPOINT}[p][trigger]"),
            (429, "end\t\tmission_set_checkpoint", "end end\t\tmission_set_checkpoint"),
            (
                529, f"for trigger, occupied in pairs({OCCUPIED_AT_CHECKPOINT}[REMOTE_PLAYER]) do",
                FOR_EACH_REMOTE + f"for trigger, occupied in pairs({OCCUPIED_AT_CHECKPOINT}[p]) do",
            ),
            (530, f"{OCCUPIED}[REMOTE_PLAYER][trigger]", f"{OCCUPIED}[p][trigger]"),
            (531, "end", "end end"),
            (598, f"{OCCUPIED}[REMOTE_PLAYER][objective_index]", f"coop_any_remote_at({OCCUPIED}, objective_index)"),
            (796, "Player_active_defense_objective[REMOTE_PLAYER] == i", "coop_any_remote_equal(Player_active_defense_objective, i)"),
            (1284, "REMOTE_PLAYER", "coop_random_remote()"),
            (1749, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER, radius)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER, radius)"),
        ],
        blocks=[
            (
                725, 727, "Player_active_defense_objective[REMOTE_PLAYER]",
                "for i, p in pairs( coop_remote_players() ) do\n"
                "\tif (Player_active_defense_objective[p] == defense_objective_index) then\n"
                "\t\tPlayer_active_defense_objective[p] = NO_ACTIVE_DEFENSE_OBJECTIVE\n"
                "\tend\n"
                "end",
            ),
        ],
        each=("rn10_begin_defense_objective",),
    ),
    "rn11.lua": Patch(
        lines=[
            (402, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (813, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (1659, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER)"),
        ],
        each=("update_indicators", "combat_ready"),
    ),
}
