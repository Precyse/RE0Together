
-- Co-op with more than two players (added by the SR2 co-op mod, appended to ug_lib.lua).
-- Player names come from player_names_get_all(); the local player is LOCAL_PLAYER, every other name is a remote player.

-- The names of the players other than the local one, in a table indexed from 1.
function coop_remote_players()
	local remotes = {}
	local count = 0
	for i, p in pairs(player_names_get_all()) do
		if p ~= LOCAL_PLAYER then
			count = count + 1
			remotes[count] = p
		end
	end
	return remotes
end

-- True when name is the name of a remote player.
function coop_is_remote_player(name)
	if name == nil or name == LOCAL_PLAYER then
		return false
	end
	for i, p in pairs(player_names_get_all()) do
		if p == name then
			return true
		end
	end
	return false
end

-- True when inside[name] is true for every remote player (also when there is none).
function coop_all_remotes_in(inside)
	for i, p in pairs(coop_remote_players()) do
		if not inside[p] then
			return false
		end
	end
	return true
end

-- True when inside[name] is true for any remote player.
function coop_any_remote_in(inside)
	for i, p in pairs(coop_remote_players()) do
		if inside[p] then
			return true
		end
	end
	return false
end

-- The distance of the nearest remote player to object, measured with dist_fn(player, object).
function coop_nearest_remote_dist(dist_fn, object)
	local nearest = 1000000
	for i, p in pairs(coop_remote_players()) do
		local current = dist_fn(p, object)
		if current < nearest then
			nearest = current
		end
	end
	return nearest
end

-- True when any remote player is farther than distance from object.
function coop_any_remote_farther_than(dist_fn, object, distance)
	for i, p in pairs(coop_remote_players()) do
		if dist_fn(p, object) > distance then
			return true
		end
	end
	return false
end

-- Sets inside[name] for every remote player closer than distance to object; true when there was any.
function coop_mark_remotes_near(inside, dist_fn, object, distance)
	local any = false
	for i, p in pairs(coop_remote_players()) do
		if dist_fn(p, object) < distance then
			inside[p] = true
			any = true
		end
	end
	return any
end

-- Kills every remote player that has inside[name] set.
function coop_kill_remotes_in(inside)
	for i, p in pairs(coop_remote_players()) do
		if inside[p] then
			character_kill( p )
			character_ragdoll( p, 100 )
		end
	end
end

-- Puts every remote player into a vehicle, the first into first_seat and each next one into the seat after it.
function coop_vehicle_enter_teleport_remotes(vehicle, first_seat)
	local seat = first_seat
	for i, p in pairs(coop_remote_players()) do
		vehicle_enter_teleport(p, vehicle, seat)
		seat = seat + 1
	end
end

-- Lets every remote player enter and leave vehicles, or not.
function coop_set_remotes_can_enter_exit_vehicles(allowed)
	for i, p in pairs(coop_remote_players()) do
		set_player_can_enter_exit_vehicles(p, allowed)
	end
end
