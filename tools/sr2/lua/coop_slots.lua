-- Per-remote slots for more than two players (added by the SR2 co-op mod, appended to ug_lib.lua after coop_missions.lua).
-- A two-player script gives the remote side one HUD slot, one seat or one "other player"; these give every remote its own.

COOP_TRACE_PREFIX = "coop: "

-- Writes text to the adapter log when trace=1: mission_debug is a native that does nothing, the mod's LT1 hook reads it.
function coop_trace(text)
	mission_debug(COOP_TRACE_PREFIX .. text)
end

-- The number n of the script name "#PLAYERn#", or nil. The game hands the names out lowest number first, so with four
-- players the remotes are #PLAYER2# to #PLAYER4# (COOP_PLAYER_NUMBER, coop_outfit_sync.lua, lists the remote names).
function coop_player_number(name)
	if name == LOCAL_PLAYER then
		return 1
	end
	return COOP_PLAYER_NUMBER[name]
end

-- The HUD slot (bar, timer or objective index) of player name: local_slot for the local player, remote_slots[n - 1] for
-- "#PLAYERn#", and remote_slots[1], the remote side's slot of the two-player script, for a remote without an entry.
function coop_hud_slot(name, local_slot, remote_slots)
	if name == LOCAL_PLAYER then
		return local_slot
	end
	local n = coop_player_number(name)
	if n ~= nil and n >= 2 and remote_slots[n - 1] ~= nil then
		return remote_slots[n - 1]
	end
	return remote_slots[1]
end

-- A table of HUD slots keyed by player name, read like the two-player {[LOCAL_PLAYER] = a, [REMOTE_PLAYER] = b}.
function coop_hud_slots(local_slot, remote_slots)
	local slots = {}
	slots[LOCAL_PLAYER] = local_slot
	slots[REMOTE_PLAYER] = remote_slots[1]
	setmetatable(slots, {
		__index = function(table_, name)
			if not coop_is_remote_player(name) then
				return nil
			end
			local slot = coop_hud_slot(name, local_slot, remote_slots)
			rawset(table_, name, slot)
			coop_trace("hud slot " .. name .. " " .. slot)
			return slot
		end
	})
	return slots
end

-- fn(slot) once for every distinct slot the remote players have in slots (a coop_hud_slots table).
function coop_each_remote_slot(fn, slots)
	local done = {}
	for i, p in pairs(coop_remote_players()) do
		local slot = slots[p]
		if slot ~= nil and not done[slot] then
			done[slot] = true
			fn(slot)
		end
	end
end

-- The names of every player but name, the local player first.
function coop_other_players(name)
	local others = {}
	for i, p in ipairs(coop_all_players()) do
		if p ~= name then
			others[coop_count(others) + 1] = p
		end
	end
	return others
end

-- True when state[p] == value for any player p other than name.
function coop_any_other_equal(state, name, value)
	for i, p in ipairs(coop_other_players(name)) do
		if state[p] == value then
			return true
		end
	end
	return false
end

-- The remote players p with state[p] == value, in coop_remote_players() order.
function coop_remotes_equal(state, value)
	local found = {}
	for i, p in ipairs(coop_remote_players()) do
		if state[p] == value then
			found[coop_count(found) + 1] = p
		end
	end
	return found
end

-- True when fn(p, ...) is true for any player p other than excluded.
function coop_any_other(fn, excluded, ...)
	for i, p in ipairs(coop_other_players(excluded)) do
		if fn(p, unpack(arg, 1, arg.n)) then
			return true
		end
	end
	return false
end

-- True when state[name] >= value for every remote player.
function coop_all_remotes_at_least(state, value)
	for i, p in pairs(coop_remote_players()) do
		if state[p] < value then
			return false
		end
	end
	return true
end

-- The player after name in coop_all_players() order, from the last back to the first (two players: the other one).
function coop_next_player(name)
	local players = coop_all_players()
	local count = coop_count(players)
	for i = 1, count do
		if players[i] == name then
			if i == count then
				return players[1]
			end
			return players[i + 1]
		end
	end
	return players[1]
end

-- The first player after name, in coop_next_player order, for which fn(p) is true, or nil.
function coop_next_player_where(name, fn)
	local p = name
	for i = 2, coop_count(coop_all_players()) do
		p = coop_next_player(p)
		if fn(p) then
			return p
		end
	end
	return nil
end

-- The player other than excluded nearest to object, a living one first; with no other player alive, the nearest one.
function coop_nearest_other_player(object, excluded)
	local best = nil
	local best_dist = nil
	local best_living = false
	for i, p in ipairs(coop_other_players(excluded)) do
		local living = not COOP_NATIVE.character_is_dead(p)
		local dist = COOP_NATIVE.get_dist(p, object)
		if best == nil or (living and not best_living) or (living == best_living and dist < best_dist) then
			best = p
			best_dist = dist
			best_living = living
		end
	end
	return best
end

-- The player other than excluded with state[p] true that is nearest to object, else get_other_coop_player(excluded).
function coop_nearest_other_in(state, object, excluded)
	local best = nil
	local best_dist = nil
	for i, p in ipairs(coop_other_players(excluded)) do
		if state[p] then
			local dist = COOP_NATIVE.get_dist(p, object)
			if best == nil or dist < best_dist then
				best = p
				best_dist = dist
			end
		end
	end
	return best or get_other_coop_player(excluded)
end

-- The smallest distance from object of the players in the list players (COOP_FAR_DISTANCE for an empty list).
function coop_nearest_dist_of(players, object)
	local nearest = COOP_FAR_DISTANCE
	for i, p in ipairs(players) do
		local dist = COOP_NATIVE.get_dist(p, object)
		if dist < nearest then
			nearest = dist
		end
	end
	return nearest
end

-- The results of fn(...) for the remote player that makes result number rank_index the smallest (REMOTE_PLAYER among
-- the arguments stands for each remote in turn).
function coop_best_remote(fn, rank_index, ...)
	local at = coop_remote_arg_index(arg)
	local best = nil
	for i, p in pairs(coop_remote_players()) do
		local copy = coop_args_for(arg, at, p)
		local results = { fn(unpack(copy, 1, copy.n)) }
		if best == nil or results[rank_index] < best[rank_index] then
			best = results
		end
	end
	return unpack(best)
end

-- True when every player's vehicle in vehicles (a player table) is vehicles[name].
function coop_all_in_vehicle_of(vehicles, name)
	for i, p in ipairs(coop_other_players(name)) do
		if vehicles[p] ~= vehicles[name] then
			return false
		end
	end
	return true
end
