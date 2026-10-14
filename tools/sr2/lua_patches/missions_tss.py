"""Tutorial and Third Street Saints story missions (tss01 to tss04, tss03_demo), the Ultor mission em01 (Juiced's copy)
and the shared mission_globals. Line numbers are those of the archive's script (em01: Juiced's copy)."""
from lua_patch import Patch

IN_VEHICLE = "character_is_in_vehicle"
NAV_IN_FOV = "navpoint_in_player_fov"
FOR_EACH_REMOTE = "for i, p in pairs( coop_remote_players() ) do "
MISSILE_TARGETS = "{LOCAL_PLAYER, CHARACTER_JULIUS, REMOTE_PLAYER}"
EXPANDED_TARGETS = "coop_expand_remote({LOCAL_PLAYER, CHARACTER_JULIUS, REMOTE_PLAYER})"

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
                "for k, p in ipairs( coop_remote_players() ) do em01_wait_for_vehicle_entry(p, JULIUS_VEHICLE, k + 1) end",
            ),
            (2500, MISSILE_TARGETS, EXPANDED_TARGETS),
            (2557, MISSILE_TARGETS, EXPANDED_TARGETS),
        ],
        each=("update_prompts",),
    ),
    "mission_globals.lua": Patch(),
    "tss01.lua": Patch(
        lines=[
            (1210, "get_dist(REMOTE_PLAYER, NAVP_ISLAND_CENTER)", "coop_farthest_remote_dist(get_dist, NAVP_ISLAND_CENTER)"),
            (2993, "inv_item_is_firearm_equipped(REMOTE_PLAYER)", "coop_any_remote(inv_item_is_firearm_equipped, REMOTE_PLAYER)"),
            (3631, "get_dist(LOCAL_PLAYER, REMOTE_PLAYER) > 10.0", "coop_any_remote_farther_than(get_dist, LOCAL_PLAYER, 10.0)"),
            (3949, f"{NAV_IN_FOV}(HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)", f"coop_any_remote({NAV_IN_FOV}, HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)"),
            (3958, f"{NAV_IN_FOV}(HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)", f"coop_any_remote({NAV_IN_FOV}, HELI_AMBUSH_VEHICLE, REMOTE_PLAYER, 5.0)"),
            (4154, f"{IN_VEHICLE}(REMOTE_PLAYER)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER)"),
            (4161, "get_dist(LOCAL_PLAYER, REMOTE_PLAYER) > 10.0", "coop_any_remote_farther_than(get_dist, LOCAL_PLAYER, 10.0)"),
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
            (483, "REMOTE_PLAYER", "coop_nearest_remote(Tss04_3_phase_table[i][CHARACTER_INDEX])"),
            (505, "get_dist_char_to_char(LOCAL_PLAYER, REMOTE_PLAYER) > 8.0", "coop_any_remote_farther_than(get_dist_char_to_char, LOCAL_PLAYER, 8.0)"),
            (1097, f"{IN_VEHICLE}(REMOTE_PLAYER, towtruck)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, towtruck)"),
            (1183, f"{IN_VEHICLE}(REMOTE_PLAYER, towtruck)", f"coop_any_remote({IN_VEHICLE}, REMOTE_PLAYER, towtruck)"),
            (
                1309, f"not {NAV_IN_FOV}(Tss04_tow_trucks[i], REMOTE_PLAYER, 3.0)",
                f"not coop_any_remote({NAV_IN_FOV}, Tss04_tow_trucks[i], REMOTE_PLAYER, 3.0)",
            ),
        ],
    ),
}
