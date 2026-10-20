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

`partner_mode=input|state` (default `input`, other values are logged and ignored) chooses how the remote-owned character is shown. `input` is the replay above and installs none of the state code. `state` is experimental (phase 1 of `docs/re/RE0_POSE_REPLICATION.md`): the owner sends its character's sampled pose (POSE_STATE 0x0140, 30 Hz) and the motion requests its engine makes (MOTION_EVENT 0x0141), and the other machine shows them on a puppet that runs the engine's script-pause mode during its own move (`docs/RE0_NOTES.md` "Pose puppet"). Both machines must use the same mode: it is a host option of the launcher (`Partner mode` in the game's options; the guest's launcher writes the host's value into its adapter.ini), and a peer on the other mode is named once in the log (`state_sync: the peer runs partner_mode=...`) and by a toast. Weapons, grabs, scripts aimed at the puppet and doors are later phases.

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
| `config: partner_mode=state` | the mode is on; `adapter: state mode incomplete` (and the toast `State mode incomplete: <modules>`) instead means a hook failed: the listed modules are off and the rest still run, so without the puppet the remote character has no driver (state mode sends no PAD_FRAME); set `partner_mode=input` on both machines |
| `state_mode: on, 18 modules enabled` | the state modules came up; `scene: load serial N (scene 0x..)` appears once per room change; F8 `state refused` and `router dropped` stay 0 and `pad sent/s` stays 0 (no PAD_FRAME in this mode); `state_net: refused` or `router: dropped` lines are failures |
| `puppet: Billy is shown from the owner's pose` | the puppet took over; `puppet: Billy released` when it ends (peer left, or the owner left the room) |
| `puppet: Billy motion 0x.... (owner 0x....) frame f (owner f) think g/s/p (owner g/s/p) mode flags A -> B -> C, events in N, misses M, seeks S, delay Dms, poses buffered P`, once a second | pass: motion and frame follow the owner's within a few frames, B has bit 0x2 set and A == C (the pause bit is back as it was), N grows with your actions, S stays small while you move calmly, D stays near 100 |
| `puppet: Billy timeline reset (serial)` | once per door or room change (`room`, `gap`, `lead`, `depth`, `clock` are rarer; `teleport` in plain running is a failure: send the log) |
| `puppet: Billy stale, holding` / `puppet: Billy fresh again after N ms` | the owner sent nothing for 2 s (stop and restart `echo_peer.py` to see it); not expected while a menu is open (held poses keep it fresh, F8 `puppet poses in/s` about 4) |
| `puppet: Billy has no motion 0x.... loaded` | a miss: that request was skipped |
| `weapon_fx: sent N entries (M unlisted calls skipped)`, once a second while you fire | pass: N grows with your shots; M stays small and steady (footsteps and other effects of your character count there); a burst of M while firing names a call site missing from `weapon_fx_sites.h` |
| `weapon_fx: Billy replayed sound site N` (and `muzzle flash`, `shoot`, `effect`, `stow`, `wick`, `ripple`), once per site | the puppet showed that part of a shot; `weapon_fx: Billy ... site N failed` or `projectile of weapon A, puppet holds B` is a miss: send the log |
| F8 `fx out`, `fx in`, `fx unlisted` | in the echo test `fx in` equals `fx out` (entries sent and received); `fx unlisted` counts calls of your character that no listed site carries |
| `game_tick: 'pose_capture' disabled`, a `pose_game:` line with an exception code, `(reset faulted)`, a "Sync stopped" toast, a `crash-*.dmp` | fail: send the log and the dump |

## One-PC state tests with the fake peer

`tools\re0\fake_peer` (Python 3, standard library only) plays the second machine of a `partner_mode=state` session on the same PC, answers the way a real peer would, and judges the game's half from `<game dir>\coop\adapter.log`. The echo peer above stays for the plain echo test; the fake peer's `t2_echo` is the same echo with the log checks of test plan T2 added. It never starts or touches the game, sends no input, and only the host seat's join snapshot reads the running game's memory (read only, like `fake_session.py`).

Install the adapter and set `partner_mode=state` in `adapter.ini` as for the echo test (`partner_mode=input` for `t1_input`), then from the repo or package root:

```
python tools\re0\fake_peer list
python tools\re0\fake_peer run t2_echo
```

With the game as host the fake starts the two launchers itself (`launcher\test\coop-launcher.exe`, else `launcher\bin\Release\net8.0-windows\coop-launcher.exe`, or `--launcher PATH`; `--save-source DIR` if Steam gives the host launcher no save folder), prints numbered steps, prints `ready`, and you start the game from Steam. With the game as guest (`--seat host`) nothing else runs and the fake listens on port 27960 as the game's launcher. `--seat host --transport launchers` is experimental (both launchers share the game folder), `--transport bridge --bridge-port N` joins a launcher you started yourself.

| scenario | command | seat |
|---|---|---|
| `t1_input` | `run t1_input` | guest |
| `t2_echo` | `run t2_echo` (`--delay 1.0 --offset-x 150`) | guest |
| `t4_join` | `run t4_join`, `run t4_join --seat host` | both |
| `t5_observe`, `t5_observer` | `run t5_observe`, then `run t5_observer` (replays the room `t5_observe` recorded) | guest (`t5_observer` also host) |
| `t6_authority` | `run t6_authority` | guest |
| `t7_flags` | `run t7_flags`, `run t7_flags --seat host --scratch` | both |
| `t8_contact`, `t8_victim` | `run t8_contact`, then `run t8_victim --scratch` | guest |
| `t9_rooms` | `run t9_rooms` | guest |
| `t10_props` | `run t10_props`, `run t10_props --seat host --scratch` | both |
| `t11_boss` | `run t11_boss`; `run t11_boss --seat host --round door`, `--round vanish` | both |
| `t12_items` | `run t12_items --scratch`, `run t12_items --seat host` | both |
| `t13_death_save` | `run t13_death_save --scratch` | guest |
| `t14_link` | `run t14_link` (the shaper: `--delay-ms 40 --jitter-ms 30 --loss 0.02` is built in) | guest |
| `record` | `run record` (Enter marks the start and end of a pose clip) | guest |

Scenarios that change the save's state need `--scratch` on a scratch save and say so first. Run in this order to fill the recordings later scenarios replay: `t2_echo`, `t4_join`, `t9_rooms`, `t5_observe`, `t8_contact`, `t12_items`, `t10_props`, `t7_flags`, `t13_death_save`, `t11_boss`, `t14_link`; a scenario with no recording for something skips that gate and names the run that fills it. `python tools\re0\fake_peer gates <scenario> [--seat host]` prints a scenario's gates, `library build` re-indexes the recordings.

Each run writes `<game dir>\coop\fake_peer\<date>-<time>-<scenario>\` with `report.txt` and `report.json`, `frames.jsonl` (every frame both ways), both launchers' output, the adapter log lines of the run and any new crash dump. The report is one line per gate:

```
PASS T4.host-echo ...
FAIL T9.R2-busy ...      the offending log line quoted
MISSING T5.enter ...     an expected line never came
SKIP T10.fake-push ...   needs a box recording: run t10_props
MANUAL T2g.torso ...     your yes / no
totals PASS 12 FAIL 0 MISSING 1 SKIP 1 MANUAL 3
```

The exit code is 1 when any gate failed or is missing. Send `report.txt`, the run folder's `adapter_segment.log` and any dump for every FAIL or MISSING.

Checking the tool itself needs no game: `python tools\re0\fake_peer selftest` (wire layouts against the adapter headers, golden frames, gate anchors against the sources, unit tests) and `selftest --smoke` (two fakes against each other through two real launchers on ports 28960 to 28970). `adapters\re0\build\fake_peer_wire_test.exe` runs the adapter's own decoders over `tools\re0\fake_peer\tests\golden_frames.bin`; regenerate it with `python tools\re0\fake_peer golden` after a wire change.
