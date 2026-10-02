# DEATH STRANDING 2 adapter

`version.dll` proxy for DEATH STRANDING 2 (Steam 3280350): each player's Sam is sent as PLAYER_STATE (0x0100) at 60 Hz, and every peer shows up as a labelled marker above their position, drawn over the game's DX12 frames. Build and file layout: `CODEMAP.md`. Research notes and the one-PC test: `docs/DS2_NOTES.md`.

Settings in `<game dir>\coop\adapter.ini`:

```
port=27980
overlay=1
self_marker=0
remote_body=0
```

`self_marker=1` also marks your own Sam (checks the projection). `mirror_animation=1` (test) makes the partner body copy your own animation, standing beside you. `remote_body=1` gives the partner a body: a second player entity (own camera, Sam's costume, the game's own walking and vehicle get-in and get-off) that follows the partner and rides the vehicle the partner drives.

The host moves cargo between the two backpacks: F7 opens the menu (your backpack and the guest's), the arrow keys pick a piece and choose the side, Enter moves it across. The game does not see those keys while the menu is open.

Remove `version.dll` and `coop\` from the game folder to play vanilla.
