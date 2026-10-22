-- Vehicle seating for three or four players (added by the SR2 co-op mod, appended to ug_lib.lua after coop_wrappers.lua).
-- The scripts seat REMOTE_PLAYER, and the NPCs and the local player around it, at fixed seats. With more than one remote player
-- the k-th remote (k = 1 is REMOTE_PLAYER) gets a seat of its own: the scripted seat for REMOTE_PLAYER, else a free seat of the
-- vehicle, else a seat of another vehicle, else it stays on foot. A seat the script then gives to someone else is taken from the
-- extra remote. The in-vehicle query follows the placement, so a wait for "every remote in the vehicle" ends.
-- With fewer than two remotes, or when the game's query natives are missing, the k-th remote takes the seat k - 1 places after
-- the scripted one.

COOP_SEAT_ARG = { vehicle_enter_teleport = 3, vehicle_enter = 3, player_force_vehicle_seat = 2 }
COOP_SEAT_VEHICLE_ARG = { vehicle_enter_teleport = 2, vehicle_enter = 2, player_force_vehicle_seat = 3 }
COOP_SEAT_FORCE = "player_force_vehicle_seat"
COOP_SEAT_FIRST_PASSENGER = 1
COOP_SEAT_ON_FOOT = "on foot"

-- The natives of the game, taken before the wrappers replace them.
COOP_SEAT_RAW = {
	in_vehicle = character_is_in_vehicle,
	occupant = get_char_in_vehicle,
	num_seats = vehicle_get_num_seats,
	vehicle_name = get_char_vehicle_name,
	enter_done = vehicle_enter_check_done,
	debug = mission_debug,
}

COOP_SEAT_PLACED = {}  -- [remote][vehicle the script named] = { vehicle, seat, request, k } or COOP_SEAT_ON_FOOT
COOP_SEAT_LAST = {}    -- [remote] = the last placement of that remote
COOP_SEAT_CLAIMS = {}  -- [character] = { vehicle, seat } of its last enter call
COOP_SEAT_FORCED = {}  -- [player] = { vehicle, seat } of its forced seat
COOP_SEAT_ASKED = {}   -- [remote and vehicle] = the last answer of the in-vehicle query that was logged

-- True when the natives that tell who sits where exist (the unit test harness has none).
function coop_seat_ready()
	return COOP_SEAT_RAW.in_vehicle ~= nil and COOP_SEAT_RAW.occupant ~= nil and COOP_SEAT_RAW.num_seats ~= nil
		and COOP_SEAT_RAW.vehicle_name ~= nil and COOP_SEAT_RAW.enter_done ~= nil
end

-- The adapter logs the texts that start with "coop seat " (mission_debug does nothing else).
function coop_seat_trace(text)
	if COOP_SEAT_RAW.debug ~= nil then
		COOP_SEAT_RAW.debug("coop seat " .. text)
	end
end

function coop_seat_use(used, vehicle, seat)
	used[vehicle .. "/" .. seat] = true
end

-- The seats a remote after the first may take in a vehicle of n seats: the seats after the scripted one, then the ones before
-- it. The driver seat 0 and the scripted seat are never in the list.
function coop_seat_candidates(n, scripted)
	local list = {}
	local count = 0
	for seat = scripted + 1, n - 1 do
		count = count + 1
		list[count] = seat
	end
	for seat = COOP_SEAT_FIRST_PASSENGER, scripted - 1 do
		if seat < n then
			count = count + 1
			list[count] = seat
		end
	end
	return list
end

-- True when the character that claimed a seat is still in the vehicle or still on its way in.
function coop_seat_claim_live(char, claim)
	if COOP_SEAT_RAW.in_vehicle(char, claim.vehicle) then
		return true
	end
	local done = COOP_SEAT_RAW.enter_done(char)
	return done == false or done == 0
end

-- True when somebody sits in the seat, is entering it, has it forced, or was just given it (used).
-- The occupant lookup answers nil while a character is entering or leaving, so the claims cover that.
function coop_seat_taken(vehicle, seat, used)
	if COOP_SEAT_RAW.occupant(vehicle, seat) ~= nil or used[vehicle .. "/" .. seat] then
		return true
	end
	for char, claim in pairs(COOP_SEAT_CLAIMS) do
		if claim.vehicle == vehicle and claim.seat == seat then
			if coop_seat_claim_live(char, claim) then
				return true
			end
			COOP_SEAT_CLAIMS[char] = nil
		end
	end
	for player, forced in pairs(COOP_SEAT_FORCED) do
		if forced.vehicle == vehicle and forced.seat == seat then
			return true
		end
	end
	return false
end

-- The first free candidate seat of the vehicle, and its seat count (0 when the vehicle is not resolved yet).
function coop_seat_pick(vehicle, scripted, used)
	local n = COOP_SEAT_RAW.num_seats(vehicle)
	if type(n) ~= "number" or n <= 0 then
		return nil, 0
	end
	local candidates = coop_seat_candidates(n, scripted)
	for i = 1, coop_count(candidates) do
		if not coop_seat_taken(vehicle, candidates[i], used) then
			return candidates[i], n
		end
	end
	return nil, n
end

-- The vehicles a remote may be seated in instead of target: the local player's, then the other remotes', in list order.
function coop_seat_fallbacks(p, target)
	local list = {}
	local count = 0
	local function add(vehicle)
		if vehicle == nil or vehicle == target then
			return
		end
		for i = 1, count do
			if list[i] == vehicle then
				return
			end
		end
		count = count + 1
		list[count] = vehicle
	end
	add(COOP_SEAT_RAW.vehicle_name(LOCAL_PLAYER))
	for i, other in ipairs(coop_remote_players()) do
		if other ~= p then
			add(COOP_SEAT_RAW.vehicle_name(other))
		end
	end
	return list
end

-- The first fallback vehicle with a free passenger seat, and that seat; nil when there is none.
function coop_seat_fallback(p, target, used)
	local vehicles = coop_seat_fallbacks(p, target)
	for i = 1, coop_count(vehicles) do
		local n = COOP_SEAT_RAW.num_seats(vehicles[i])
		if type(n) == "number" then
			for seat = COOP_SEAT_FIRST_PASSENGER, n - 1 do
				if not coop_seat_taken(vehicles[i], seat, used) then
					return vehicles[i], seat
				end
			end
		end
	end
	return nil, nil
end

-- Enters remote p (the k-th) as the request says: the scripted vehicle and a free seat in it, else another vehicle, else not at all.
-- The request is { name, args, at } of the script's call. used lists the seats given out by the call in progress.
function coop_seat_put(p, k, request, used)
	local seat_at = COOP_SEAT_ARG[request.name]
	local vehicle_at = COOP_SEAT_VEHICLE_ARG[request.name]
	local native = COOP_NATIVE[request.name]
	local vehicle = request.args[vehicle_at]
	local scripted = request.args[seat_at]
	local copy = coop_args_for(request.args, request.at, p)
	local placed = COOP_SEAT_PLACED[p]
	if placed == nil then
		placed = {}
		COOP_SEAT_PLACED[p] = placed
	end
	COOP_SEAT_CLAIMS[p] = nil
	local seat, n = coop_seat_pick(vehicle, scripted, used)
	local label = p .. " " .. vehicle .. " n=" .. n .. " seat " .. scripted .. " -> "
	if n == 0 then
		copy[seat_at] = scripted + k - 1
		native(unpack(copy, 1, copy.n))
		coop_seat_trace(label .. vehicle .. " " .. copy[seat_at] .. " (legacy)")
		return
	end
	local target = vehicle
	local how = "free"
	if seat == nil then
		target, seat = coop_seat_fallback(p, vehicle, used)
		how = "moved"
	end
	if seat == nil then
		placed[vehicle] = COOP_SEAT_ON_FOOT
		COOP_SEAT_LAST[p] = COOP_SEAT_ON_FOOT
		coop_seat_trace(label .. "none (on foot)")
		return
	end
	copy[vehicle_at] = target
	copy[seat_at] = seat
	native(unpack(copy, 1, copy.n))
	local placement = { vehicle = target, seat = seat, request = request, k = k }
	placed[vehicle] = placement
	COOP_SEAT_LAST[p] = placement
	COOP_SEAT_CLAIMS[p] = { vehicle = target, seat = seat }
	coop_seat_use(used, target, seat)
	coop_seat_trace(label .. target .. " " .. seat .. " (" .. how .. ")")
end

-- Takes the extra remotes out of a seat that a character is about to enter, and seats them again somewhere else.
function coop_seat_evict(vehicle, seat, char)
	local victims = {}
	local count = 0
	for k, p in ipairs(coop_remote_players()) do
		if k >= 2 and p ~= char and COOP_SEAT_PLACED[p] ~= nil then
			for intended, placement in pairs(COOP_SEAT_PLACED[p]) do
				if placement ~= COOP_SEAT_ON_FOOT and placement.vehicle == vehicle and placement.seat == seat then
					count = count + 1
					victims[count] = { player = p, k = k, intended = intended, request = placement.request }
				end
			end
		end
	end
	local used = {}
	coop_seat_use(used, vehicle, seat)
	for i = 1, count do
		local victim = victims[i]
		coop_seat_trace("evict " .. victim.player .. " from " .. vehicle .. " " .. seat .. " for " .. tostring(char))
		COOP_SEAT_PLACED[victim.player][victim.intended] = nil
		COOP_SEAT_CLAIMS[victim.player] = nil
		coop_native(vehicle_exit_teleport)(victim.player)
		coop_seat_put(victim.player, victim.k, victim.request, used)
	end
end

-- Called before every seat call that does not name REMOTE_PLAYER (and before the call for REMOTE_PLAYER itself): records the seat
-- as taken and gives it up from any extra remote that was placed in it.
function coop_seat_claim(name, args)
	if not coop_seat_ready() or coop_count(coop_remote_players()) < 2 then
		return
	end
	local char = args[1]
	local seat = args[COOP_SEAT_ARG[name]]
	local vehicle = args[COOP_SEAT_VEHICLE_ARG[name]]
	if name == COOP_SEAT_FORCE then
		COOP_SEAT_FORCED[char] = nil
		if type(seat) == "number" and seat >= 0 and vehicle ~= nil then
			COOP_SEAT_FORCED[char] = { vehicle = vehicle, seat = seat }
		end
		return
	end
	if vehicle == nil or type(seat) ~= "number" or seat < 0 then
		return
	end
	COOP_SEAT_CLAIMS[char] = { vehicle = vehicle, seat = seat }
	coop_seat_evict(vehicle, seat, char)
end

-- A forced seat of an extra remote: a free candidate seat of the vehicle; none, or no vehicle, forces nothing (-1: any free seat).
function coop_seat_force(native, p, k, copy, used)
	local seat = copy[2]
	local vehicle = copy[3]
	COOP_SEAT_FORCED[p] = nil
	if type(seat) ~= "number" or seat < 0 then
		native(unpack(copy, 1, copy.n))
		return
	end
	local chosen = nil
	local n = 0
	if vehicle ~= nil then
		chosen, n = coop_seat_pick(vehicle, seat, used)
	end
	local label = p .. " " .. tostring(vehicle) .. " " .. seat .. " -> "
	if vehicle ~= nil and n == 0 then
		copy[2] = seat + k - 1
		COOP_SEAT_FORCED[p] = { vehicle = vehicle, seat = copy[2] }
		native(unpack(copy, 1, copy.n))
		coop_seat_trace("force " .. label .. copy[2])
	elseif chosen == nil then
		native(p, -1)
		coop_seat_trace("force " .. label .. "-1")
	else
		copy[2] = chosen
		COOP_SEAT_FORCED[p] = { vehicle = vehicle, seat = chosen }
		coop_seat_use(used, vehicle, chosen)
		native(unpack(copy, 1, copy.n))
		coop_seat_trace("force " .. label .. chosen)
	end
end

-- The rule of the seat calls: the k-th remote after the first gets a seat of its own; a seat below 0 (a release) or none is not moved.
function coop_seat_place(native, args, at, remotes, name)
	local seat_at = COOP_SEAT_ARG[name]
	local vehicle_at = COOP_SEAT_VEHICLE_ARG[name]
	local ready = coop_seat_ready()
	local request = { name = name, args = args, at = at }
	local used = {}
	local first = nil
	for k, p in ipairs(remotes) do
		local copy = coop_args_for(args, at, p)
		local seat = args[seat_at]
		local results = nil
		if not ready then
			if type(seat) == "number" and seat >= 0 then
				copy[seat_at] = seat + k - 1
			end
			results = { native(unpack(copy, 1, copy.n)) }
		elseif k == 1 then
			coop_seat_claim(name, copy)
			if name ~= COOP_SEAT_FORCE and type(seat) == "number" and seat >= 0 and args[vehicle_at] ~= nil then
				coop_seat_use(used, args[vehicle_at], seat)
			end
			results = { native(unpack(copy, 1, copy.n)) }
		elseif name == COOP_SEAT_FORCE then
			coop_seat_force(native, p, k, copy, used)
		elseif type(seat) == "number" and seat >= 0 and args[vehicle_at] ~= nil then
			coop_seat_put(p, k, request, used)
		else
			native(unpack(copy, 1, copy.n))
		end
		first = first or results
	end
	return unpack(first)
end

-- The calls that stop remotes from entering vehicles skip the extra remotes that are on foot for lack of a seat: they keep their
-- chance to take a vehicle of their own.
function COOP_RULE.seat_lock(native, args, at, remotes)
	local first = nil
	for k, p in ipairs(remotes) do
		if args[2] == false and COOP_SEAT_LAST[p] == COOP_SEAT_ON_FOOT then
			coop_seat_trace("keep entry " .. p .. " (on foot)")
		else
			local copy = coop_args_for(args, at, p)
			local results = { native(unpack(copy, 1, copy.n)) }
			first = first or results
		end
	end
	return unpack(first or {})
end

-- character_is_in_vehicle for a remote that was given a seat of another vehicle asks about that vehicle; one that stayed on foot
-- answers as REMOTE_PLAYER does, which changes neither an "all" nor an "any" over the remotes.
function coop_seat_in_vehicle(...)
	local p = arg[1]
	local vehicle = arg[2]
	local placed = COOP_SEAT_PLACED[p]
	local placement = nil
	if placed ~= nil and vehicle ~= nil then
		placement = placed[vehicle]
	end
	if placement == nil then
		return COOP_SEAT_RAW.in_vehicle(unpack(arg, 1, arg.n))
	end
	local where = "mirror"
	local answer = nil
	if placement == COOP_SEAT_ON_FOOT then
		answer = COOP_SEAT_RAW.in_vehicle(REMOTE_PLAYER, vehicle)
	else
		where = placement.vehicle
		answer = COOP_SEAT_RAW.in_vehicle(p, where)
	end
	local key = p .. " " .. vehicle
	if COOP_SEAT_ASKED[key] ~= tostring(answer) then
		COOP_SEAT_ASKED[key] = tostring(answer)
		coop_seat_trace("ask " .. p .. " in " .. vehicle .. " -> " .. where .. " (" .. tostring(answer) .. ")")
	end
	return answer
end

-- The seat player was given in vehicle (the vehicle the script names), for the waits that follow a seat call: first_seat for the first
-- remote, whose seat is the script's, the seat of its placement, nil for a remote that stayed on foot or went to another vehicle.
function coop_seat_of(vehicle, player, first_seat)
	local placed = COOP_SEAT_PLACED[player]
	local placement = nil
	if placed ~= nil then
		placement = placed[vehicle]
	end
	if placement == nil then
		if player == coop_remote_players()[1] then
			return first_seat
		end
		return nil
	end
	if placement == COOP_SEAT_ON_FOOT or placement.vehicle ~= vehicle then
		return nil
	end
	return placement.seat
end

-- True when fn(p, vehicle) is true for every remote player that has a seat in vehicle.
function coop_all_seated(fn, vehicle)
	for i, p in ipairs(coop_remote_players()) do
		if coop_seat_of(vehicle, p, 0) ~= nil and not fn(p, vehicle) then
			return false
		end
	end
	return true
end

COOP_RULE.seats = coop_seat_place
COOP_SEAT_CLAIM.vehicle_enter_teleport = coop_seat_claim
COOP_SEAT_CLAIM.vehicle_enter = coop_seat_claim
COOP_SEAT_CLAIM.player_force_vehicle_seat = coop_seat_claim
