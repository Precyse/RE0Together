-- Calls that name REMOTE_PLAYER (the one partner of the two-player scripts) act for every remote player.
-- A wrapper replaces the global function. It looks for REMOTE_PLAYER among the arguments and, when there is more than one
-- remote player, applies the function's rule; with one remote player (or none) it makes the original call unchanged.
--   fanout   the call is made once per remote player
--   seats    each remote player gets a seat of its own (coop_seats.lua)
--   seat_lock as fanout, except to a remote player that is on foot for lack of a seat (coop_seats.lua)
--   group    REMOTE_PLAYER in a list of characters is replaced by every remote player
--   target   the call is made for the remote player nearest to the first argument
--   all      true when the call is true for every remote player
--   any      true when the call is true for any remote player
--   nearest  the smallest value over the remote players

COOP_NATIVE = {}
COOP_ORIGINAL = {}
COOP_RULE = {}
-- Called with the arguments of a seat call that does not name REMOTE_PLAYER (coop_seats.lua fills it).
COOP_SEAT_CLAIM = {}

-- The function the game registered for a wrapper made by coop_wrap (any other function as it is).
function coop_native(fn)
	return COOP_ORIGINAL[fn] or fn
end

-- The position of the first REMOTE_PLAYER in the argument table, or nil.
function coop_remote_arg_index(args)
	for i = 1, args.n do
		if args[i] == REMOTE_PLAYER then
			return i
		end
	end
	return nil
end

-- A copy of the arguments with player at position at.
function coop_args_for(args, at, player)
	local copy = { n = args.n }
	for i = 1, args.n do
		copy[i] = args[i]
	end
	copy[at] = player
	return copy
end

function COOP_RULE.fanout(native, args, at, remotes)
	local first = nil
	for i, p in ipairs(remotes) do
		local copy = coop_args_for(args, at, p)
		local results = { native(unpack(copy, 1, copy.n)) }
		first = first or results
	end
	return unpack(first)
end

function COOP_RULE.all(native, args, at, remotes)
	for i, p in ipairs(remotes) do
		local copy = coop_args_for(args, at, p)
		if not native(unpack(copy, 1, copy.n)) then
			return false
		end
	end
	return true
end

function COOP_RULE.any(native, args, at, remotes)
	for i, p in ipairs(remotes) do
		local copy = coop_args_for(args, at, p)
		if native(unpack(copy, 1, copy.n)) then
			return true
		end
	end
	return false
end

function COOP_RULE.nearest(native, args, at, remotes)
	local nearest = nil
	for i, p in ipairs(remotes) do
		local copy = coop_args_for(args, at, p)
		local dist = native(unpack(copy, 1, copy.n))
		if nearest == nil or dist < nearest then
			nearest = dist
		end
	end
	return nearest
end

function COOP_RULE.target(native, args, at, remotes)
	local chosen = remotes[1]
	if at ~= 1 then
		local chosen_dist = nil
		for i, p in ipairs(remotes) do
			local dist = COOP_NATIVE.get_dist(args[1], p)
			if chosen_dist == nil or dist < chosen_dist then
				chosen = p
				chosen_dist = dist
			end
		end
	end
	local copy = coop_args_for(args, at, chosen)
	return native(unpack(copy, 1, copy.n))
end

function COOP_RULE.group(native, args, at, remotes)
	local copy = { n = 0 }
	for i = 1, args.n do
		if i == at then
			for k, p in ipairs(remotes) do
				copy.n = copy.n + 1
				copy[copy.n] = p
			end
		else
			copy.n = copy.n + 1
			copy[copy.n] = args[i]
		end
	end
	return native(unpack(copy, 1, copy.n))
end

-- Replaces the global function name with a wrapper that applies rule around native (default: the current function).
-- A name that is not defined, or is already wrapped, is left alone.
function coop_wrap(name, rule, native)
	if COOP_ORIGINAL[_G[name]] ~= nil then
		return
	end
	native = native or _G[name]
	if native == nil or COOP_RULE[rule] == nil then
		return
	end
	local handler = COOP_RULE[rule]
	local wrapper = function(...)
		local at = coop_remote_arg_index(arg)
		if at == nil then
			if COOP_SEAT_CLAIM[name] ~= nil then
				COOP_SEAT_CLAIM[name](name, arg)
			end
			return native(unpack(arg, 1, arg.n))
		end
		local remotes = coop_remote_players()
		if coop_count(remotes) < 2 or (rule == "group" and type(arg[1]) == "table") then
			return native(unpack(arg, 1, arg.n))
		end
		return handler(native, arg, at, remotes, name)
	end
	COOP_NATIVE[name] = native
	COOP_ORIGINAL[wrapper] = native
	_G[name] = wrapper
end
