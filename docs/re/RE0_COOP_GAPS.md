# RE0 co-op gaps (audit 2026-10-06)

What real two-player play needs, checked against the code and the static notes (`docs/RE0_NOTES.md`). Nothing here
was run in game; "done" means the code path exists, "open" means not built or not proven.

## Done

| need | where |
|---|---|
| Both players always have their own camera, own character, own pad replay | `camera_parity`, `character_owner`, `net_pad` |
| Doors together and split rooms, spawn spots that do not overlap | `door_sync`, `door_travel`, `split_rooms`, `spot_rule.h` |
| Story flags, puzzle progress, doors unlocked once, events seen (0x47 words, includes the enemy-killed bits) | `flag_sync` |
| Inventories, ammo, equipped weapon, item exchange in a menu | `inventory_sync`, `equip_refresh`, `menu_mirror` |
| Floor items put and taken, both directions, held for a room that is not ready | `floor_items_sync`, `floor_pending`, `pickup_guard` |
| Partner health and HP changes (HUD shows both characters), only the owner changes a character's HP | `state_sync`, `state_correction`, `player_damage` |
| One player dies: the game's own death flow runs on both, guest follows the host through game over | `player_damage` (PLAYER_DIED), `auto_join`, `session_slot` |
| Typewriter save: host's save is shared with guests, solo saves are never overwritten (co-op slot) | `session_slot`, `remote_storage_proxy` |
| Enemies the same on both screens: owner runs the AI, the other machine shows puppets (pose, HP, behaviour record, target); kills and damage are owner-only | `enemy_state`, `enemy_action`, `enemy_target`, `enemy_damage_hook`, `enemy_net` |
| Enemies killed in one player's room stay dead for both | flags: an enemy reads bit `0x612 + spawn id` at its first update, scripts set it, `flag_sync` carries it (notes, "kill persistence") |
| Cutscenes and scripted moves of the other character | `event_place` |
| Joining a game in progress | `join_sync` |

## Added in this round

- Enemy AI: puppets skip the base classes' think step (0x41db20), the update restores the behaviour record after the
  classes' direct state writes, both target selectors (0x421b20, 0x439e90) follow the owner's target.

## Not needed

- Item box: RE0 has none; items are dropped on the floor (covered by floor sync).
- Downed / revive: RE0 has no such state; a death is a game over.
- Local script flags (sEventScript +0x58): script-internal ordering, not saved; see the notes for why they are not synced.

## Open

- Guest typewriter saves stay on the guest's own disk (the host's save is the shared one). Whose save wins after a game
  over is the host's by design; a guest who saves expects it to count and it does not.
- World objects whose state lives in the unit and not in a flag (pushable or shootable props, moving platforms, doors
  that animate after a script) are not synced; only the flags they set are. Not audited object by object.
- Enemy classes outside the 15 base ones still run their own decision code on a puppet (only the setAction calls and
  the record are held to the owner); see the notes for why no think step could be skipped.
- Everything above about enemies needs a two-PC session to confirm: watch for puppets that stand still, snap, or
  repeat an animation, and for a puppet attacking the other character than the owner's target.
