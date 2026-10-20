"""The per-player waypoint calls a script makes through a local sync variable: each becomes a call of
tools/sr2/lua/coop_waypoints.lua that names the player of the enclosing function."""


def per_player_waypoints(player, removes=(), adds=(), function="waypoint"):
    """Line edits (lua_patch.apply_line_edits) for the <function>_remove calls on the lines removes and the <function>_add
    calls on the lines adds; function is "waypoint" or "mission_waypoint"."""
    edits = [(number, f"{function}_remove(", f"coop_waypoint_remove_for({player}, {function}_remove, ") for number in removes]
    edits += [(number, f"{function}_add(", f"coop_waypoint_add_for({player}, {function}_add, ") for number in adds]
    return edits
