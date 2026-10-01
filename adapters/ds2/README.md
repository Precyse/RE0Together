# DEATH STRANDING 2 adapter

`version.dll` proxy for DEATH STRANDING 2 (Steam 3280350): each player's Sam is sent as PLAYER_STATE (0x0100) at 60 Hz, and every peer shows up as a labelled marker above their position, drawn over the game's DX12 frames. Build and file layout: `CODEMAP.md`. Research notes and the one-PC test: `docs/DS2_NOTES.md`.

Settings in `<game dir>\coop\adapter.ini`:

```
port=27980
overlay=1
self_marker=0
```

`self_marker=1` also marks your own Sam (checks the projection). Remove `version.dll` and `coop\` from the game folder to play vanilla.
