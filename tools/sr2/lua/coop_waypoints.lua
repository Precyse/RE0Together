-- A waypoint a script sets for one remote player reaches that player's machine alone (adapter: waypoint_scope.cpp). The script names
-- the player through mission_debug, which does nothing else in the game, and the adapter sends the waypoints that follow to that
-- player until the name is cleared. Without the adapter's hooks the control text is a debug line and the waypoint reaches every remote.

COOP_SEND_TO = "coop_send_to"

-- True when waypoints for player must be named to the adapter: there is more than one remote player and player is one of them.
function coop_sends_to_one(player)
	return coop_count(coop_remote_players()) > 1 and coop_is_remote_player(player)
end

-- add(navpoint, sync) for player: sent to that player's machine alone when there is more than one remote player.
function coop_waypoint_add_for(player, add, navpoint, sync)
	if not coop_sends_to_one(player) then
		return add(navpoint, sync)
	end
	mission_debug(COOP_SEND_TO .. " " .. player)
	local result = add(navpoint, sync)
	mission_debug(COOP_SEND_TO)
	return result
end

-- remove(sync) for player: sent to that player's machine alone when there is more than one remote player.
function coop_waypoint_remove_for(player, remove, sync)
	if not coop_sends_to_one(player) then
		return remove(sync)
	end
	mission_debug(COOP_SEND_TO .. " " .. player)
	local result = remove(sync)
	mission_debug(COOP_SEND_TO)
	return result
end

-- The name of the navpoint in navpoint_names closest to player, measured with the game's own get_dist so that a remote player is not
-- widened to the nearest remote.
function coop_closest_navpoint(navpoint_names, player)
	local measure = coop_native(get_dist)
	local closest_name = navpoint_names[1]
	local closest_dist = measure(player, navpoint_names[1])
	for index, name in pairs(navpoint_names) do
		local current = measure(player, name)
		if current < closest_dist then
			closest_name = name
			closest_dist = current
		end
	end
	return closest_name
end
