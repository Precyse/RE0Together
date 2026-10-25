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
| c. open | confirm on Co-op (pad confirm, Enter, left click on the row) | the decide sound, the title fades out and the Options screen's frame and key guide fade in with the Co-op page (T17); the log has `native_page: opening menu coop on the PC Setup page`; the Options list does not show first; the Steam overlay store does not open; no game starts | the Steam store opens, the Options list shows, a load list, a difficulty menu or a new game starts, nothing happens |
| d. back | on the page press cancel (pad B, Esc, right mouse button, the key guide's Back) | the page fades out, `native_page: menu coop closed`, the main menu comes back with the cursor on Co-op and takes input; the closing press does not reopen the page or choose another row | stuck menu, cursor on another row, the page reopens at once, the title goes back to Press Start |
| e. Options after | open Options, then PC Setup, change nothing and leave; open Options again | the Options list and PC Setup are vanilla (labels Resolution ... Texture Quality, their values, the gold header, arrows on every row); leaving applies nothing (no display reset) | a Co-op label or value on PC Setup, a missing row, the display resets |
| f. no input bleed | on the page press every key the page does not use; after returning press nothing for 2 s | no title row was chosen while the page was open and the menu cursor did not move | anything else opens, the cursor moved |
| g. repeat and leave | open and close the page 5 times; then Options, Extras and Load Game in turn and back; cancel to Press Start and return to the menu; start a new game, quit to the title | the entry and the page work every time; every other row works as vanilla; Co-op is there again after each return, with its label | row gone, "Online Store" back, crash |
| h. language | change the language in Options (each of English, French, German, Italian, Spanish, Japanese), return to the main menu | the label reads Co-op in the same lettering as the other rows (Japanese CO-OP; German in a slightly different system font); the page still works | label lost, garbled, letters on different baselines or uneven spacing |
| i. coop=0 / overlay=0 unchanged | set `coop=0`, start the game; then `coop=1` with `overlay=0` | vanilla four-row main menu, no `title_coop:` / `title_label:` / `native_page:` lines (with `overlay=0` the log has `adapter: overlay off, no Co-op entry on the title`) | any difference from vanilla |

### T16 Forced save load (one PC; coop=1, the launcher's host options in adapter.ini, no partner needed)

Setup: in `<game dir>\coop\adapter.ini` set `coop=1`, `load_slot=<N>` (a used slot) and `load_hash=<hex>` (the launcher writes both; the FNV-1a 32 of that slot's 0x1c850 bytes). Back up the save first. Delete `adapter.log`, start the game, take Load Game on the title and confirm a different used slot (the cursor row is not N). Send `adapter.log` and a screenshot of the world that loaded.

| step | do | pass | fail |
|---|---|---|---|
| a. config | open the log | `config: load_slot=N load_hash=xxxxxxxx`; no `config: load_slot=... ignored` | a key ignored although valid |
| b. redirect | Load Game, confirm slot M (M not N), Continue | `save_load: slot M requested, loading slot N (redirect)`, then `session_slot: playing slot N`; the game that starts is slot N's (room, play time and count of the launcher's row) | `save_load: slot N hash differs, not redirected` with an unchanged save (the block read at 0x613390 + 0x23a7c8 + N * 0x1c850 is not the plaintext slot, or the hash was made from other bytes), or slot M loads |
| c. hash guard | change `load_hash` by one digit, restart, repeat b | `save_load: slot N hash differs, not redirected`, slot M loads as the player chose | the redirect happens |
| d. other keys | `load_slot=20`, then `load_slot=x`, then no `load_slot` | each: one `config: ... ignored` line (none when absent) and the game's own load list decides | a redirect |
| e. pass-through | boot with the order set, watch the log before the main menu | no `save_load:` line for the title's boot read of the system data (slot 0x15) | the system data redirected |
| f. game over | die, wait for the title, Load Game, confirm any slot | `save_load: ... loading slot N` again (or the co-op slot 19 after a save: `loading slot 19`), no hash line | an old slot after a co-op save |
| g. guest | a guest with the same keys joins the host already in game | the guest's confirm logs `session_slot: load of slot M turned into the host's slot K` once the host announced its slot; before that `save_load: ... (redirect)` | the guest loads its own slot |

### T17 Native Co-op page and the host's start (one PC; coop=1, the launcher hosting, no partner needed)

Setup: host a lobby in the launcher (no guest), pick a save on the hosting screen, start the game; back up the save first; delete `adapter.log` before the start. Send `adapter.log` and game-window screenshots of the page for every fail.

| step | do | pass | fail |
|---|---|---|---|
| a. hooks | the log after the main menu | `hooks: uOptionManager::v8 hooked`, `uGUIOptionPcSetup input hooked`, `uGUIOptionPcSetup refresh hooked`; `lobby: launcher state, 1 members`; no `native_page: ... not the expected code` | a hook missing, no lobby line |
| b. look | open the page (T15 c) | the frame, the key guide and "Co-op" in the game font where PC Setup has its help line; no gold header; 8 rows: your name (you) with Host / Ready, Open, Character, Outfit, World pause, Partner mode, Save, Start; the values in the value column, none clipped in a way that hides the slot number | the PC Setup labels or values, an empty row, overlapping text, the gold "PC Setup" header |
| c. cursor | press up and down through the page, wrap at both ends; click rows with the mouse | the cursor skips the two player rows and Partner mode; the move sound plays; the arrows show only on Character, Outfit, World pause and Save; a click on another row moves the cursor there, a click on the cursor row acts like confirm | the cursor lands on an info row, arrows on Start, no sound |
| d. options | on Character, Outfit and World pause press left and right (also click the arrows) | each change shows on the page within a moment and on the launcher's hosting screen, and the log has `lobby: option ... sent to the launcher` and the launcher log `Adapter option ...=... saved`; Outfit steps only through the outfits the launcher offers for the shown character (DLC ones only when installed); after a Character change the Outfit row follows the new character | the page and the launcher disagree, a refused line, a DLC outfit without its DLC |
| e. save | on Save press left and right through every value | None, then each used slot labelled like the launcher's picker (`Slot 3 / Normal / 2:14:05 / Area 4`); the launcher's picker follows; at start the row showed the launcher's pick; Start is refused (cancel sound) on None | an empty slot offered, the label differs from the launcher's row, Start acts on None |
| f. start | pick a slot, confirm on Start | the page fades out and the game loads that slot without the main menu showing again; the log has `title_load: host start of slot N`, `the title is parked for slot N (host start)`, `load of slot N requested (try 1)`, `slot N loaded (hash ...), start mode 1, wesker 0, switching to the game`, `lobby: start 1 of slot N ... announced`; the room, play time and outfits are that save's with the page's outfit | the main menu flashes, another slot loads, a black screen that stays, a crash |
| g. cleared and Wesker | repeat f with a cleared save and with a Wesker save if the save file has them | `start mode 2` for the cleared save; `wesker 1` and Wesker mode for the Wesker save, as the game's own Continue starts them | start mode 1 for a cleared save |
| h. game over | die, wait for the title, open the page, Start again | the Save row shows the slot the session plays (the co-op slot after a save in the session) and Start loads it | an older slot |
| i. Continue still redirects | instead of the page take Load Game, confirm another slot | `save_load: slot M requested, loading slot N (redirect)` as in T16 b, N being the page's pick | the confirmed slot loads |

### T18 Lobby page with a partner (two PCs; the launcher session as in TWO-PC setup)

| step | do | pass | fail |
|---|---|---|---|
| a. both pages | both open the page | each shows the host's and the guest's names, (you) on its own row, Host / Ready and Guest / Ready once both pages are open; a player on its main menu reads In menu, a closed game Away within 6 s | wrong names, roles swapped, a state stuck |
| b. read only | on the guest's page try Character, World pause and Save | they show the host's values and refuse a change (cancel sound); the guest's Outfit changes and shows on the host's launcher, the host's page does not show it (only the launcher's player cards do) | a guest changes a host option |
| c. host changes | the host changes Character, World pause and Save | the guest's page follows within 2 s (character swaps to the other one, world pause label, the save label) | the guest's page keeps old values |
| d. save differs | before the session, give the guest an older copy of the host's save (or edit nothing and skip) | the host's page reads Guest / Save differs while the guest's copy of the picked slot is another | Ready with a different copy |
| e. start | the guest waits on its page; the host Starts | the guest's Start row reads Waiting for host, then its page closes and its game loads the same slot (`title_load: following the host's start 1 of slot N`, `slot N loaded (hash H)` with the host's hash); the join places the guest in the host's world | the guest stays, loads another slot, or starts with a different hash |
| f. guest at the main menu | the guest leaves the page and waits on the main menu; the host Starts | the guest's title fades out and loads the slot the same way; no auto_join presses (`auto_join:` lines show no taps on the title) | the load list opens, Enter presses on the title |
| g. staged save | in game the host saves (typewriter); both die; on the title the host Starts the co-op slot | the guest's log shows `save_redirect: data0.bin read` from the staged copy (the guest loads the save made in the session, hash equal) | `is not the host's ... waiting for a newer copy` until it gives up |

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
| pass | F8 `delay` near 50 ms on a clean link (adaptive 50..250); `timeline reset` count per minute no higher than T2c; alt-tab the game 1 s: `Fresh channel: N older frames replaced` in that machine's launcher log; a stall gives one `SLOW_FRAME` line in `adapter.log` naming the column |
| fail | `delay` climbs, repeated resets |

### T20 Enemies 1:1 (2026-10-25 changes; both machines on the same build)

Scene 0x24 zombies, then the lift room 0x32 insects. The host enters a room the guest loaded first.

| result | line |
|---|---|
| pass | host log `room_authority: scene 0x.. owner slot <host> epoch N (claimed)` on arrival (no boss alive); no `entry refused`; enemies visible on both, both floors of 0x32 |
| pass | shots on an enemy the other machine runs: blood and the hit sound on both; F8 `hit fx sent/shown` counts on the two machines |
| pass | a zombie headshot kill: the head bursts on both (`hit_fx: ... lost part 2, sent` / `(from its simulator)`); the head stays gone (PART_MASK) |
| pass | flinches on the observer while the enemy lives; a falling death plays once (few `enemy_observer: slot N layer 0 sought` lines) |
| pass | groans and grab cries heard on the machine that does not run the room |
| pass | a grab plays on both; killing the grabber frees the victim (`lease: token N ended (enemy dead)` after the release) |
| fail | any enemy invisible, frozen, double-hit, a stuck grab, a part shown wrong on the observer (PART_MASK word count) |
