# RE0 adapter

dinput8.dll proxy for RE0 HD. Build and file layout: `CODEMAP.md`. Settings live in `<game dir>\coop\adapter.ini`:

```
port=27960
trace=0
trace_vtables=
coop=1
overlay=1
```

`coop=1` sends the local pad as PAD_FRAME (0x0101) every frame and assigns each character (Billy, Rebecca) to one player. The host always owns Rebecca and the first peer always owns Billy (OWNERSHIP 0x0102); each machine always keeps its own character focused (its own camera), in Team and in Split up; E / LT only decides door travel, and V / Y do nothing while a peer is connected. A remote-owned character gets a player think, is driven from its owner's pad during its move, and is snapped to its owner's reported position when it drifts more than 60 units (same room only).

`overlay=1` installs the D3D9 hooks and makes a status panel available in the top-left corner; it is hidden until F8 toggles it while the game window is focused. With a partner connected, a one-line status sits in the top-right corner: name, condition, same room or its room, in menu. `overlay=0` installs no D3D hooks.

`auto_join=1` drives a guest through the boot, title and load screens into the host's game; without it the guest still follows the host's Continue at game over.

F9 in gameplay, or creating `<game dir>\coop\resync_now.txt`, on either machine makes the guest take a new join snapshot (rooms, inventories, flags, floor items); the file is deleted when taken. A room desync that lasts 10 s does the same by itself. A resync also restarts any sync that stopped on an exception (the "Sync stopped" toast).

## One-PC test with the echo peer

The game plays the host; `echo_peer.py` plays the remote player and mirrors your own pad and position back after 1 s, so the character you do not control repeats what you did a second ago. The echo carries PLAYER_STATE with your own character id and the host flag, and OWNERSHIP is not echoed, so the test exercises input replay only: snapping ignores the echoed state (your character is locally owned) and no camera swap happens on the host. From `launcher\`:

```
coop-launcher host re0 --transport local --local-port 27961 --peer-port 27962 --no-launch
coop-launcher join 0 --transport local --local-port 27962 --peer-port 27961 --bridge-port 27970 --no-launch
python ..\tools\echo_peer.py --port 27970 --delay 1.0
```

Start the game (with `coop=1`) after the host launcher is running; it connects to the profile port 27960. Without the game, `adapters\re0\build\net_test.exe --port 27960 --seconds 6 --expect-peer` stands in for it and prints PASS when PLAYER_STATE and PAD_FRAME both came back.
