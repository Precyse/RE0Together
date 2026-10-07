# DEATH STRANDING 2 adapter

`version.dll` proxy for DEATH STRANDING 2 (Steam 3280350): each player's Sam is sent as PLAYER_STATE (0x0100) at 60 Hz, and every peer shows up as a labelled marker above their position, drawn over the game's DX12 frames. Build and file layout: `CODEMAP.md`. Research notes and the one-PC test: `docs/DS2_NOTES.md`.

Settings in `<game dir>\coop\adapter.ini`:

```
port=27980
overlay=1
self_marker=0
remote_body=1
```

`self_marker=1` also marks your own Sam (checks the projection). `mirror_animation=1` (test) makes the partner body copy your own animation, standing beside you. `remote_body=1` (the default; `0` turns it off) gives the partner a body: a second player entity (own camera, Sam's costume, the game's own walking and vehicle get-in and get-off) that follows the partner and rides the vehicle the partner drives.

`enemy_sync=1` (on by default; `0` turns it off) shares the host's enemies: the host reports them, a guest's own enemies follow the host's, and a partner's hits on an enemy land on the host.
`gear_restore=1` (off by default) makes a guest keep the gear it gained across sessions (`coop\personal_gear.txt`, given back at the next join).

`weapon_sync=1` (on by default; `0` turns it off) makes the partner's body hold the weapon the partner has drawn and play the partner's shots; `weapon_attach_mode=N` tries another way of attaching that weapon.

Either player moves cargo between the two backpacks (a guest asks the host, which decides): F7 opens the menu (your backpack and the guest's), the arrow keys pick a piece and choose the side, Enter moves it across. The game does not see those keys while the menu is open.

F6 warps you beside the partner (refused while you are dead, riding or driving, in a cutscene, a menu or a loading screen).

Remove `version.dll` and `coop\` from the game folder to play vanilla.
