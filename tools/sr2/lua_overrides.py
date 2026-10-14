"""Builds the Saints Row 2 stronghold Lua overrides for 3 or more players from the game's own scripts.

usage: python tools/sr2/lua_overrides.py <dir with the original .lua files> <output dir>

The adapter (script_override, AF9) loads a script from <game>\\coop\\scripts when a file of that name is there. The
scripts are the game's, so they are never kept in the repo: this tool applies the edits below to copies that were
extracted from the game's archives (read-only) and writes the results to the output dir, normally the build's
coop\\scripts folder. Every edit must find its text exactly the expected number of times, else nothing is written.
"""
import sys
from pathlib import Path

HELPERS = Path(__file__).with_name("coop_helpers.lua")

# file -> list of (old text, new text, expected count). Text uses \n; the file's own line endings are applied.
EDITS = {
    "ug_lib.lua": [
        (
            "\tif (coop_is_active()) then\n\t\tteleport( REMOTE_PLAYER, remote_player_nav, exit_vehicles )\n\tend\n",
            "\tlocal remotes = {}\n\tif (coop_is_active()) then\n\t\tremotes = coop_remote_players()\n"
            "\t\tfor i, p in pairs(remotes) do\n\t\t\tteleport( p, remote_player_nav, exit_vehicles )\n\t\tend\n\tend\n",
            1,
        ),
        (
            "\t\twhile( not teleport_check_done(REMOTE_PLAYER)) do\n\t\t\tthread_yield()\n\t\tend\n",
            "\t\tfor i, p in pairs(remotes) do\n\t\t\twhile( not teleport_check_done(p)) do\n\t\t\t\tthread_yield()\n\t\t\tend\n\t\tend\n",
            1,
        ),
    ],
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
    "sh_ss_trailerpark.lua": [
        (
            '\t\tvehicle_enter_teleport("#PLAYER2#","sh_ss_trailerpark_$v002 (0)", 0)\n',
            '\t\tcoop_vehicle_enter_teleport_remotes("sh_ss_trailerpark_$v002 (0)", 0)\n',
            1,
        ),
    ],
    "sh_ss_crackhouse.lua": [
        (
            "\t\tset_player_can_enter_exit_vehicles(REMOTE_PLAYER, true)\n",
            "\t\tcoop_set_remotes_can_enter_exit_vehicles(true)\n",
            1,
        ),
    ],
    "sh_tss_caverns.lua": [
        ("\tif damager == LOCAL_PLAYER or damager == REMOTE_PLAYER then", "\tif damager == LOCAL_PLAYER or coop_is_remote_player(damager) then", 1),
    ],
    # sh_tss_ugmall.lua teleports through teleport_coop (ug_lib.lua above); the other strongholds need no change.
}


def read(path):
    """The game's scripts are not valid UTF-8 (Windows-1252 quotes); latin-1 keeps every byte as it is."""
    return path.read_bytes().decode("latin-1")


def apply_edits(name, text):
    """The text of `name` with its edits applied, in the file's own line endings."""
    crlf = "\r\n" in text
    body = text.replace("\r\n", "\n")
    for old, new, expected in EDITS.get(name, []):
        found = body.count(old)
        if found != expected:
            raise ValueError(f"{name}: expected {expected} of {old!r}, found {found}")
        body = body.replace(old, new)
    if name == "ug_lib.lua":
        body = body.rstrip("\n") + "\n" + HELPERS.read_text(encoding="utf-8")
    return body.replace("\n", "\r\n") if crlf else body


def build(source, out):
    results = {}
    for name in EDITS:
        path = source / name
        if not path.is_file():
            raise FileNotFoundError(path)
        results[name] = apply_edits(name, read(path))
    out.mkdir(parents=True, exist_ok=True)
    for name, text in results.items():
        (out / name).write_bytes(text.encode("latin-1"))
    return sorted(results)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        written = build(Path(argv[1]), Path(argv[2]))
    except (ValueError, FileNotFoundError) as error:
        print(f"lua_overrides: {error}")
        return 1
    print(f"lua_overrides: wrote {len(written)} scripts to {argv[2]}: {', '.join(written)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
