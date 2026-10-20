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

`partner_mode=input|state` (default `input`, other values are logged and ignored) chooses how the remote-owned character is shown. `input` is the replay above and installs none of the state code. `state` is experimental (phase 1 of `docs/re/RE0_POSE_REPLICATION.md`): the owner sends its character's sampled pose (POSE_STATE 0x0140, 30 Hz) and the motion requests its engine makes (MOTION_EVENT 0x0141), and the other machine shows them on a puppet that runs the engine's script-pause mode during its own move (`docs/RE0_NOTES.md` "Pose puppet"). Both machines must use the same mode. Weapons, grabs, scripts aimed at the puppet and doors are later phases.

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

## One-PC echo test of `partner_mode=state`

Same setup as above with `coop=1` and `partner_mode=state` in `<game dir>\coop\adapter.ini`, and the echo peer started with an offset so the puppet stands beside you:

```
python ..\tools\echo_peer.py --port 27970 --delay 1.0 --offset-x 150
```

The host plays Rebecca; the fake peer owns Billy, so Billy is the puppet and repeats Rebecca's pose and motion requests about 1.1 s late (1 s echo + 0.1 s render delay), 150 units to the side. Load a save in a room with one zombie and, for two minutes, walk, run, aim at the three heights, fire, reload, use the knife, pick up an item and take a hit. Billy's motion banks differ from Rebecca's, so some of her motion numbers are not loaded for him: misses are expected, and a bind-pose Billy for those moments is not a failure. Read `<game dir>\coop\adapter.log` (the F8 panel shows the same counters as `puppet poses in/s`, `puppet events in/out` and `puppet seeks/misses`):

| line | meaning |
|---|---|
| `config: partner_mode=state` | the mode is on; `adapter: state mode unavailable` instead means a hook failed (the game runs as in `input`, no puppet) |
| `state_mode: on, 14 modules enabled` | the state modules came up; `scene: load serial N (scene 0x..)` appears once per room change; F8 `state refused` and `router dropped` stay 0 and `pad sent/s` stays 0 (no PAD_FRAME in this mode); `state_net: refused` or `router: dropped` lines are failures |
| `puppet: Billy is shown from the owner's pose` | the puppet took over; `puppet: Billy released` when it ends (peer left, or the owner left the room) |
| `puppet: Billy motion 0x.... (owner 0x....) frame f (owner f) think g/s/p (owner g/s/p) mode flags A -> B -> C, events in N, misses M, seeks S, delay Dms, poses buffered P`, once a second | pass: motion and frame follow the owner's within a few frames, B has bit 0x2 set and A == C (the pause bit is back as it was), N grows with your actions, S stays small while you move calmly, D stays near 100 |
| `puppet: Billy timeline reset (serial)` | once per door or room change (`room`, `gap`, `lead`, `depth`, `clock` are rarer; `teleport` in plain running is a failure: send the log) |
| `puppet: Billy stale, holding` / `puppet: Billy fresh again after N ms` | the owner sent nothing for 2 s (stop and restart `echo_peer.py` to see it); not expected while a menu is open (held poses keep it fresh, F8 `puppet poses in/s` about 4) |
| `puppet: Billy has no motion 0x.... loaded` | a miss: that request was skipped |
| `weapon_fx: sent N entries (M unlisted calls skipped)`, once a second while you fire | pass: N grows with your shots; M stays small and steady (footsteps and other effects of your character count there); a burst of M while firing names a call site missing from `weapon_fx_sites.h` |
| `weapon_fx: Billy replayed sound site N` (and `muzzle flash`, `shoot`, `effect`, `stow`, `wick`, `ripple`), once per site | the puppet showed that part of a shot; `weapon_fx: Billy ... site N failed` or `projectile of weapon A, puppet holds B` is a miss: send the log |
| F8 `fx out`, `fx in`, `fx unlisted` | in the echo test `fx in` equals `fx out` (entries sent and received); `fx unlisted` counts calls of your character that no listed site carries |
| `game_tick: 'pose_capture' disabled`, a `pose_game:` line with an exception code, `(reset faulted)`, a "Sync stopped" toast, a `crash-*.dmp` | fail: send the log and the dump |
