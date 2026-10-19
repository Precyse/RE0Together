"""Saints story missions (ss01 to ss11). Line numbers are those of the archive's script."""
from lua_patch import Patch
from .slots import reserve_seats

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "
COMMITTED = "Group_committed_to_kill"
UNSAFE_ZONE = "Players_in_unsafe_zone"
FOLLOWER_THREADS = "Follower_distance_check_threads"
IN_STARTING_BOX = "Players_in_starting_box"
IN_ANNEX = "Players_in_annex"
IN_DEFENSE_AREA = "Players_in_defense_area"
# ss06 objective slots of the remote players: the mission's slot 2 (REMOTE_PLAYER_INDEX) and the free slot 3; P4 shares P2's
SS06_REMOTE_OBJECTIVE_SLOTS = "{ REMOTE_PLAYER_INDEX, 3 }"

PATCHES = {
    "ss01.lua": Patch(
        lines=[
            (641, f"{COMMITTED}[REMOTE_PLAYER] == group_index", f"coop_first_remote_equal({COMMITTED}, group_index) ~= nil"),
            (643, "REMOTE_PLAYER", f"coop_first_remote_equal({COMMITTED}, group_index)"),
            (903, "{ REMOTE_PLAYER, LOCAL_PLAYER }", "coop_all_players()"),
            (
                1312, "ss01_get_closest_nav_to_char( group_locations, REMOTE_PLAYER )",
                "coop_best_remote( ss01_get_closest_nav_to_char, 3, group_locations, REMOTE_PLAYER )",
            ),
        ],
        blocks=[
            (
                776, 785, "local other_player = LOCAL_PLAYER",
                "local sync_type = sync_from_player( player_name )\n"
                f"if ( coop_any_other_equal( {COMMITTED}, player_name, group_index ) ) then\n"
                "   sync_type = SYNC_ALL\n"
                "end",
            ),
            (
                856, 863, f"{COMMITTED}[REMOTE_PLAYER]",
                "for i, p in pairs( coop_remote_players() ) do\n"
                f"\tif ( {COMMITTED}[p] == group_index ) then\n"
                "\t\tif ( Failure_thread_handles[p] ~= nil ) then\n"
                '\t\t\tmission_debug( "stopped failure timer for "..p )\n'
                "\t\t\tthread_kill( Failure_thread_handles[p] )\n"
                "\t\t\tFailure_thread_handles[p] = nil\n"
                "\t\tend\n"
                f"\t\t{COMMITTED}[p] = nil\n"
                "\tend\n"
                "end",
            ),
        ],
    ),
    "ss02.lua": Patch(
        lines=[
            (332, f"{UNSAFE_ZONE}[REMOTE_PLAYER]", f"coop_all_remotes_in({UNSAFE_ZONE})"),
            (484, "Players_in_prison_district[REMOTE_PLAYER] == false", "not coop_any_remote_in(Players_in_prison_district)"),
            (866, f"{FOLLOWER_THREADS}[REMOTE_PLAYER]", f"{FOLLOWER_THREADS}[triggerer_name]"),
            (880, f"{FOLLOWER_THREADS}[REMOTE_PLAYER]", f"{FOLLOWER_THREADS}[triggerer_name]"),
            (881, f"{FOLLOWER_THREADS}[REMOTE_PLAYER]", f"{FOLLOWER_THREADS}[triggerer_name]"),
            (882, f"{FOLLOWER_THREADS}[REMOTE_PLAYER]", f"{FOLLOWER_THREADS}[triggerer_name]"),
        ],
        blocks=[
            (
                692, 697, "local other_player = LOCAL_PLAYER",
                "if ( not coop_any_other( ss02_player_can_see_stairwell_guards, Laura_leader_name ) ) then",
            ),
            (
                704, 704, "if ( ss02_player_can_see_respawned_cafeteria_guards( other_player ) == false ) then",
                "if ( not coop_any_other( ss02_player_can_see_respawned_cafeteria_guards, Laura_leader_name ) ) then",
            ),
            (1317, 1320, f"{UNSAFE_ZONE}[REMOTE_PLAYER]", f"coop_kill_remotes_in({UNSAFE_ZONE})"),
        ],
    ),
    "ss03.lua": Patch(
        lines=[
            reserve_seats(577, "vehicle_enter_teleport( REMOTE_PLAYER, HELICOPTER_NAME, 3, true )", "HELICOPTER_NAME", (0, 2)),
        ],
    ),
    "ss05.lua": Patch(
        lines=[
            (409, "Players_in_Lab3_Area[REMOTE_PLAYER]", "coop_all_remotes_in(Players_in_Lab3_Area)"),
            (495, "ss05_player_in_lab4_first_half( REMOTE_PLAYER )", "coop_all_remotes( ss05_player_in_lab4_first_half, REMOTE_PLAYER )"),
        ],
    ),
    "ss06.lua": Patch(
        lines=[
            (424, "Players_in_starting_box = { true, true }", f"{IN_STARTING_BOX} = coop_player_table( true, true )"),
            (425, "Players_in_annex = { false, false }", f"{IN_ANNEX} = coop_player_table( false, false )"),
            (427, "Players_in_defense_area = { false, false }", f"{IN_DEFENSE_AREA} = coop_player_table( false, false )"),
            (1368, f"{IN_DEFENSE_AREA}[LOCAL_PLAYER_INDEX] == false", f"{IN_DEFENSE_AREA}[LOCAL_PLAYER] == false"),
            (
                1676, "player_index = REMOTE_PLAYER_INDEX",
                f"player_index = coop_hud_slot( attacker_name, LOCAL_PLAYER_INDEX, {SS06_REMOTE_OBJECTIVE_SLOTS} ) "
                'coop_trace("ss06 kills " .. attacker_name .. " slot " .. player_index)',
            ),
            (2028, f"{IN_STARTING_BOX}[LOCAL_PLAYER_INDEX]", f"{IN_STARTING_BOX}[LOCAL_PLAYER]"),
            (2030, f"{IN_STARTING_BOX}[REMOTE_PLAYER_INDEX]", f"{IN_STARTING_BOX}[human_name]"),
            (2036, f"{IN_ANNEX}[LOCAL_PLAYER_INDEX]", f"{IN_ANNEX}[LOCAL_PLAYER]"),
            (2038, f"{IN_ANNEX}[REMOTE_PLAYER_INDEX]", f"{IN_ANNEX}[human_name]"),
            (2067, f"{IN_DEFENSE_AREA}[LOCAL_PLAYER_INDEX]", f"{IN_DEFENSE_AREA}[LOCAL_PLAYER]"),
            (2069, f"{IN_DEFENSE_AREA}[REMOTE_PLAYER_INDEX]", f"{IN_DEFENSE_AREA}[human_name]"),
            (2084, f"{IN_DEFENSE_AREA}[LOCAL_PLAYER_INDEX]", f"{IN_DEFENSE_AREA}[LOCAL_PLAYER]"),
            (2086, f"{IN_DEFENSE_AREA}[REMOTE_PLAYER_INDEX]", f"{IN_DEFENSE_AREA}[human_name]"),
        ],
        blocks=[
            (
                1371, 1373, f"if ( coop_is_active() and {IN_DEFENSE_AREA}[REMOTE_PLAYER_INDEX] == false ) then",
                "if ( coop_is_active() ) then\n"
                "\tfor i, p in pairs( coop_remote_players() ) do\n"
                f"\t\tif ( {IN_DEFENSE_AREA}[p] == false ) then\n"
                '\t\t\tPlayer_failure_timer_thread_handles[p] = coop_native(thread_new)( "ss06_get_to_hq_or_fail", p )\n'
                "\t\tend\n"
                "\tend\n"
                "end",
            ),
            (
                1449, 1465, "local player_without_shaundi = LOCAL_PLAYER",
                "-- Every player but the one with Shaundi could be in place to see Pierce spawn\n"
                "local watchers = coop_other_players( Player_with_Shaundi )\n"
                "\n"
                "-- We want the index of the point furthest from them so that none of them sees Pierce spawn\n"
                "local warp_point_distance = coop_nearest_dist_of( watchers, PIERCE_SPAWN_LOCATIONS[warp_point_index] )\n"
                "\n"
                "-- Find the point furthest away from them\n"
                "for index, location_name in pairs( PIERCE_SPAWN_LOCATIONS ) do\n"
                "   local cur_point_distance = coop_nearest_dist_of( watchers, location_name )\n"
                "\n"
                "   if ( cur_point_distance > warp_point_distance ) then\n"
                "      warp_point_distance = cur_point_distance\n"
                "      warp_point_index = index\n"
                "   end\n"
                "end",
            ),
            (
                1761, 1786, "if ( trigger_name == START_TRIGGER_ANNEX_NAME ) then",
                "if ( human_name == LOCAL_PLAYER or coop_is_remote_player( human_name ) ) then\n"
                "   if ( trigger_name == START_TRIGGER_ANNEX_NAME ) then\n"
                f"      {IN_ANNEX}[human_name] = false\n"
                "   elseif ( trigger_name == START_TRIGGER_REGION_NAME ) then\n"
                f"      {IN_STARTING_BOX}[human_name] = false\n"
                "   end\n"
                "end\n"
                "\n"
                "-- Check to see if a player is outside both boxes (the remote players only in coop)\n"
                "local player_completely_left = false\n"
                f"if ( {IN_STARTING_BOX}[LOCAL_PLAYER] == false and\n"
                f"    {IN_ANNEX}[LOCAL_PLAYER] == false ) then\n"
                "   player_completely_left = true\n"
                "elseif ( coop_is_active() ) then\n"
                "   for i, p in pairs( coop_remote_players() ) do\n"
                f"      if ( {IN_STARTING_BOX}[p] == false and {IN_ANNEX}[p] == false ) then\n"
                "         player_completely_left = true\n"
                "      end\n"
                "   end\n"
                "end",
            ),
            (
                1966, 1967, "objective_text_clear( LOCAL_PLAYER_INDEX )",
                "objective_text_clear( LOCAL_PLAYER_INDEX )\n"
                f"coop_each_remote_slot( objective_text_clear, coop_hud_slots( LOCAL_PLAYER_INDEX, {SS06_REMOTE_OBJECTIVE_SLOTS} ) )",
            ),
        ],
    ),
    "ss07.lua": Patch(
        lines=[
            (279, "in_vehicle_status[REMOTE_PLAYER] == true", "coop_all_remotes_equal(in_vehicle_status, true)"),
            reserve_seats(362, "vehicle_enter_teleport( REMOTE_PLAYER, RAIL_CAR_ONE_NAME, 3, true )", "RAIL_CAR_ONE_NAME", (0, 2)),
            reserve_seats(407, "vehicle_enter_teleport( REMOTE_PLAYER, RAIL_BOAT_NAME, 2 )", "RAIL_BOAT_NAME", (0, 1)),
            reserve_seats(447, "vehicle_enter_teleport( REMOTE_PLAYER, RAIL_CAR_TWO_NAME, 3, false )", "RAIL_CAR_TWO_NAME", (0, 2)),
            reserve_seats(724, "player_force_vehicle_seat( REMOTE_PLAYER, 2, RAIL_BOAT_NAME )", "RAIL_BOAT_NAME", (0, 1)),
            reserve_seats(775, "player_force_vehicle_seat( REMOTE_PLAYER, 3, RAIL_CAR_TWO_NAME )", "RAIL_CAR_TWO_NAME", (0, 2)),
        ],
    ),
    "ss08.lua": Patch(
        lines=[
            (338, "player_conditions[REMOTE_PLAYER]", "coop_all_remotes_in(player_conditions)"),
            (351, "player_conditions[REMOTE_PLAYER] == false", "coop_all_remotes_equal(player_conditions, false)"),
            (1164, f"{IN_VEHICLE}( REMOTE_PLAYER )", f"coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER )"),
            (
                1165, "Players_in_vehicle[REMOTE_PLAYER] = true",
                FOR_EACH_REMOTE + f"Players_in_vehicle[p] = Players_in_vehicle[p] or COOP_NATIVE.{IN_VEHICLE}( p ) end",
            ),
            (
                1735, f"{NAV_IN_FOV}( navpoint_name, REMOTE_PLAYER ) == false",
                f"coop_any_remote( {NAV_IN_FOV}, navpoint_name, REMOTE_PLAYER ) == false",
            ),
        ],
        blocks=[
            (
                1171, 1173, "get_dist( REMOTE_PLAYER, DRUG_TRUCK )",
                "if ( coop_is_active() ) then\n"
                "\tcoop_mark_remotes_near( Players_in_range_of_drug_truck, get_dist, DRUG_TRUCK, DRUG_TRUCK_MAX_DISTANCE_METERS )\n"
                "end",
            ),
        ],
        each=("ss08_stop_failure_countdown",),
    ),
    "ss09.lua": Patch(
        lines=[
            (265, "Players_on_top_platform[REMOTE_PLAYER] == false", "not coop_any_remote_in(Players_on_top_platform)"),
            (399, "REMOTE_PLAYER", "coop_random_remote()"),
        ],
        each=("ss09_do_voodoo_damage",),
    ),
    "ss10.lua": Patch(
        lines=[
            (449, "ss10_player_in_station( REMOTE_PLAYER ) == false", "coop_any_remote( ss10_player_in_station, REMOTE_PLAYER ) == false"),
            (941, f"{IN_VEHICLE}( REMOTE_PLAYER ) == false", f"coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER ) == false"),
            reserve_seats(946, "vehicle_enter_teleport( REMOTE_PLAYER, REPAIR_VAN_NAME, 2 )", "REPAIR_VAN_NAME", (0,)),
            (1857, "Players_in_repair_van[REMOTE_PLAYER]", "coop_all_remotes_in(Players_in_repair_van)"),
            (
                1894, "ss10_player_has_weapon_equipped_in_station( REMOTE_PLAYER )",
                "coop_any_remote( ss10_player_has_weapon_equipped_in_station, REMOTE_PLAYER )",
            ),
        ],
        each=("ss10_notoriety_event",),
    ),
    "ss11.lua": Patch(
        lines=[
            (372, "Players_in_arena[REMOTE_PLAYER]", "coop_all_remotes_in(Players_in_arena)"),
            (849, f"{NAV_IN_FOV}( VEHICLE_SHOW_VEHICLES[vehicle_index], REMOTE_PLAYER )", f"coop_any_remote( {NAV_IN_FOV}, VEHICLE_SHOW_VEHICLES[vehicle_index], REMOTE_PLAYER )"),
            (1383, "REMOTE_PLAYER", "coop_random_remote()"),
            (1568, "Players_went_underground[REMOTE_PLAYER] == true", "coop_all_remotes_equal(Players_went_underground, true)"),
        ],
    ),
}
