"""Epilogue story missions (ep01 to ep04). Line numbers are those of the archive's script."""
from lua_patch import Patch
from .slots import REMOTE_HUD_SLOTS, reserve_seats

SEQUENCE_BEGUN = "Player_sequence_begun"
IN_VEHICLE = "character_is_in_vehicle"
SYNC_REMOTE_BRANCH = "if ( (sync == SYNC_REMOTE) or (sync == SYNC_ALL) ) then"


def remote_bars(first, last, sequence, body):
    """The block edit that runs `body` once for each remote player in `sequence`, each on its own bar (HUD_BAR[p]), in place
    of the single remote branch of lines first to last."""
    return (first, last, SYNC_REMOTE_BRANCH, f"for i, p in pairs( coop_remotes_equal( {SEQUENCE_BEGUN}, {sequence} ) ) do\n{body}\nend")


PATCHES = {
    "ep01.lua": Patch(
        lines=[
            (589, f'{SEQUENCE_BEGUN}[REMOTE_PLAYER] ~= ""', f'not coop_all_remotes_equal({SEQUENCE_BEGUN}, "")'),
            (696, f"{SEQUENCE_BEGUN}[REMOTE_PLAYER] == TRIGGER_SHAUNDI", f"coop_any_remote_equal({SEQUENCE_BEGUN}, TRIGGER_SHAUNDI)"),
            (780, "character_is_in_a_driver_seat(REMOTE_PLAYER)", "coop_any_remote(character_is_in_a_driver_seat, REMOTE_PLAYER)"),
            (809, f'{SEQUENCE_BEGUN}[REMOTE_PLAYER] ~= ""', f'not coop_all_remotes_equal({SEQUENCE_BEGUN}, "")'),
            (848, f"{SEQUENCE_BEGUN}[REMOTE_PLAYER] == trigger", f"coop_any_remote_equal({SEQUENCE_BEGUN}, trigger)"),
            (1355, "escort_attack(REMOTE_PLAYER)", "escort_attack(coop_nearest_remote(VEHICLE_SHAUNDI_VAN))"),
            (1954, f"{SEQUENCE_BEGUN}[REMOTE_PLAYER] == sequence", f"coop_any_remote_equal({SEQUENCE_BEGUN}, sequence)"),
            (188, "HUD_BAR = {[LOCAL_PLAYER] = 0, [REMOTE_PLAYER] = 1}", f"HUD_BAR = coop_hud_slots(0, {REMOTE_HUD_SLOTS})"),
            (
                971, 'thread_new("ep01_join_shaundi_monitor", OTHER_PLAYER[triggerer])',
                'for i, p in pairs( coop_other_players( triggerer ) ) do coop_native(thread_new)("ep01_join_shaundi_monitor", p) end',
            ),
            (
                1078, f"({SEQUENCE_BEGUN}[OTHER_PLAYER[player]] == TRIGGER_SHAUNDI)",
                f"coop_any_other_equal({SEQUENCE_BEGUN}, player, TRIGGER_SHAUNDI)",
            ),
            (1320, "(player_attackers[OTHER_PLAYER[player]] ~= escort)", "(not coop_any_other_equal(player_attackers, player, escort))"),
        ],
        blocks=[
            (
                895, 907, "local other_player = OTHER_PLAYER[triggerer]",
                "for i, other_player in pairs( coop_other_players( triggerer ) ) do\n"
                f"\tif ({SEQUENCE_BEGUN}[other_player] == \"\") then\n"
                "\t\tlocal dist = COOP_NATIVE.get_dist(other_player,trigger)\n"
                "\t\tif(dist < 300) then\n"
                "\t\t\tif (LTNT_TRIGGER_JOIN_FUNCTIONS[trigger] ~= \"\") then\n"
                "\t\t\t\tplayer_begin_sequence(other_player)\n"
                "\t\t\tend\n"
                "\t\tend\n"
                "\tend\n"
                "end",
            ),
            remote_bars(1203, 1205, "TRIGGER_SHAUNDI", "\thud_bar_off(HUD_BAR[p])"),
            remote_bars(1410, 1412, "TRIGGER_SHAUNDI", "\thud_bar_set_value(HUD_BAR[p],van_cur_hit_points, SYNC_REMOTE)"),
            remote_bars(1476, 1478, "TRIGGER_PIERCE", "\thud_bar_off(HUD_BAR[p])"),
            remote_bars(
                1703, 1708, "TRIGGER_PIERCE",
                "\tlocal hud_bar = HUD_BAR[p]\n"
                '\thud_bar_on(hud_bar, "Health", HELPTEXT_HUD_PIERCE_HEALTH, pierce_max_hit_points, SYNC_REMOTE)\n'
                "\thud_bar_set_range(hud_bar, 0, pierce_max_hit_points, SYNC_ALL)\n"
                "\thud_bar_set_value(hud_bar, pierce_start_hit_points, SYNC_REMOTE)\n"
                '\tcoop_trace("ep01 pierce bar " .. p .. " slot " .. hud_bar)',
            ),
            remote_bars(1780, 1782, "TRIGGER_PIERCE", "\thud_bar_set_value(HUD_BAR[p], new_health, SYNC_REMOTE )"),
            remote_bars(1791, 1793, "TRIGGER_PIERCE", "\thud_bar_off(HUD_BAR[p])"),
            remote_bars(1812, 1814, "TRIGGER_PIERCE", "\thud_bar_set_value(HUD_BAR[p], new_health, SYNC_REMOTE )"),
            (1880, 1882, "if (IN_COOP) then", "if (IN_COOP) then\n\tcoop_each_remote_slot(hud_bar_off, HUD_BAR)\nend"),
        ],
        each=("get_new_player_waypoint",),
    ),
    "ep02.lua": Patch(
        lines=[
            (
                276, "hud_timer_stop(timer)", 'hud_timer_stop(timer) coop_trace("ep02 in the row " .. player .. " timer " .. timer)',
            ),
            (
                298, 'hud_timer_set(timer, OUT_OF_ROW_TIME_MS,"ep02_failure_out_of_row", false, sync)',
                'hud_timer_set(timer, OUT_OF_ROW_TIME_MS,"ep02_failure_out_of_row", false, sync) '
                'coop_trace("ep02 out of the row " .. player .. " timer " .. timer)',
            ),
            (356, "hud_timer_stop(PLAYER_TIMER[REMOTE_PLAYER])", "coop_each_remote_slot(hud_timer_stop, PLAYER_TIMER)"),
            (960, "hud_timer_stop(PLAYER_TIMER[REMOTE_PLAYER])", "coop_each_remote_slot(hud_timer_stop, PLAYER_TIMER)"),
        ],
        blocks=[(141, 141, "PLAYER_TIMER", f"PLAYER_TIMER = coop_hud_slots(0, {REMOTE_HUD_SLOTS})")],
        each=("update_confessional_gps", "ep02_update_waypoint"),
    ),
    "ep03.lua": Patch(
        lines=[
            reserve_seats(344, "vehicle_enter_teleport(REMOTE_PLAYER, GETAWAY_BOAT, 1)", "GETAWAY_BOAT", (0,)),
            (170, "ep03_character_in_vehicle_of_correct_type( REMOTE_PLAYER )", "coop_any_remote( ep03_character_in_vehicle_of_correct_type, REMOTE_PLAYER )"),
            (517, f"{IN_VEHICLE}( REMOTE_PLAYER, GETAWAY_BOAT )", f"coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER, GETAWAY_BOAT )"),
        ],
    ),
    "ep04.lua": Patch(
        lines=[
            (389, "Players_in_helicopter[REMOTE_PLAYER] == false", "coop_all_remotes_equal(Players_in_helicopter, false)"),
            (602, 'inv_has_item("chainsaw", REMOTE_PLAYER)', 'coop_any_remote(inv_has_item, "chainsaw", REMOTE_PLAYER)'),
            (983, "Players_in_range_of_limo[REMOTE_PLAYER] == false", "coop_all_remotes_equal(Players_in_range_of_limo, false)"),
            (1062, f"{IN_VEHICLE}( REMOTE_PLAYER, ATTACK_HELICOPTER )", f"coop_any_remote( {IN_VEHICLE}, REMOTE_PLAYER, ATTACK_HELICOPTER )"),
        ],
        blocks=[
            (
                833, 835, "get_dist( REMOTE_PLAYER, VOGELS_LIMO_NAME )",
                "coop_mark_remotes_near(Players_in_range_of_limo, get_dist, VOGELS_LIMO_NAME, VOGELS_LIMO_MAX_DIST_METERS)",
            ),
        ],
    ),
}
