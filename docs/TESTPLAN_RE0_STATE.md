# RE0 state mode live tests (branch re0-state)

Probe ids (X1, A1, ...) are from STATE_PLAN.md section 5. Logs: `<game>\coop\adapter.log`; on the host the guest's log arrives as `<game>\coop\peer_<id>.log`. F8 toggles the status panel (Page Up / Page Down scroll). Send both logs and any `crash-*.dmp` for every fail.

## Setup (once per PC, game closed)

1. Extract the zip. Run `launcher\test\coop-launcher.exe` (never `launcher\app`), pick Resident Evil 0 HD, click Install. Close the launcher.
2. Copy `adapter.ini` to `<game>\coop\adapter.ini` (create `coop`). It holds `coop=1`, `overlay=1`, `partner_mode=state`, `trace=0`, `auto_join=0`. Both PCs use the same ini. The launcher only rewrites `coop` and the option keys.
3. Uninstall afterwards: launcher Settings, the game's section, Uninstall (restores the original dinput8.dll).

Every test below starts from a fresh `adapter.log` (delete it before launching the game).

## ONE-PC

### T1 Input-mode regression (partner_mode=input)

Setup: set `partner_mode=input` in `<game>\coop\adapter.ini`. Run the echo setup (T2 commands, but without `--offset-x`). Load a save, play 3 min (walk, fire, door, menu).

| result | line |
|---|---|
| pass | no `state_mode:` line, no `router:` line, no `puppet:` line; F8 `state refused` 0, `router dropped` 0, `pad sent/s` above 0; Billy repeats Rebecca's pad about 1 s late as in the shipped build |
| fail | any `state_mode:`, `state_net:`, `router:` line; `pad sent/s` 0 |

### T2 Echo test (partner_mode=state)

Setup: `partner_mode=state`. From `launcher\test\` in three terminals, then start the game from Steam (host plays Rebecca, Billy is the puppet):

```
coop-launcher host re0 --transport local --local-port 27961 --peer-port 27962 --no-launch
coop-launcher join 0 --transport local --local-port 27962 --peer-port 27961 --bridge-port 27970 --no-launch
python ..\..\tools\echo_peer.py --port 27970 --delay 1.0 --offset-x 150
```

Load a save in a room with one zombie. For 2 min: walk, run, aim low/mid/high, fire, reload, knife, pick up an item, take a hit. Then the sub-tests:

| step | do | pass | fail |
|---|---|---|---|
| a. start | load game | `config: partner_mode=state`; `state_mode: on, 18 modules enabled`; `puppet: Billy is shown from the owner's pose`; F8 `pad sent/s` 0, `state refused` 0 | `adapter: state mode unavailable`; any `state_net: refused` or `router: dropped` line |
| b. motion (E1) | the 2 min above | 1 Hz `puppet: Billy motion ...` line: motion and frame follow Rebecca's within a few frames, mode flags `A -> B -> C` with B having bit 0x2 and A == C, `delay` near 100 ms, misses only for Rebecca-only motions | A != C; `teleport` while running in a straight line (note the distance logged) |
| c. door | walk through a door and back | exactly one `timeline reset (serial)` per door, `scene: load serial N` once per room change, no slide across the room | repeated resets, `teleport`, `gap` |
| d. menu | open the inventory 5 s | F8 `puppet poses in/s` about 4, no `stale` line | `Billy stale, holding` |
| e. stale | stop `echo_peer.py` 3 s, restart it | `Billy stale, holding` then `Billy fresh again after N ms` | no recovery |
| f. weapons | fire handgun, shotgun, grenade rounds, magnum, SMG, molotov, knife | `weapon_fx: sent N entries` grows with shots, `weapon_fx: Billy replayed sound site/muzzle flash/shoot/effect ...` lines, F8 `fx in` == `fx out`, `fx unlisted` small and steady | `weapon_fx: Billy ... site N failed`, `projectile of weapon A, puppet holds B`, burst of unlisted calls while firing |
| g. body (W3) | tilt aim slowly up and down; fire; reload; switch weapons in the menu | Billy's torso follows the tilt, slide/magazine move, F8 `puppet sub events` grows, model changes within about 1 s, `puppet: Billy given a controller think` once | `sub-control 3 has no unit` after an equip settles; bind pose that persists |
| h. fps | cap the game at 30 fps once | `puppet poses in/s` about 30 at 60 fps and at 30 fps | much lower |
| i. Wesker (B1) | `outfit_billy=a5` in the ini, 1 min standing/walking/aiming | nothing odd on Billy | `pose_game:` line or caught exception |

Global fail for T2: `game_tick: 'pose_capture' disabled`, a `pose_game:` line with an exception code, `(reset faulted)`, a "Sync stopped" toast, a `crash-*.dmp`.

### T3 Probe F1 (solo, coop=0)

Setup: set `coop=0` (or no peer), game running, zombie room. Run from the package root:

```
python tools\re0\player_fields_probe.py --hz 10 --seconds 180 > fields.txt
```

Get grabbed from the front, bitten from behind, mash free, and get freed by a partner-AI shot (or your own shot). Send `fields.txt`.

| result | line |
|---|---|
| expect | +0x1034 is 0 in normal play; group 2 states 6 / 9 in holds; +0x69cc runs 1 / 2 / 3 |
| report | every non-zero +0x1034 with its moment |

### T15 Co-op entry on the title screen (one PC; either partner_mode, no partner needed)

Setup: `coop=1`, `overlay=1`, any `partner_mode`, delete `adapter.log`, start the game with no partner connected. Run the steps on the title screen's main menu (after Press Start). Send `adapter.log` and a screenshot of the menu and of the page for every fail.

| step | do | pass | fail |
|---|---|---|---|
| a. hooks | start the game, open the log | `hooks: uGUITitle::v8 hooked`; on reaching the main menu `title_label: Co-op drawn into the <lang> title atlas` (German also logs `has no letters for Co-op, drawn with the system font`); no `title_coop:` or `title_label:` line about unexpected code or a lock failure, no `adapter: Co-op menu entry unavailable` | any of those lines |
| b. entry | the main menu | five rows: Load Game, New Game, Options, Extras, Co-op; the menu panel and spacing fit five rows (the game's own five-row layout), also on the first menu after boot (the save check's opening); keyboard, pad, mouse wheel and mouse hover move over the new row and wrap from Co-op to Load Game | four rows, "Online Store" label, a garbled or clipped label, panel sized for four rows, cursor skips or sticks |
| c. open | confirm on Co-op (pad confirm, Enter, left click on the row) | the decide sound, then the Co-op page centred over the title: title, the rows Partner / Connection / Ping / Mode / Character / Outfit / Host, a separator, Back highlighted; the Steam overlay store does not open; no game starts | the Steam store opens, a load list, a difficulty menu or a new game starts, nothing happens |
| d. values | read the rows with no partner, then with one (T2 or a two-PC session) | no partner: Partner `none`, Connection `waiting for partner` (or `link down` without the launcher), Ping `none`, Mode = `partner_mode` of adapter.ini, Character / Outfit `none` or the planned character, Host `you` or `none`; with a partner: its name, `connected`, a ping that moves, the host's name or `you` | wrong mode or host, ping stuck |
| e. back | on the page press confirm; reopen it and press cancel (pad B, Esc, right mouse button) | each press closes the page and the main menu takes input again with the cursor on Co-op; the log has one `coop_page: opened` and one `coop_page: closed` per visit; the closing press does not reopen the page or choose another row | stuck menu, cursor on another row, the page reopens at once, the title goes back to Press Start |
| f. no input bleed | on the page press up, down, left, right, confirm and cancel in turn; after returning press nothing for 2 s | the page ignores directions; no row was chosen while it was open (no load list, difficulty menu, Options, Extras) and the menu cursor did not move | anything else opens, the cursor moved |
| g. repeat and leave | open and close the page 5 times; then Options, Extras and Load Game in turn and back; cancel to Press Start and return to the menu; start a new game, quit to the title | the entry and the page work every time; every other row works as vanilla; Co-op is there again after each return, with its label | row gone, "Online Store" back, crash |
| h. language | change the language in Options (each of English, French, German, Italian, Spanish, Japanese), return to the main menu | the label reads Co-op in the same lettering as the other rows (Japanese CO-OP; German in a slightly different system font); the page still works | label lost, garbled, letters on different baselines or uneven spacing |
| i. coop=0 / overlay=0 unchanged | set `coop=0`, start the game; then `coop=1` with `overlay=0` | vanilla four-row main menu, no `title_coop:` / `title_label:` / `coop_page:` lines (with `overlay=0` the log has `adapter: overlay off, no Co-op entry on the title`) | any difference from vanilla |

## TWO-PC

Setup: both PCs install (Setup above), same build. Host: `character=rebecca` (default). Host opens `launcher\test\coop-launcher.exe`, Host, sends the code; guest Join. Both launcher logs: `Fresh channel: 3 types`. Wi-Fi guest is fine. Start with a fresh save the host owns; guest gets it automatically.

### T4 Join, ready, F9 (phase 16)

Guest joins mid-game.

| result | line |
|---|---|
| pass | host: `join_sync: snapshot sent`, `join_extras: sent serial 1`, later `session_ready: echoed`; guest: `join_sync: caught up in scene`, `join_extras: applied serial 1`, `session_ready: sent`, `session_ready: echo received`; Billy and Rebecca stand in the right rooms on both |
| F9 on guest | a new `join_extras` / `session_ready` pair with serial 2, nothing else changes |
| fail | no `echo received`, wrong rooms, `Sync stopped` toast |

### T5 Enemy observers X1-X3 (phase 7/8)

Room with 3 zombies, guest enters second.

| step | pass | fail |
|---|---|---|
| enter | guest log `observed from slot 0` three times; host log `sending 3 enemies` | fewer observed |
| X1: guest shoots standing, walking, lunging zombies, head shots | zombies move the same on both screens; hits land where drawn; guest kills them once; 1 Hz `enemy_observer:` line shows no `entry refused` and bit-0 before/after equal | hits miss the drawn pose; `entry refused` |
| X2: one room per class (dog, bats, leeches, Hunter-like 39, Lurker-like 30, bosses 31, 37/38, 46) | same look on both; write down anything missing (wings, segments, sparks, sound) | per class note |
| X3: enter second while a script spawns in front of you | `observed` only after the enemy stood up natively; no T-pose / falling frame | T-pose frame |
| unplug link 3 s | `held (stale)`, zombies stand, then `resumed after N ms` | zombies keep walking or never resume |
| X4: kill zombies, bats, leeches | corpses stay on both; removed units vanish on both (`terminal`, `removed` lines) | corpse only on one |

### T6 Room authority A1 (phase 6)

TEAM door into a room with enemies, 5 times (either side).

| result | line |
|---|---|
| pass | both logs show the same `room_authority: ... owner slot S epoch E` line each time, no second owner line within 2 s; shots land once |
| walk out alone (LEAVE_BEHIND) | `released`, then the other's `claimed` |
| fail | owner flips, two owner lines |

### T7 Flags F2 (phase 14)

| step | pass | fail |
|---|---|---|
| normal progress (keys, doors, a cutscene) | F8 `flag commits` counts on both | `flag_seq: corrected` in normal play |
| F2: both toggle the same lever-type flag in the same second, 10 times | both end equal; `corrected` lines only on the guest | ends differ, `corrected` on the host |

### T8 Grabs and lease G1-G3 (phase 9), host authority

| step | pass | fail |
|---|---|---|
| G1: zombie grabs the guest's Billy front and back; guest mashes free | host `contact: slot N (uEnemy10) on Billy kind 0 level 0 -> state 6 (paired)` and `lease: token T slot N to slot 1`; guest `lease: token T started here`; guest `ended (released)`, host `ended (released) hp H`; zombie resumes; host sees the same hold on Billy | no `started here`; zombie stuck |
| host shoots the zombie mid-hold | host `hit for slot N sent to slot 1`; guest `hit slot N applied` and `ended (enemy dead)`; release on both | hold never ends |
| bite | guest HP drops once (PLAYER_STATE on both) | HP drops twice |
| bat / Cerberus plain hit | guest `contact: hit applied to Billy from local slot N`; host `-> state S (plain)` | |
| G1b: Lurker-like 30 grabbing both | second victim gets `hit applied` only (known limit) | |
| G1c: Queen Leech pull 0x18 | lease/pull ends cleanly | |
| G2: guest pulls the network cable 2 s as a zombie grabs | host `ended (expired)` or `(silent)`; zombie resumes, no loop | zombie loops in a hold |
| G3: 10 grabs on Wi-Fi | note time from host `contact:` to guest `lease: ... started here`; no visible gap beyond a step | |

### T9 Rooms R1-R3 (phase 8/10)

| step | pass | fail |
|---|---|---|
| R2: ten TEAM doors from either side | host door gives `door_follow: serial N sent`, guest `door_follow: serial N accepted`; each machine plays its own door; guest log `standing behind`; no carry, no `room_gate` line; Billy behind Rebecca | `Waiting for partner`, carry |
| R2: both press a door in the same second | `rejected (busy)` and `partner rejected (busy)` on both, each in own target, nobody swapped | swapped / stuck |
| R3: LEAVE_BEHIND, guest takes a door | host `room_move: Billy -> scene 0x.. (leave)` within one network delay, Billy vanishes then; guest walks back: `(enter)` with Billy at the door spot; doors into never-visited rooms without a hitch (no SLOW_FRAME over a frame) | late move, hitch |
| split save | loads with each character in its room; self-heal line absent in normal play | self-heal line |
| R1: scripted group door (sh1f, tr0f) with the slower PC 3 s later | thread continues on both; no `Waiting for partner`; no `room_gate:` hold line | hold line |
| X5: authority leaves a room mid-fight (LEAVE_BEHIND) with zombies, dogs, Hunter-like 39 | other machine `authority -> here ... resumed (n at step 0, adopted m)` with m equal to n after the leaving machine's final round; no snap or freeze | teleport / restart pose |
| hit from non-authority | its log `hit for slot N sent to slot 0`; authority `hit slot N applied`; HP drops once on both | twice |
| script spawn not yet run | `created on demand` once for its slot | |

### T10 Push block O1 (phase 13)

Room 0x2d: Rebecca (either machine) pushes the box to its spot.

| result | line |
|---|---|
| pass | pusher: `prop: scene 0x2d unit N lease granted slot S` (host) or `requested` then `granted` (guest), then `prop: unit N committed (...)`; other machine: box moves about 100 ms behind, F8 `prop moves ... applied` rises, `prop: unit N committed` once; `event_sync: fired scene 0x2d trigger ... (type 0x.., obstacle)` on the pusher only, `... is the box pusher's to fire` on the other; flag 0x8d on both (`flag_seq` lines); same layout after leaving and re-entering |
| fail | box rests elsewhere, trigger fires on both, layouts differ |

### T11 Boss hand-over (phase 15)

Boss room: Proto-Tyrant 31 or Queen Leech 46, both in the room.

| step | pass | fail |
|---|---|---|
| (1) authority machine's player disconnects (close its game) | other log: `room_authority: scene 0x.. owner slot N epoch E (peer left)`, `enemy_observer: authority -> here ...`, `boss: token T commit alone (members M, alive MASK)`; boss continues from the same HP and pose, then `boss: token T outcome` as members die | stands still, HP resets |
| (2) authority starts a door in LEAVE_BEHIND (if the room lets you out) | authority `boss: token T prepare (members M, alive MASK)` then `commit`; other `ready` then `commit`; boss frozen on both until the door finishes; then other `boss: token T applied (...)`, leaver `handed over`, later `outcome` | boss moves during hand-over |
| (3) leave the room on the other machine mid-prepare | `cancelled (left)` on the authority, boss moves again at once | stays frozen |

### T12 Items I1/I4 (phase 12)

| step | pass | fail |
|---|---|---|
| I1: both press action on one herb within 100 ms, 10 times | host one `item_ledger: take ... committed` and one `refused (claimed)`; loser `pickup_guard: take refused (claimed)`; one herb total; guest pickup in a room the host is not in: `committed to slot 1` | two herbs |
| I4: single pickups on a slow link | `pickup_guard: take held for the host`, then the item, bend-down motion, no second prompt | double prompt |

### T13 Death, scripts, save (phase 11/16)

| step | pass | fail |
|---|---|---|
| D1: guest's Billy dies | host `player_damage: remote character N died`, `player_damage: game over requested for the dead partner`; both `phase: Main -> Dead`; Continue loads both (guest needs `auto_join=1` or confirms by hand) | survivor never reaches Dead |
| S: host saves while the guest is in a door | `session_slot: save waits for plain gameplay (partner is in a door)`, save goes through after the guest arrives | save lost |
| S1: guest climbs the pl16 ladder; tr0a lift | host `event_sync: op PlayerClimb_Pos skipped as Body (... is a puppet)`; puppet climbs from the stream, same end spot | puppet stuck |
| S3: cutscene in tr0a skipped by the follower once and the leader once | both end within one delay, idle on both after; one `demo_skip: request|skip N sent` and one `skip N applied` | one side stuck in the demo |
| W5: guest fires a grenade round into a zombie in the host-authority room | guest `weapon_fx: projectile hit on slot N by Billy`; host applies once; HP drops once on both | twice / none |
| W4: guest shoots near an unaware zombie | zombie turns on the host's screen too | |

### T14 Transport (phase 17)

Wi-Fi state mode, 10 min.

| result | line |
|---|---|
| pass | F8 `delay` near 100 ms; `timeline reset` count per minute no higher than T2c; alt-tab the game 1 s: `Fresh channel: N older frames replaced` in that machine's launcher log; a stall gives one `SLOW_FRAME` line in `adapter.log` naming the column |
| fail | `delay` climbs, repeated resets |
