"""Saints story missions (ss01 to ss11). Line numbers are those of the archive's script."""
from lua_patch import Patch

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "
COMMITTED = "Group_committed_to_kill"
UNSAFE_ZONE = "Players_in_unsafe_zone"
FOLLOWER_THREADS = "Follower_distance_check_threads"

PATCHES = {
    "ss01.lua": Patch(
        lines=[
            (641, f"{COMMITTED}[REMOTE_PLAYER] == group_index", f"coop_first_remote_equal({COMMITTED}, group_index) ~= nil"),
            (643, "REMOTE_PLAYER", f"coop_first_remote_equal({COMMITTED}, group_index)"),
            (903, "{ REMOTE_PLAYER, LOCAL_PLAYER }", "coop_all_players()"),
        ],
        blocks=[
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
            (
                1311, 1320, "coop_is_active()",
                "if ( coop_is_active() ) then\n"
                "\tfor i, p in pairs( coop_remote_players() ) do\n"
                "\t\tlocal closest_nav_remote = coop_closest_navpoint( group_locations, p )\n"
                "\t\tif ( Cur_dealer_group_path_location[p] ~= closest_nav_remote ) then\n"
                "\t\t\tcoop_waypoint_remove_for( p, waypoint_remove, SYNC_REMOTE )\n"
                "\t\t\tcoop_waypoint_add_for( p, waypoint_add, closest_nav_remote, SYNC_REMOTE )\n"
                "\t\t\tCur_dealer_group_path_location[p] = closest_nav_remote\n"
                "\t\tend\n"
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
        blocks=[(1317, 1320, f"{UNSAFE_ZONE}[REMOTE_PLAYER]", f"coop_kill_remotes_in({UNSAFE_ZONE})")],
    ),
    "ss03.lua": Patch(),
    "ss05.lua": Patch(
        lines=[
            (409, "Players_in_Lab3_Area[REMOTE_PLAYER]", "coop_all_remotes_in(Players_in_Lab3_Area)"),
            (495, "ss05_player_in_lab4_first_half( REMOTE_PLAYER )", "coop_all_remotes( ss05_player_in_lab4_first_half, REMOTE_PLAYER )"),
        ],
    ),
    "ss06.lua": Patch(
        lines=[
            (
                1372, 'Player_failure_timer_thread_handles[REMOTE_PLAYER] = thread_new( "ss06_get_to_hq_or_fail", REMOTE_PLAYER )',
                FOR_EACH_REMOTE + 'Player_failure_timer_thread_handles[p] = coop_native(thread_new)( "ss06_get_to_hq_or_fail", p ) end',
            ),
        ],
    ),
    "ss07.lua": Patch(
        lines=[(279, "in_vehicle_status[REMOTE_PLAYER] == true", "coop_all_remotes_equal(in_vehicle_status, true)")],
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
