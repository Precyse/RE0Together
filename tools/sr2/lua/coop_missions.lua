-- Mission support for more than two players (added by the SR2 co-op mod, appended to ug_lib.lua after coop_helpers.lua).
-- The scripts keep per-player state in tables keyed by player name and ask "both players ..." or "either player ...".

-- A per-player default: a table is copied one level, any other value is used as it is.
function coop_copy_default(value)
	if type(value) ~= "table" then
		return value
	end
	local copy = {}
	for k, v in pairs(value) do
		copy[k] = v
	end
	return copy
end

-- A table keyed by player name: local_value for the local player, a copy of remote_value for every remote player.
-- A remote player that has no entry yet (it is in the game, but was not when the table was made) gets a copy on its first read.
function coop_player_table(local_value, remote_value)
	local state = {}
	state[LOCAL_PLAYER] = local_value
	for i, p in pairs(coop_remote_players()) do
		state[p] = coop_copy_default(remote_value)
	end
	setmetatable(state, {
		__index = function(table_, name)
			if coop_is_remote_player(name) then
				local value = coop_copy_default(remote_value)
				rawset(table_, name, value)
				return value
			end
			return nil
		end
	})
	return state
end

-- True when state[name] is true for the local player and for every remote player.
function coop_all_players_in(state)
	return state[LOCAL_PLAYER] and coop_all_remotes_in(state)
end

-- True when state[name] is true for the local player or for any remote player.
function coop_any_player_in(state)
	return state[LOCAL_PLAYER] or coop_any_remote_in(state)
end

-- Sets state[name] = value for every remote player.
function coop_set_remotes(state, value)
	for i, p in pairs(coop_remote_players()) do
		state[p] = value
	end
end

-- True when state[name] == value for every remote player.
function coop_all_remotes_equal(state, value)
	for i, p in pairs(coop_remote_players()) do
		if state[p] ~= value then
			return false
		end
	end
	return true
end

-- True when state[name] == value for any remote player.
function coop_any_remote_equal(state, value)
	for i, p in pairs(coop_remote_players()) do
		if state[p] == value then
			return true
		end
	end
	return false
end

-- The name of the remote player nearest to object (REMOTE_PLAYER when there is no other player).
function coop_nearest_remote(object)
	local nearest = nil
	local nearest_dist = nil
	for i, p in pairs(coop_remote_players()) do
		local dist = COOP_NATIVE.get_dist(p, object)
		if nearest == nil or dist < nearest_dist then
			nearest = p
			nearest_dist = dist
		end
	end
	return nearest
end

-- The calls below take the call as it is written in a two-player script, fn followed by its arguments with REMOTE_PLAYER
-- among them, and make it once per remote player with REMOTE_PLAYER replaced by that player.

-- fn(...) once per remote player.
function coop_each_remote(fn, ...)
	local at = coop_remote_arg_index(arg)
	for i, p in pairs(coop_remote_players()) do
		local copy = coop_args_for(arg, at, p)
		fn(unpack(copy, 1, copy.n))
	end
end

-- True when fn(...) is true for any remote player.
function coop_any_remote(fn, ...)
	return coop_first_remote(fn, unpack(arg)) ~= nil
end

-- True when fn(...) is true for every remote player.
function coop_all_remotes(fn, ...)
	local test = coop_native(fn)
	local at = coop_remote_arg_index(arg)
	for i, p in pairs(coop_remote_players()) do
		local copy = coop_args_for(arg, at, p)
		if not test(unpack(copy, 1, copy.n)) then
			return false
		end
	end
	return true
end

-- The first remote player for which fn(...) is true, or nil.
function coop_first_remote(fn, ...)
	local test = coop_native(fn)
	local at = coop_remote_arg_index(arg)
	for i, p in pairs(coop_remote_players()) do
		local copy = coop_args_for(arg, at, p)
		if test(unpack(copy, 1, copy.n)) then
			return p
		end
	end
	return nil
end

-- The first remote player with state[name] == value, or nil.
function coop_first_remote_equal(state, value)
	for i, p in pairs(coop_remote_players()) do
		if state[p] == value then
			return p
		end
	end
	return nil
end

-- Disables the controls of every remote player who is in a vehicle and stops that vehicle.
function coop_stop_remote_vehicles()
	for i, p in pairs(coop_remote_players()) do
		if COOP_NATIVE.character_is_in_vehicle(p) then
			COOP_NATIVE.player_controls_disable(p)
			COOP_NATIVE.vehicle_stop_do(p)
		end
	end
end

-- True when state[name][key] is true for any remote player (state holds one table per player).
function coop_any_remote_at(state, key)
	for i, p in pairs(coop_remote_players()) do
		if state[p][key] then
			return true
		end
	end
	return false
end

-- True when state[name][key] is greater than limit for any remote player.
function coop_any_remote_above(state, key, limit)
	for i, p in pairs(coop_remote_players()) do
		if state[p][key] > limit then
			return true
		end
	end
	return false
end

-- A random remote player.
function coop_random_remote()
	local remotes = coop_remote_players()
	return remotes[rand_int(1, coop_count(remotes))]
end

-- The names of all players: the local player, then every remote player.
function coop_all_players()
	local players = { LOCAL_PLAYER }
	for i, p in pairs(coop_remote_players()) do
		players[coop_count(players) + 1] = p
	end
	return players
end

-- The distance of the remote player farthest from object, measured with dist_fn(player, object).
function coop_farthest_remote_dist(dist_fn, object)
	local measure = coop_native(dist_fn)
	local farthest = 0
	for i, p in pairs(coop_remote_players()) do
		local current = measure(p, object)
		if current > farthest then
			farthest = current
		end
	end
	return farthest
end

-- A copy of the list of names with REMOTE_PLAYER replaced by every remote player.
function coop_expand_remote(names)
	local expanded = {}
	for i, name in ipairs(names) do
		if name == REMOTE_PLAYER then
			for k, p in ipairs(coop_remote_players()) do
				expanded[coop_count(expanded) + 1] = p
			end
		else
			expanded[coop_count(expanded) + 1] = name
		end
	end
	return expanded
end
