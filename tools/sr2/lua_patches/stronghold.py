"""Stronghold scripts: the two-player tests become remote-player helpers (tools/sr2/lua/coop_helpers.lua).
The rest of the strongholds is covered by the wrappers of tools/sr2/lua/coop_wrappers.lua alone (crack house vehicle flags,
trailer park seats, ugmall teleport_coop)."""
from lua_patch import Patch

EDITS = {
    "sh_bh_airport.lua": [
        ("in_area = false\nin_areatwo = false\n", "in_area = false\nin_areatwo = false\nairport_remote_inside = {}\n", 1),
        (
            "\tif char==REMOTE_PLAYER then\n\t\tin_areatwo = true\n",
            "\tif coop_is_remote_player(char) then\n\t\tairport_remote_inside[char] = true\n"
            "\t\tin_areatwo = coop_all_remotes_in(airport_remote_inside)\n",
            1,
        ),
        ("\tif coop_is_active() and char ~= REMOTE_PLAYER then", "\tif coop_is_active() and not coop_is_remote_player(char) then", 1),
        (
            "\tif char == REMOTE_PLAYER then\n\t\tin_areatwo = true\n",
            "\tif coop_is_remote_player(char) then\n\t\tairport_remote_inside[char] = true\n"
            "\t\tin_areatwo = coop_all_remotes_in(airport_remote_inside)\n",
            1,
        ),
        (
            "\t\tif char == REMOTE_PLAYER then\n\t\t\tin_areatwo = false\n",
            "\t\tif coop_is_remote_player(char) then\n\t\t\tairport_remote_inside[char] = false\n\t\t\tin_areatwo = false\n",
            1,
        ),
    ],
    "sh_ss_fishingdock.lua": [
        ("\nin_radius_player2 = false\n", "\nin_radius_player2 = false\nin_radius_remote = {}\n", 1),
        (
            "\telseif (triggerer_name == REMOTE_PLAYER) then\n\t\tin_radius_player2 = true\n",
            "\telseif coop_is_remote_player(triggerer_name) then\n\t\tin_radius_remote[triggerer_name] = true\n\t\tin_radius_player2 = true\n",
            1,
        ),
        (
            "\telseif  coop_is_active() and triggerer_name == REMOTE_PLAYER and character_is_dead(REMOTE_PLAYER)==false  then\n"
            "\t\tin_radius_player2 = false\n",
            "\telseif  coop_is_active() and coop_is_remote_player(triggerer_name) and character_is_dead(triggerer_name)==false  then\n"
            "\t\tin_radius_remote[triggerer_name] = false\n\t\tin_radius_player2 = coop_any_remote_in(in_radius_remote)\n",
            1,
        ),
        (
            "\t\tif get_dist_char_to_nav(REMOTE_PLAYER, last_fish_group) < distance_from_fish then",
            "\t\tif coop_nearest_remote_dist(get_dist_char_to_nav, last_fish_group) < distance_from_fish then",
            1,
        ),
        ("\telseif ( triggerer_name == REMOTE_PLAYER ) then", "\telseif coop_is_remote_player( triggerer_name ) then", 1),
        (
            '\t\tif get_dist_char_to_nav(REMOTE_PLAYER, "sh_ss_fishingdock_$tRadius") < 60 then\t\t\n',
            '\t\tif coop_mark_remotes_near(in_radius_remote, get_dist_char_to_nav, "sh_ss_fishingdock_$tRadius", 60) then\n',
            1,
        ),
        (
            "\t\t\tcharacter_kill( REMOTE_PLAYER )\n\t\t\tcharacter_ragdoll( REMOTE_PLAYER, 100 )\n",
            "\t\t\tcoop_kill_remotes_in(in_radius_remote)\n",
            2,
        ),
    ],
    "sh_bh_docks.lua": [
        (
            '\t\t\tif(get_dist_char_to_vehicle(REMOTE_PLAYER,"sh_bh_docks_$v001") > 60) then',
            '\t\t\tif coop_any_remote_farther_than(get_dist_char_to_vehicle, "sh_bh_docks_$v001", 60) then',
            1,
        ),
        ("\tif (attacker==LOCAL_PLAYER or attacker==REMOTE_PLAYER) then", "\tif (attacker==LOCAL_PLAYER or coop_is_remote_player(attacker)) then", 1),
        ("\t\t\tif char == REMOTE_PLAYER then\n\t\t\t\tin_areatwo = false\n", "\t\t\tif coop_is_remote_player(char) then\n\t\t\t\tin_areatwo = false\n", 1),
    ],
    "sh_bh_chinatown.lua": [
        ("\t\t\tif char == REMOTE_PLAYER then\n\t\t\t\tin_areatwo = false\n", "\t\t\tif coop_is_remote_player(char) then\n\t\t\t\tin_areatwo = false\n", 1),
        ("\t\tif triggerer_name == REMOTE_PLAYER then\n\t\t\tin_areatwo = true\n", "\t\tif coop_is_remote_player(triggerer_name) then\n\t\t\tin_areatwo = true\n", 1),
        ("\t\t\t\t\tif triggerer_name == REMOTE_PLAYER then\n", "\t\t\t\t\tif coop_is_remote_player(triggerer_name) then\n", 1),
    ],
    "sh_bh_apartments.lua": [
        ("\tif triggerer_name == REMOTE_PLAYER then\n\tin_area_remote = true\n", "\tif coop_is_remote_player(triggerer_name) then\n\tin_area_remote = true\n", 1),
    ],
    "sh_tss_caverns.lua": [
        ("\tif damager == LOCAL_PLAYER or damager == REMOTE_PLAYER then", "\tif damager == LOCAL_PLAYER or coop_is_remote_player(damager) then", 1),
    ],
}

PATCHES = {name: Patch(edits=edits, generic=False) for name, edits in EDITS.items()}
