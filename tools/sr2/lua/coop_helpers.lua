
-- Co-op with more than two players (added by the SR2 co-op mod, appended to ug_lib.lua).
-- Player names come from player_names_get_all(); the local player is LOCAL_PLAYER, every other name is a remote player.

COOP_FAR_DISTANCE = 1000000

-- The number of entries of list from index 1 up to the first nil. SR2 runs Lua 5.0 with the base library only,
-- so there is no getn; this is the one counter of the library.
function coop_count(list)
	local count = 0
	while list[count + 1] ~= nil do
		count = count + 1
	end
	return count
end

-- The names of the players other than the local one, in a table indexed from 1.
-- Without any other player the list holds REMOTE_PLAYER, so everything built on it behaves like the two-player script.
function coop_remote_players()
	local remotes = {}
	local count = 0
	for i, p in pairs(player_names_get_all()) do
		if type(p) == "string" and p ~= LOCAL_PLAYER then
			count = count + 1
			remotes[count] = p
		end
	end
	if count == 0 then
		remotes[1] = REMOTE_PLAYER
	end
	return remotes
end

-- True when name is the name of a remote player.
function coop_is_remote_player(name)
	if name == nil or name == LOCAL_PLAYER then
		return false
	end
	for i, p in pairs(coop_remote_players()) do
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
	local measure = coop_native(dist_fn)
	local nearest = COOP_FAR_DISTANCE
	for i, p in pairs(coop_remote_players()) do
		local current = measure(p, object)
		if current < nearest then
			nearest = current
		end
	end
	return nearest
end

-- True when any remote player is farther than distance from object.
function coop_any_remote_farther_than(dist_fn, object, distance)
	local measure = coop_native(dist_fn)
	for i, p in pairs(coop_remote_players()) do
		if measure(p, object) > distance then
			return true
		end
	end
	return false
end

-- Sets inside[name] to value (default true) for every remote player closer than distance to object;
-- true when there was any.
function coop_mark_remotes_near(inside, dist_fn, object, distance, value)
	local measure = coop_native(dist_fn)
	if value == nil then
		value = true
	end
	local any = false
	for i, p in pairs(coop_remote_players()) do
		if measure(p, object) < distance then
			inside[p] = value
			any = true
		end
	end
	return any
end

-- Kills every remote player that has inside[name] set.
function coop_kill_remotes_in(inside)
	for i, p in pairs(coop_remote_players()) do
		if inside[p] then
			COOP_NATIVE.character_kill( p )
			COOP_NATIVE.character_ragdoll( p, 100 )
		end
	end
end
