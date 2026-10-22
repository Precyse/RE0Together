"""ug_lib.lua: the library the missions share. Its calls with REMOTE_PLAYER (mission_start_fade_*, teleport_coop, ...) are
covered by the wrappers; the partner lookup, sync_from_player (the help text target: every remote name is SYNC_REMOTE, M16 W2 SN1)
and get_dist_closest_player_to_object are patched."""
from lua_patch import Patch

PATCHES = {
    "ug_lib.lua": Patch(
        edits=[
            (
                "   elseif ( player_name == REMOTE_PLAYER ) then\n      return LOCAL_PLAYER\n",
                "   elseif ( coop_is_remote_player(player_name) ) then\n      return LOCAL_PLAYER\n",
                1,
            ),
            (
                "function sync_from_player(player_name)\n\tif player_name == REMOTE_PLAYER then\n",
                "function sync_from_player(player_name)\n\tif coop_is_remote_player(player_name) then\n",
                1,
            ),
            (
                "\tlocal closest_dist = get_dist(player_list[1], object)\n",
                "\tlocal closest_dist = coop_native(get_dist)(player_list[1], object)\n",
                1,
            ),
            (
                "\t\tlocal current_dist = get_dist(p, object)\n",
                "\t\tlocal current_dist = coop_native(get_dist)(p, object)\n",
                1,
            ),
        ],
        generic=False,
    ),
}
