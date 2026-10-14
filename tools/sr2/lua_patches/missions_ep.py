"""Epilogue story missions (ep01 to ep04). Line numbers are those of the archive's script."""
from lua_patch import Patch

SEQUENCE_BEGUN = "Player_sequence_begun"
IN_VEHICLE = "character_is_in_vehicle"

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
        ],
        each=("get_new_player_waypoint",),
    ),
    "ep02.lua": Patch(each=("update_confessional_gps", "ep02_update_waypoint")),
    "ep03.lua": Patch(
        lines=[
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
