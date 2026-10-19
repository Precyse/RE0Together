-- Which rule (see coop_wrappers.lua) each function follows when it is called with REMOTE_PLAYER.
-- Functions that keep the default partner (party_add, cutscene_play, get_char_vehicle_*, mission_debug) are not listed.

COOP_RULES = {
	fanout = {
		"player_controls_enable", "player_controls_disable", "teleport", "inv_weapon_add_temporary", "inv_weapon_remove_temporary",
		"turn_vulnerable", "turn_invulnerable",
		"character_evacuate_from_all_vehicles", "inv_item_equip", "character_set_damage_multiplier", "character_ragdoll",
		"vehicle_stop_do", "inv_item_remove_in_slot", "character_kill", "character_set_cannot_wield_havok_weapon",
		"set_player_can_be_busted", "cash_add", "cash_remove", "clear_animation_state", "crouch_start",
		"inv_item_remove_all", "player_warp_to_shore_disable", "player_warp_to_shore_enable", "vehicle_mark_as_players",
		"inv_item_add", "vehicle_exit_teleport", "vehicle_exit_dive", "character_release_human_shield", "thread_new",
		"on_vehicle_enter", "on_vehicle_exit", "on_weapon_fired", "on_take_damage", "on_district_changed",
		"on_weapon_pickup", "on_weapon_equip", "on_searchlight_track", "on_damage", "on_notoriety_event",
	},
	seats = { "vehicle_enter_teleport", "player_force_vehicle_seat", "vehicle_enter" },
	group = { "vehicle_enter_group_teleport" },
	target = { "attack", "vehicle_chase", "turn_to_char", "force_throw_char" },
	all = {
		"teleport_check_done", "character_is_ready", "character_is_in_a_driver_seat",
		"inv_item_is_equipped", "inv_has_item", "inv_item_is_firearm_equipped", "character_has_specific_human_shield",
		"character_has_human_shield", "character_is_dead", "navpoint_in_player_fov", "npc_in_player_fov", "los_check",
	},
	seat_lock = { "set_player_can_enter_exit_vehicles" },
	nearest = { "get_dist" },
}

-- ug_lib functions that only call get_dist: wrapped around get_dist itself, so a call for one remote player is not
-- widened again by the get_dist wrapper.
COOP_DIST_ALIASES = { "get_dist_char_to_nav", "get_dist_char_to_char", "get_dist_char_to_vehicle" }

for rule, names in pairs(COOP_RULES) do
	for i, name in ipairs(names) do
		coop_wrap(name, rule)
	end
end

for i, name in ipairs(COOP_DIST_ALIASES) do
	coop_wrap(name, "nearest", COOP_NATIVE.get_dist)
end

-- The query follows the seat each remote player was given (coop_seats.lua).
coop_wrap("character_is_in_vehicle", "all", coop_seat_in_vehicle)
