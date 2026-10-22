-- A waypoint a script sets for one remote player reaches that player's machine alone (adapter: waypoint_scope.cpp). The script names
-- the player through mission_debug, which does nothing else in the game, and the adapter sends the waypoints that follow to that
-- player until the name is cleared. Without the adapter's hooks the control text is a debug line and the waypoint reaches every remote.

COOP_SEND_TO = "coop_send_to"

-- True when waypoints for player must be named to the adapter: there is more than one remote player and player is one of them.
function coop_sends_to_one(player)
	return coop_count(coop_remote_players()) > 1 and coop_is_remote_player(player)
end

-- Sent to one remote, ug_lib's mission_waypoint_add and mission_waypoint_remove are made with the natives they call. Those two
-- keep one record for every machine (Mission_waypoint): a remove for one remote cleared it, so the next remote's remove was
-- skipped and that player kept the GPS. Here the remove always reaches the player and leaves the record, and the add sets it,
-- so a later mission-wide remove still runs. A remove when none is set does nothing in the game.
function coop_mission_waypoint_add_one(navpoint, sync)
	waypoint_remove(sync)
	Mission_waypoint = waypoint_add(navpoint, sync)
end

function coop_mission_waypoint_remove_one(sync)
	waypoint_remove(sync)
end

-- The function a call for one remote makes in place of the waypoint function the script named.
function coop_waypoint_one(waypoint_function)
	if waypoint_function == mission_waypoint_add then
		return coop_mission_waypoint_add_one
	elseif waypoint_function == mission_waypoint_remove then
		return coop_mission_waypoint_remove_one
	end
	return waypoint_function
end

-- Runs make() with the waypoint calls it makes named to player, then clears the name. The name is cleared also when make() raises an
-- error (the error is raised again), so the calls of every later script do not all go to that one player.
function coop_send_to_player(player, make)
	mission_debug(COOP_SEND_TO .. " " .. player)
	local ok, result = pcall(make)
	mission_debug(COOP_SEND_TO)
	if not ok then
		error(result)
	end
	return result
end

-- add(navpoint, sync) for player: sent to that player's machine alone when there is more than one remote player.
function coop_waypoint_add_for(player, add, navpoint, sync)
	if not coop_sends_to_one(player) then
		return add(navpoint, sync)
	end
	coop_trace("waypoint add " .. player)
	return coop_send_to_player(player, function() return coop_waypoint_one(add)(navpoint, sync) end)
end

-- remove(sync) for player: sent to that player's machine alone when there is more than one remote player.
function coop_waypoint_remove_for(player, remove, sync)
	if not coop_sends_to_one(player) then
		return remove(sync)
	end
	coop_trace("waypoint remove " .. player)
	return coop_send_to_player(player, function() return coop_waypoint_one(remove)(sync) end)
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
