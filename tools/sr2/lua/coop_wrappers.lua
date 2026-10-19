-- Calls that name REMOTE_PLAYER (the one partner of the two-player scripts) act for every remote player.
-- A wrapper replaces the global function. It looks for REMOTE_PLAYER among the arguments and, when there is more than one
-- remote player, applies the function's rule; with one remote player (or none) it makes the original call unchanged.
--   fanout   the call is made once per remote player
--   seats    as fanout; the first remote player takes the given seat, every other one the highest free seat that the
--            script does not reserve (coop_slots.lua), or stays out when there is none
--   group    REMOTE_PLAYER in a list of characters is replaced by every remote player
--   target   the call is made for the remote player nearest to the first argument
--   all      true when the call is true for every remote player
--   in_vehicle  as all, leaving out a remote player the seats rule found no seat for in that vehicle
--   any      true when the call is true for any remote player
--   nearest  the smallest value over the remote players

COOP_NATIVE = {}
COOP_ORIGINAL = {}
COOP_RULE = {}
COOP_SEAT_ARG = { vehicle_enter_teleport = 3, player_force_vehicle_seat = 2 }
COOP_VEHICLE_ARG = { vehicle_enter_teleport = 2, player_force_vehicle_seat = 3 }
COOP_LEFT_OUT = {}  -- vehicle .. player: the in_vehicle rule has traced that it leaves this remote out

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

-- A seat that is not a number of 0 or more (none given, or below 0 to release the player) is passed on unchanged. The
-- seats given are recorded in COOP_SEATS[vehicle] (false: no free seat, the call is not made for that remote).
function COOP_RULE.seats(native, args, at, remotes, name)
	local seat_at = COOP_SEAT_ARG[name]
	local given = args[seat_at]
	local vehicle = args[COOP_VEHICLE_ARG[name]]
	local seating = type(given) == "number" and given >= 0
	local record = nil
	if seating and vehicle ~= nil then
		record = COOP_SEATS[vehicle] or {}
		COOP_SEATS[vehicle] = record
	end
	local taken = {}
	local first = nil
	for k, p in ipairs(remotes) do
		local seat = given
		if seating and k > 1 then
			if record == nil then
				seat = given + k - 1
			elseif type(record[p]) == "number" then
				seat = record[p]
			else
				seat = coop_free_seat(vehicle, taken)
			end
			coop_trace("seat " .. tostring(vehicle) .. " " .. p .. " " .. tostring(seat))
		end
		if seating and seat ~= nil then
			taken[seat] = true
		end
		if record ~= nil then
			record[p] = seat or false
		end
		if seat ~= nil or not seating then
			local copy = coop_args_for(args, at, p)
			copy[seat_at] = seat
			local results = { native(unpack(copy, 1, copy.n)) }
			first = first or results
		end
	end
	return unpack(first)
end

function COOP_RULE.in_vehicle(native, args, at, remotes)
	local record = COOP_SEATS[args[2]]
	for i, p in ipairs(remotes) do
		if record == nil or record[p] ~= false then
			local copy = coop_args_for(args, at, p)
			if not native(unpack(copy, 1, copy.n)) then
				return false
			end
		elseif not COOP_LEFT_OUT[tostring(args[2]) .. p] then
			COOP_LEFT_OUT[tostring(args[2]) .. p] = true
			coop_trace("in vehicle " .. tostring(args[2]) .. " leaves out " .. p .. ", it has no seat")
		end
	end
	return true
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
