"""Ronin story missions (rn01 to rn11). Line numbers are those of the archive's script (rn07: Juiced's copy)."""
from lua_patch import Patch
from .slots import REMOTE_HUD_SLOTS

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
OCCUPIED = "Rn10_player_defense_objectives_occupied"
OCCUPIED_AT_CHECKPOINT = "Rn10_player_defense_objectives_occupied_at_checkpoint"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "
REMOTE_LOCATION = "Remote_player_location"

PATCHES = {
    "rn01.lua": Patch(
        lines=[
            (680, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER, radius)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER, radius)"),
            (
                1616, "rn01_wait_for_vehicle_entry(REMOTE_PLAYER, VEHICLE_TRUCK, 1)",
                "for k, p in ipairs( coop_remote_players() ) do local seat = coop_seat_of( VEHICLE_TRUCK, p, 1 ) "
                "if ( seat ~= nil ) then rn01_wait_for_vehicle_entry(p, VEHICLE_TRUCK, seat) end end",
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
                "for k, p in ipairs( coop_remote_players() ) do local seat = coop_seat_of( VEHICLE_PLAYER_CAR, p, 2 ) "
                "if ( seat ~= nil ) then rn02_wait_for_vehicle_entry(p, VEHICLE_PLAYER_CAR, seat) end end",
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
            (
                934, 936, "character_is_in_a_driver_seat(REMOTE_PLAYER)",
                "for i, p in pairs( coop_remote_players() ) do\n"
                "\tif (not COOP_NATIVE.character_is_in_a_driver_seat(p)) then\n"
                "\t\tCOOP_NATIVE.inv_item_equip(FREE_WEAPON_GUN, p)\n"
                "\tend\n"
                "end",
            ),
        ],
        each=("rn02_update_player_prompts",),
    ),
    "rn03.lua": Patch(
        lines=[
            (
                425, "(Player_vehicles[player] == Player_vehicles[rn03_get_other_player(player)] )",
                "coop_all_in_vehicle_of(Player_vehicles, player)",
            ),
        ],
        each=("update_bike_gps",),
    ),
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
            (467, '"other dimension"', 'coop_player_table("other dimension", "other dimension")'),
            (1206, "thread_new(", "coop_native(thread_new)("),
            (1211, "Helicopter_entered[REMOTE_PLAYER]", "coop_any_remote_equal(Helicopter_entered, true)"),
            (1597, "Helicopter_entered[REMOTE_PLAYER] = true", "coop_set_remotes(Helicopter_entered, true)"),
            (1419, f"return {REMOTE_LOCATION}", f"return {REMOTE_LOCATION}[target]"),
            (
                1591, f"{REMOTE_LOCATION} = ENTRANCE_TARGET_AREAS[trigger]",
                f"{REMOTE_LOCATION}[triggerer] = ENTRANCE_TARGET_AREAS[trigger] "
                'coop_trace("rn07 location " .. triggerer .. " " .. tostring(ENTRANCE_TARGET_AREAS[trigger]))',
            ),
            (2250, f"{IN_VEHICLE}(REMOTE_PLAYER, VEHICLE_HELICOPTER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, VEHICLE_HELICOPTER)"),
        ],
        blocks=[
            (714, 714, REMOTE_LOCATION, f'coop_set_remotes({REMOTE_LOCATION}, "floor_2_area_1")'),
            (
                1286, 1288, f"if ( IN_COOP and patrol_areas[{REMOTE_LOCATION}] == true) then",
                "local present_remotes = {}\n"
                "if ( IN_COOP ) then\n"
                "\tfor i, p in pairs( coop_remote_players() ) do\n"
                f"\t\tif ( patrol_areas[{REMOTE_LOCATION}[p]] == true ) then\n"
                "\t\t\tpresent_remotes[coop_count(present_remotes) + 1] = p\n"
                "\t\tend\n"
                "\tend\n"
                "end\n"
                "remote_present = coop_count(present_remotes) > 0",
            ),
            (
                1300, 1311, "if (local_present and remote_present) then",
                "local remote_count = coop_count(present_remotes)\n"
                "if (local_present and remote_present) then\n"
                "\tlocal coin_toss = rand_int(1, remote_count + 1)\n"
                "\tif(coin_toss == 1) then\n"
                "\t\ttarget = LOCAL_PLAYER\n"
                "\telse\n"
                "\t\ttarget = present_remotes[coin_toss - 1]\n"
                "\tend\n"
                "elseif (local_present) then\n"
                "\ttarget = LOCAL_PLAYER\n"
                "elseif (remote_present) then\n"
                "\ttarget = present_remotes[1]\n"
                "\tif (remote_count > 1) then\n"
                "\t\ttarget = present_remotes[rand_int(1, remote_count)]\n"
                "\tend\n"
                "end\n"
                'coop_trace("rn07 lieutenant target " .. tostring(target))',
            ),
            (
                1450, 1460, "if(player_target == LOCAL_PLAYER) then",
                "player_target = coop_next_player(player_target)\n"
                "if (not LTNT_DEBUGGED) then\n"
                '\tmission_debug(lieutenant .. " switching to target " .. player_target, -1)\n'
                "end",
            ),
            (
                1512, 1551, "local valid_local_player_spawn_areas = safe_spawn_areas[Local_player_location]",
                "-- The spawn areas listed for every player on the floor: the local player's list, then each remote's\n"
                "local lists = {}\n"
                "if (safe_spawn_areas[Local_player_location]) then\n"
                "\tlists[1] = safe_spawn_areas[Local_player_location]\n"
                "end\n"
                "for i, p in pairs( coop_remote_players() ) do\n"
                f"\tif (safe_spawn_areas[{REMOTE_LOCATION}[p]]) then\n"
                f"\t\tlists[coop_count(lists) + 1] = safe_spawn_areas[{REMOTE_LOCATION}[p]]\n"
                "\tend\n"
                "end\n"
                "local valid_areas = {}\n"
                "\n"
                "-- If no player is on the floor, return nil\n"
                "local list_count = coop_count(lists)\n"
                "if (list_count == 0) then\n"
                "\treturn nil\n"
                "end\n"
                "\n"
                "-- Only the areas of the last list that every other list has too\n"
                "for i,area in pairs(lists[list_count]) do\n"
                "\tlocal in_every_list = true\n"
                "\tfor k = 1, list_count - 1 do\n"
                "\t\tlocal found = false\n"
                "\t\tfor j,other in pairs(lists[k]) do\n"
                "\t\t\tif (other == area) then\n"
                "\t\t\t\tfound = true\n"
                "\t\t\tend\n"
                "\t\tend\n"
                "\t\tin_every_list = in_every_list and found\n"
                "\tend\n"
                "\tif (in_every_list) then\n"
                "\t\tvalid_areas[area] = area\n"
                "\tend\n"
                "end",
            ),
        ],
    ),
    "rn08.lua": Patch(
        lines=[(1202, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER)")],
    ),
    "rn09.lua": Patch(
        lines=[
            (
                534, "rn09_character_in_vehicle(REMOTE_PLAYER,VEHICLE_REMOTE_PLAYER_MOTORCYCLE)",
                "coop_all_seated(rn09_character_in_vehicle, VEHICLE_REMOTE_PLAYER_MOTORCYCLE)",
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
            (887, "local player_timer = {[LOCAL_PLAYER] = 0, [REMOTE_PLAYER] = 1}", f"local player_timer = coop_hud_slots(0, {REMOTE_HUD_SLOTS})"),
            (
                903, 'hud_timer_set(player_timer[player], allowed_time_ms,"rn11_failure_wong")',
                'hud_timer_set(player_timer[player], allowed_time_ms,"rn11_failure_wong") '
                'coop_trace("rn11 out of range " .. player .. " timer " .. player_timer[player])',
            ),
            (1047, "hud_timer_stop(1)", f"coop_each_remote_slot(hud_timer_stop, coop_hud_slots(0, {REMOTE_HUD_SLOTS}))"),
            (402, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (813, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (1659, f"{NAV_IN_FOV}(navpoint, REMOTE_PLAYER)", f"coop_any_remote({NAV_IN_FOV}, navpoint, REMOTE_PLAYER)"),
        ],
        each=("update_indicators", "combat_ready"),
    ),
}
