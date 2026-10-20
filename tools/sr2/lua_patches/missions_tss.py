"""Tutorial and Third Street Saints story missions (tss01 to tss04, tss03_demo), the Ultor mission em01 (Juiced's copy)
and the shared mission_globals. Line numbers are those of the archive's script (em01: Juiced's copy)."""
from lua_patch import Patch
from .slots import REMOTE_HUD_SLOTS
from .waypoints import per_player_waypoints

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "
MISSILE_TARGETS = "{LOCAL_PLAYER, CHARACTER_JULIUS, REMOTE_PLAYER}"
EXPANDED_TARGETS = "coop_expand_remote({LOCAL_PLAYER, CHARACTER_JULIUS, REMOTE_PLAYER})"
REMOTE_ON_ROOFTOP = "REMOTE_PLAYER_ON_2ND_ROOFTOP"
REMOTE_JUMPS = "Tss04_player2_jump"
STUNT_OBJECTIVES = (
    "for i, p in pairs( coop_remote_players() ) do "
    f'objective_text(coop_hud_slot(p, 0, {REMOTE_HUD_SLOTS}), "tss04_stunt", {REMOTE_JUMPS}[p], Num_tss04_stunt_jumps, SYNC_REMOTE) end'
)
TOW_TRUCK_REMOTE = "coop_first_remote(character_is_in_vehicle, REMOTE_PLAYER, towtruck) or REMOTE_PLAYER"

PATCHES = {
    "em01.lua": Patch(
        lines=[
            (754, "player_was_inside[REMOTE_PLAYER] = IN_CHURCH[REMOTE_PLAYER]", FOR_EACH_REMOTE + "player_was_inside[p] = IN_CHURCH[p] end"),
            (894, "not npc_in_player_fov(human, REMOTE_PLAYER)", "not coop_any_remote(npc_in_player_fov, human, REMOTE_PLAYER)"),
            (960, "get_dist_char_to_nav(REMOTE_PLAYER, CHURCH_LEASH_INSIDE)", "coop_farthest_remote_dist(get_dist_char_to_nav, CHURCH_LEASH_INSIDE)"),
            (1467, MISSILE_TARGETS, EXPANDED_TARGETS),
            (
                1660, "(get_dist_char_to_vehicle(REMOTE_PLAYER, JULIUS_VEHICLE) <= 10.0)",
                "(not coop_any_remote_farther_than(get_dist_char_to_vehicle, JULIUS_VEHICLE, 10.0))",
            ),
            (1666, "character_is_dead(REMOTE_PLAYER)", "coop_any_remote(character_is_dead, REMOTE_PLAYER)"),
            (
                1749, "em01_wait_for_vehicle_entry(REMOTE_PLAYER, JULIUS_VEHICLE, 2)",
                "for k, p in ipairs( coop_remote_players() ) do local seat = coop_seat_of( JULIUS_VEHICLE, p, 2 ) "
                "if ( seat ~= nil ) then em01_wait_for_vehicle_entry(p, JULIUS_VEHICLE, seat) end end",
            ),
            (2500, MISSILE_TARGETS, EXPANDED_TARGETS),
            (2557, MISSILE_TARGETS, EXPANDED_TARGETS),
        ],
        each=("update_prompts",),
    ),
    "mission_globals.lua": Patch(),
    "tss01.lua": Patch(
        lines=[
            (189, "{false, false}", "coop_player_table(false, false)"),
            (190, "{false, false}", "coop_player_table(false, false)"),
            (368, "= false", "= coop_player_table(false, false)"),
            (
                551, "(player_vehicle[player] ~= player_vehicle[OTHER_PLAYER[player]])",
                "(not coop_any_other_equal(player_vehicle, player, player_vehicle[player]))",
            ),
            (561, "(player_vehicle[OTHER_PLAYER[player]] == new_vehicle)", "coop_any_other_equal(player_vehicle, player, new_vehicle)"),
            (2109, f"{REMOTE_ON_ROOFTOP} = false", f"{REMOTE_ON_ROOFTOP}[human] = false"),
            (2121, f"{REMOTE_ON_ROOFTOP} = true", f"{REMOTE_ON_ROOFTOP}[human] = true"),
            (
                2128, f"(LOCAL_PLAYER_ON_2ND_ROOFTOP and {REMOTE_ON_ROOFTOP})",
                f"(LOCAL_PLAYER_ON_2ND_ROOFTOP and coop_all_remotes_in({REMOTE_ON_ROOFTOP}))",
            ),
            (
                2822, 'hud_timer_set(hud_index, 8 * 1000, "tss01_searchlight_timer_elapsed", true, hud_sync)',
                'hud_timer_set(hud_index, 8 * 1000, "tss01_searchlight_timer_elapsed", true, hud_sync) '
                'coop_trace("tss01 searchlight " .. tostring(human) .. " timer " .. hud_index)',
            ),
            (1210, "get_dist(REMOTE_PLAYER, NAVP_ISLAND_CENTER)", "coop_farthest_remote_dist(get_dist, NAVP_ISLAND_CENTER)"),
            (2993, "inv_item_is_firearm_equipped(REMOTE_PLAYER)", "coop_any_remote(inv_item_is_firearm_equipped, REMOTE_PLAYER)"),
            (3631, "get_dist(LOCAL_PLAYER, REMOTE_PLAYER) > 10.0", "coop_any_remote_farther_than(get_dist, LOCAL_PLAYER, 10.0)"),
            (3949, f"{NAV_IN_FOV}(HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)", f"coop_any_remote({NAV_IN_FOV}, HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)"),
            (3958, f"{NAV_IN_FOV}(HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)", f"coop_any_remote({NAV_IN_FOV}, HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)"),
            (4154, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (4161, "get_dist(LOCAL_PLAYER, REMOTE_PLAYER) > 10.0", "coop_any_remote_farther_than(get_dist, LOCAL_PLAYER, 10.0)"),
        ],
        blocks=[
            (
                2794, 2802, "local\t\tstate_idx = 1",
                "local\t\tstate_idx = LOCAL_PLAYER\n"
                "local\t\thud_index = 0\n"
                "local\t\thud_sync = SYNC_LOCAL\n"
                "\n"
                "if (coop_is_remote_player(human)) then\n"
                "\tstate_idx = human\n"
                f"\thud_index = coop_hud_slot(human, 0, {REMOTE_HUD_SLOTS})\n"
                "\thud_sync = SYNC_REMOTE\n"
                "end",
            ),
        ],
        each=("update_player_vehicle_health", "update_player"),
    ),
    "tss02.lua": Patch(
        lines=[
            (223, "REMOTE_PLAYER", "coop_nearest_remote(CHARACTER_GAT)"),
            (
                406, 'Tss02_clothes_thread[REMOTE_PLAYER] = thread_new("tss02_change_clothes", REMOTE_PLAYER)',
                FOR_EACH_REMOTE + 'Tss02_clothes_thread[p] = coop_native(thread_new)("tss02_change_clothes", p) end',
            ),
            (732, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (735, "get_dist(REMOTE_PLAYER, TRIGGER_TIMER_START) > 100", "coop_any_remote_farther_than(get_dist, TRIGGER_TIMER_START, 100)"),
            (752, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (1322, "get_dist(REMOTE_PLAYER, TRIGGER_MISSION_COMPLETE) < 10.0", "not coop_any_remote_farther_than(get_dist, TRIGGER_MISSION_COMPLETE, 10.0)"),
        ] + per_player_waypoints("player", removes=(434, 483), adds=(424, 477)),
        blocks=[
            (
                494, 495, "thread_kill(Tss02_clothes_thread[OTHER_PLAYER[player]])",
                "for i, p in pairs( coop_other_players( player ) ) do\n"
                "\tthread_kill(Tss02_clothes_thread[p])\n"
                "\tthread_kill(Tss02_clothes_nag_thread[p])\n"
                "end",
            ),
        ],
    ),
    "tss03.lua": Patch(
        lines=[
            (591, "los_check(human, REMOTE_PLAYER)", "coop_any_remote(los_check, human, REMOTE_PLAYER)"),
            (619, "character_has_human_shield(REMOTE_PLAYER)", "coop_any_remote(character_has_human_shield, REMOTE_PLAYER)"),
            (983, "not npc_in_player_fov(bum, REMOTE_PLAYER)", "not coop_any_remote(npc_in_player_fov, bum, REMOTE_PLAYER)"),
        ],
    ),
    "tss03_demo.lua": Patch(),
    "tss04.lua": Patch(
        lines=[
            (132, "= 0", "= coop_player_table(0, 0)"),
            (
                793, f'objective_text(1, "tss04_stunt", {REMOTE_JUMPS}, Num_tss04_stunt_jumps, SYNC_REMOTE)', STUNT_OBJECTIVES,
            ),
            (483, "REMOTE_PLAYER", "coop_nearest_remote(Tss04_3_phase_table[i][CHARACTER_INDEX])"),
            (505, "get_dist_char_to_char(LOCAL_PLAYER, REMOTE_PLAYER) > 8.0", "coop_any_remote_farther_than(get_dist_char_to_char, LOCAL_PLAYER, 8.0)"),
            (905, f"if ({REMOTE_JUMPS} < Num_tss04_stunt_jumps) then", f"if ({REMOTE_JUMPS}[human] < Num_tss04_stunt_jumps) then"),
            (913, f"({REMOTE_JUMPS} >= index)", f"coop_all_remotes_at_least({REMOTE_JUMPS}, index)"),
            (
                920, f'objective_text(1, "tss04_stunt", {REMOTE_JUMPS}, Num_tss04_stunt_jumps, SYNC_REMOTE)', STUNT_OBJECTIVES,
            ),
            (926, f"({REMOTE_JUMPS} == Num_tss04_stunt_jumps)", f"(coop_first_remote_equal({REMOTE_JUMPS}, Num_tss04_stunt_jumps) ~= nil)"),
            (
                927, "Tss04_coop_winner = REMOTE_PLAYER",
                f"Tss04_coop_winner = coop_first_remote_equal({REMOTE_JUMPS}, Num_tss04_stunt_jumps)",
            ),
            (932, f"({REMOTE_JUMPS} == Num_tss04_stunt_jumps)", f"coop_all_remotes_equal({REMOTE_JUMPS}, Num_tss04_stunt_jumps)"),
            (1097, f"{IN_VEHICLE}(REMOTE_PLAYER, towtruck)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, towtruck)"),
            (1106, "local player_name = REMOTE_PLAYER", f"local player_name = {TOW_TRUCK_REMOTE}"),
            (1183, f"{IN_VEHICLE}(REMOTE_PLAYER, towtruck)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, towtruck)"),
            (1192, "local player_name = REMOTE_PLAYER", f"local player_name = {TOW_TRUCK_REMOTE}"),
            (
                1309, f"not {NAV_IN_FOV}(Tss04_tow_trucks[i], REMOTE_PLAYER, 3.0)",
                f"not coop_any_remote({NAV_IN_FOV}, Tss04_tow_trucks[i], REMOTE_PLAYER, 3.0)",
            ),
        ],
        blocks=[
            (
                892, 901, f"if (index <= {REMOTE_JUMPS}) then",
                f"if (index <= {REMOTE_JUMPS}[human]) then\n"
                "\t-- We already went through these\n"
                "\treturn\n"
                f"elseif (index > ({REMOTE_JUMPS}[human] + 1)) then\n"
                "\t-- We cannot go through this one yet\n"
                "\treturn\n"
                "end\n"
                "\n"
                "-- Increment the count\n"
                f"{REMOTE_JUMPS}[human] = {REMOTE_JUMPS}[human] + 1\n"
                f'coop_trace("tss04 jump " .. human .. " " .. {REMOTE_JUMPS}[human])',
            ),
        ],
    ),
}
