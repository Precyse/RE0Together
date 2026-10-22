-- A sync mask (SYNC_LOCAL 1, SYNC_REMOTE 2, SYNC_ALL 3) names the local player and the whole remote side. For
-- customization_item_wear and inv_weapon_enable_or_disable_all_slots the adapter also reads a player number above those
-- two bits and sends the remote part to that player only; the game's natives test only the two bits.
COOP_SYNC_TARGET_STEP = 16
COOP_PLAYER_NUMBER = {
	["#PLAYER2#"] = 2, ["#PLAYER3#"] = 3, ["#PLAYER4#"] = 4, ["#PLAYER5#"] = 5, ["#PLAYER6#"] = 6, ["#PLAYER7#"] = 7,
	["#PLAYER8#"] = 8, ["#PLAYER9#"] = 9, ["#PLAYER10#"] = 10, ["#PLAYER11#"] = 11, ["#PLAYER12#"] = 12,
}

-- sync_table[player], its remote part narrowed to that player while there is more than one remote player.
function coop_sync_for(sync_table, player)
	local sync = sync_table[player]
	local number = COOP_PLAYER_NUMBER[player]
	if number == nil or (sync ~= SYNC_REMOTE and sync ~= SYNC_ALL) or coop_count(coop_remote_players()) < 2 then
		return sync
	end
	return sync + COOP_SYNC_TARGET_STEP * number
end
