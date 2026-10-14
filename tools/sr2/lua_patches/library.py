"""ug_lib.lua: the library the missions share. Its calls with REMOTE_PLAYER (mission_start_fade_*, teleport_coop, ...) are
covered by the wrappers; the partner lookup is the one function that compares the name."""
from lua_patch import Patch

PATCHES = {
    "ug_lib.lua": Patch(
        edits=[
            (
                "   elseif ( player_name == REMOTE_PLAYER ) then\n      return LOCAL_PLAYER\n",
                "   elseif ( coop_is_remote_player(player_name) ) then\n      return LOCAL_PLAYER\n",
                1,
            ),
        ],
        generic=False,
    ),
}
