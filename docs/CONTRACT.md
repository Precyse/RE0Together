# Co-op framework contract

Two halves per player: the **launcher** (owns Steam, game-agnostic) and the **adapter** (a DLL inside the game, game-specific). They talk only over loopback. The game never links Steamworks.

```
[game + adapter DLL] <-TCP 127.0.0.1-> [launcher] <-Steam P2P / local UDP-> [launcher] <-TCP-> [game + adapter DLL]
```

## Loopback link

- The launcher listens on `127.0.0.1:<port>` (from the game profile). One adapter connection at a time. A new connection replaces the old one.
- Both directions use the same frame, all integers little-endian:

| field | type | meaning |
|---|---|---|
| len | u32 | bytes after this field (header remainder + payload), max 1 MiB |
| type | u16 | message type |
| flags | u8 | bit0 = reliable, other bits 0 |
| slot | u8 | adapter->launcher: destination slot, 0xFF = all peers. launcher->adapter: source slot |

- The payload follows immediately. The launcher never parses types >= 0x0100. It relays them as opaque bytes.

## Peer wire (launcher <-> launcher)

- Same bytes as the loopback frame minus `len`: `type u16 | flags u8 | slot u8 | payload`. On send, the launcher writes its own slot. On receive, it **overwrites** `slot` with the slot mapped from the sender's transport identity, never trusting the byte.
- Steam: `ISteamNetworkingMessages`, channel 0. Reliable uses `k_nSteamNetworkingSend_Reliable | NoNagle`, unreliable uses `Unreliable | NoNagle`, and both OR in `AutoRestartBrokenSession`.

## Control messages (0x0000-0x00FF, launcher-owned)

| type | name | dir | payload |
|---|---|---|---|
| 0x0001 | HELLO | adapter->launcher | u16 proto, u8 len + ascii game id |
| 0x0002 | WELCOME | launcher->adapter | u8 local slot, u8 host slot, u8 max players, u32 epoch, u64 local SteamID (15 bytes; an adapter that reads 7 bytes ignores it; the local test transport reports its UDP port) |
| 0x0003 | PEER_UP | launcher->adapter | u8 slot, u64 steam id, u8 len + utf8 name |
| 0x0004 | PEER_DOWN | launcher->adapter | u8 slot |
| 0x0005 | REJECT | launcher->adapter | u8 len + ascii reason (e.g. game or proto mismatch), then close |
| 0x0010 | PING | launcher<->launcher | u64 sender time µs |
| 0x0011 | PONG | launcher<->launcher | u64 echoed time µs |
| 0x0012 | PEER_STATS | launcher->adapter | u8 slot, u16 rtt ms |
| 0x0020 | HEARTBEAT | adapter<->launcher | empty, every 1 s both ways; 5 s silence = link down |

| 0x0040 | FILE_BEGIN | launcher->launcher | u32 transfer id, u32 total size, u8[32] sha256, u8 len + ascii file name |
| 0x0041 | FILE_CHUNK | launcher->launcher | u32 transfer id, u32 offset, bytes (≤ 256 KiB) |
| 0x0042 | FILE_END | launcher->launcher | u32 transfer id |
| 0x0043 | FILE_ACK | launcher->launcher | u32 transfer id, u8 ok (sha256 matched) |
| 0x0040..0x0043 | crash files | guest launcher->host launcher | the same FILE_BEGIN / CHUNK / END (the host answers FILE_ACK) carry a guest's crash files after a crash, exit or hang of its game: file name `<yyyyMMdd-HHmmss>/<flat name>` (the stamp groups one incident, the flat name is the path in the game folder with `\` and `/` replaced by `_`), at most 64 MiB; the host writes `<game>\coop\peer_crash\<steamid>_<stamp>\<flat name>` (3 stamps per guest and session). The set is the profile's `crashFiles` that appeared after the session start and stopped growing, plus the whole adapter log once the game is gone; no consent is asked |
| 0x0044 | FILE_MANIFEST | host launcher->guest launcher | u8 count, then per file u8 length + ascii name; sent before the files when the profile sends by `filePattern`; the guest then expects exactly these files |
| 0x0013 | BUILD_INFO | host launcher->guest launcher | i32 launcher build number (the launcher component's version in installed.json; 0 = dev build), then i32 installed game build (Steam buildid; 0 = unknown; absent from older launchers), then u8[32] mod hash (SHA-256 over the profile's adapterFiles as installed: per file the lowercase dst name, 0x00, u64 LE length, bytes; zero = not installed; absent from older launchers); sent as a peer joins, before the save files; a guest on a different launcher build or game build refuses to start the game, and so does one with another hash (Mod files differ) or none (Mod not installed) when the host's is not zero; the first difference found counts, in the order launcher build, game build, mod; a dev build (build 0) is guarded by the hash alone |
| 0x0014 | LEAVING | launcher->launcher | empty; sent to every peer when a launcher leaves the session on purpose (Leave, window close). A guest's host drops the guest at once (no reconnect grace); a guest whose host sent it ends the session (Host left). Not relayed to the adapter; an older launcher ignores it |
| 0x0015 | KICK | host launcher->guest launcher | empty; the host removes a guest: the guest ends its session (Removed from the lobby). The host drops the guest at once, hides it from the lobby and sends KICK again if it is still a lobby member later in that session. Not relayed to the adapter; an older launcher ignores it |
| 0x0060 | SAVE_CHANGED | adapter->own launcher | ascii name of a save file the game just wrote to the Steam cloud; the host launcher re-sends the profile's save files to every peer (same FILE_* transfer as at join) |
| 0x0061 | OPTION_SET | adapter->own launcher | ascii `key=value` (key 1..32 chars of [a-z0-9_], value 0..32 printable chars without `;` or `=`): an option the player changed on the game's own Co-op page (an outfit; as host the character, world pause or the save slot `load_slot`). The launcher saves it like a choice made in its window (the profile has the option, it is not a derived one like `load_hash`, a host option only from the host, the value is offered in this game folder; not blocked by the selection lock), so the lobby and every screen get it the usual way; a refused one is logged |
| 0x0062 | LOBBY_STATE | launcher->own adapter | UTF-8 text, lines by LF, fields by TAB (tabs and line breaks in a field become spaces), at most 8192 bytes of whole lines; sent when the link comes up and whenever the text changes. `member` slot (255 = none yet), host 0/1, local 0/1, character value, character label, outfit label, name (64 chars at most), host first; `option` key, value, label for each host option the page shows (not runtime, not `max_players`); `value` key, value, label for each value those options offer; `outfit` character value, outfit value, label for the outfits offered in this game folder in profile order; `own` key, value for this player's own options. An adapter skips lines it does not know |
| 0x0050 | LOG_APPEND | guest launcher->host launcher | raw bytes newly appended to the guest's adapter log (≤ 32 KiB, every 2 s); the host appends them to the session's `peer_<steamid>_<stamp>.log` beside its own adapter log (the newest few per peer are kept) |

- `epoch` increments every time the session membership resets (new lobby, host change). Adapters drop all game state on a new epoch.
- On adapter connect, the launcher answers HELLO with WELCOME followed by one PEER_UP per current peer.
- PING runs every 1 s per peer. RTT is smoothed (EWMA, alpha 0.125) and reported through PEER_STATS every 2 s.

## Session

- Slots: lobby owner = 0 = host. The others are sorted ascending by SteamID into 1..max-1. Recompute on every membership change. A member missing from the lobby is held 10 s before PEER_DOWN; its slot and frames continue. Back within 10 s: PEER_UP again for the same slot (adapters resync: a guest asks for a snapshot, a host re-sends what it sends a joining peer). The host missing for 10 s, or any change of lobby owner, ends the session (epoch bump, PEER_DOWN for all). A guest whose lobby fails keeps its session and adapter link during the 10 s while it rejoins.
- Lobby data: `cf_game=<id>`, `cf_proto=<n>`, `cf_ver=<launcher version>`. Members whose `cf_game`/`cf_proto` mismatch get no slot.
- Protocol version `proto = 1`.

### Peer frame budget

Game frames (type 0x0100 and up) from a peer pass a token bucket per source slot before the launcher relays them to the adapter. A frame must fit both buckets of its kind, or it is dropped and counted per (slot, type, reliable); the peer is never disconnected. Launcher-owned frames (FILE_*, BUILD_INFO, LOG_APPEND, CRASH_DUMP, PING/PONG) are not budgeted. Every 5 s, while the totals change, the log gets `Peer budget: slot 1 dropped 0x0101 x12 (unreliable)`; a reliable drop adds "state may diverge; F9 resyncs".

| bucket | rate | capacity |
|---|---|---|
| unreliable frames | 300 /s | 600 (2 s) |
| unreliable bytes | 256 KiB/s | 512 KiB |
| reliable frames | 300 /s | 3000 (10 s, a stall's backlog) |
| reliable bytes | 512 KiB/s | 4 MiB (above the 1 MiB frame cap) |

That is about 2x the RE0 peak (140 frames/s, up to about 80 KiB/s; DS2 about 125 frames/s, under 20 KiB/s).

## Game profile (`launcher/games/<id>.json`)

```json
{ "id": "re0", "name": "Resident Evil 0 HD", "steamAppId": 339340, "exe": "re0hd.exe",
  "maxPlayers": 2, "port": 27960,
  "adapterFiles": [ { "src": "adapters/re0/dinput8.dll", "dst": "dinput8.dll" } ],
  "adapterIni": "coop/adapter.ini" }
```

A `rendezvous` game's join file (`joinFile` in the profile, SR2: `coop\sr2_join.txt` in the game folder) is written atomically by the launcher and read by the mod: UTF-8 lines `role=host|guest` (the lobby owner hosts), `token=<digits>` (the Steam lobby id), `password=<digits>` (the lobby's `JoinSecret`) and the optional `name=<UTF-8>`. `name` is the player's Steam name in the game: printable ASCII and U+00A1..U+00FF only (any other code point is sent as `?`, control characters are dropped), trimmed, at most 15 characters and at most 64 bytes; when another lobby member shapes to the same name (case-insensitive) the members in ascending Steam id order after the first get ` 2`, ` 3` ... with the base cut to keep 15 characters. No `name` line (an empty result) means the game keeps its own name. The mod ignores a name outside that set, and drops one over 64 bytes without dropping the request.

`sessionKind` is `lobby` (default: the launcher's Steam lobby, transport and save sync) or `native` (the game matches players itself, as Saints Row 2 does through OpenSpy: the launcher installs the mod, writes the options into `adapterIni` and starts the game; no lobby, seats, save sync or adapter link). `rendezvous` is `native` plus a Steam lobby that says who hosts (`joinFile`, `statusFile`); with `"bridgeWhen": { "key", "value" }` the launcher also runs the bridge, build check and log forwarding when the host's value of option `key` equals `value`. A game option with `"host": true` is the host's alone: guests write the host's value to the adapter ini. `requires` lists `{ "file", "name", "install"? }` entries the game folder must hold; a missing one is shown in the rail line as `<name> not found`, and Install first shows the missing ones. `install` is `{ "url" (https zip), "sha256" (of the zip), "files": [{ "from" (zip entry), "to" (game-relative) }] }`: the launcher downloads it, refuses a different hash and writes only the listed files (an existing file is kept); without `install` the player installs it.

## Save sync (launcher-owned, game-agnostic)

- Profile key `saveSync`: `{ "steamRemoteFiles": ["data0.bin"], "sessionDir": "coop/session", }`. Files come from `Steam\userdata\<accountId>\<steamAppId>\remote\` on the host.
- Host: on each new peer, send every file as FILE_BEGIN, FILE_CHUNKs and FILE_END, all reliable. The host never has a session dir (it plays its own save).
- Guest: write chunks to a temp file, verify the sha256, move it into `<game>\<sessionDir>\`, and reply FILE_ACK. Launch the game only after every file has been ACKed ok (60 s timeout, then abort with a message).
- Both sides set `coop=1` and `role=host` or `role=guest` in the profile's `adapterIni` (in the game folder) before launching. On launcher exit, and on launcher start to recover from a crash, delete the session dir and set `coop=0`, so a normal launch plays vanilla.
- The adapter redirects the game's save I/O to the session dir whenever that dir holds the files and `coop=1`.
- Games that keep their saves outside the Steam cloud folder add `hostSaveDir` (host source), `guestSaveDir` (guest destination, relative to the game folder) and `filePattern` (e.g. `*.dat`). The folders are templates: `{documents}` is the user's Documents folder, `{steamid64}` the signed-in Steam user (from Steam's registry key). The host sends every matching file after a FILE_MANIFEST. Example: `launcher/games/ds2.json`. Before the launcher deletes a session folder (exit, crash recovery) it copies the guest's files in it to `<sessionDir's parent>\archive\<yyyyMMdd-HHmmss>\` (staging and `.part` files excluded; the last 5 archives are kept), so the guest's own progress is never lost. With `filePattern` the host launcher also watches its save folder (the game writes saves after the join and sends no SAVE_CHANGED) and, 3 s after a matching file stops changing, sends it to every peer as FILE_MANIFEST (current list) plus FILE_BEGIN/CHUNK/END. The guest takes the first full set straight into the session folder; every later file goes to `<sessionDir>/staging` and is moved into the session folder only while the game exe is not running, so a running game's own saves are never replaced.
- Profile key `saveSync.resign`: `{ "kind": "mandarin", "seed": "6829223510001954331", "variant": 1 }`, for games that encrypt each save for the Steam account that wrote it (Dragon's Dogma 2: RE Engine "DSSS" files). `kind` is the scheme (only `mandarin`), `seed` the game's 64-bit seed of it (decimal or `0x` hex, a string), `variant` how the scheme's user id derives from a SteamID64 (0: the SteamID64, 1: the account id's complement under all ones in the high half, 2: the SteamID64's complement, 3: obfuscated complement; DD2 uses 1). The profile load refuses an unknown kind, a seed that is not a 64-bit number and an unknown variant. A guest rewrites every received file (after the sha256 check, before it is moved into the session or staging folder) from the host's SteamID64 (the lobby host's id, `session.HostId`) to its own (SteamID64 = 76561197960265728 + the signed-in Steam account id); the file keeps its flavor (header word 2: `0x10` as is, `0x18` deflate-compressed, both occur in one DD2 save folder). A file that is not an intact save of the host's account is refused like a failed hash (FILE_ACK not ok, the host resends up to its attempt limit, the guest's stall timeout applies); a guest with no signed-in Steam user is refused at session start. The host sends its files unchanged. The ported code and its licence are in `launcher/SaveResign/`.

## Save backups (launcher-owned, for games where each player keeps their own save)

- Profile key `conflicts`: `[{ "name": "DD2gether", "into": "coop/archive", "paths": [ ...the mod's scripts and data folder, relative to the game folder... ] }]`. When any path is in the game folder and the game is closed, every present path is moved (never deleted) into `<into>/<name slug>_<yyyyMMdd_HHmmss>/` under the same relative path: at launcher start, before a mod install and in the start checks of a session. The player event says where.
- Profile key `campaigns`: `{ "dir": "coop/campaigns", "saveFolder": "win64_save", "characterFile": "profile.dd2p", "importDd2gether": true }`. Each campaign is `<dir>/<id>/` (id: 1 to 48 of `[a-z0-9-]`, plus `-N` when taken) with `campaign.json` `{name, createdUtc, source}`, the world in `saveFolder` and the adapter's capture of its character in `characterFile`. Option `source: "campaigns"` (key `campaign`, value a campaign id or empty for the game's own save; the values New campaign `new` and New from own save `copy` make one and choose it) and `source: "characters"` (key `arisen`, a campaign id whose character file exists, or empty) are shown in the options grids, and on the hosting screen until this machine's game starts (the host picks the campaign, each guest its character; a character not captured yet reads `<name> (save once)`, a captured one `<name> / Lv N`). A host that changes campaign while hosting sends the new world to every guest again. A guest whose picked character (a campaign's, or its own save's: `campaigns.ownCharacter`) was never captured is refused before its game starts (`CharacterNotSaved`, "<name> not saved yet: host it once and save", with Join again). A host with a campaign sends that campaign's world in save sync; the DD2 adapter redirects its saves there (`campaign`) or, as a guest, splices the picked campaign's character (`arisen`). With `importDd2gether`, DD2gether's campaigns (newest checkpoint) and character profiles (their save) from its live or archived data are made into campaigns once each, at launcher start and from the settings view's Import DD2gether (command `game.importDd2gether`, shown while that data exists).
- Profile key `saveBackup`: `{ "dir": "{localappdata}/Pearl Abyss/CD/save/{steamaccountid}", "into": "bin64/coop/save_backups", "keep": 10 }`. `dir` is a folder template (`{documents}`, `{localappdata}`, `{steamid64}`, `{steamaccountid}` = SteamID64 - 76561197960265728, `{steamuserdata}` = Steam's install folder + `userdata`), `into` is relative to the game folder. Dragon's Dogma 2 uses `{steamuserdata}/{steamaccountid}/2054970/remote/win64_save` into `coop/archive/real`.
- Every player (host and guest) copies the whole folder into `<into>\<yyyyMMdd-HHmmss>-before\` once per session just before starting the game, and into `...-after\` when the session is dropped. Each copy has `manifest.tsv` (relative path, size, SHA-256). The newest `keep` sessions (2 folders each) are kept.
- A restore writes a copy back only while the game is not running and only when every file still matches its manifest (`SaveBackup.Restore`; no window button yet).

## Adapter responsibilities (every game)

1. Connect to the launcher, send HELLO, and wait for WELCOME. Reconnect every 2 s if the link drops. Never block the game thread on the socket.
2. Run the network on a fixed 30 Hz tick driven from a game-thread hook, never the render thread.
3. Keep an engine-neutral gameplay core and implement the per-engine primitives:
   `enumerate_actors`, `local_player`, `bind_remote(slot) -> body`, `suppress_ai(body)`, `warp(body, pos, rot)`, `play_action(body, id)`, `set_locomotion(body, speed, dir)`, `read_anim(body)`, `force_anim(body, anim)`, `apply_hit(descriptor)`.
4. Authority: the host (slot 0) owns enemies, world flags and pickups. Guests send their own player state plus hit descriptors.
5. A PEER_UP for a slot that is already up means the peer was unreachable for a while: resync, do not reset.

## Game messages of the SR2 adapter (0x0100 and up, adapter-owned)

Used only when the host's `transport` option is `steam`: the game's UDP datagrams travel through the launchers (`bridgeWhen` in `games/sr2.json`, port 27962). The launcher relays them as opaque bytes and overwrites the source slot, as for every game message. Integers little-endian.

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0100 | GAME_DATAGRAM | adapter to adapter | no | u32 dst (IPv4 in host order; 0xFFFFFFFF = broadcast), then the game's UDP payload (1..1300 bytes). The sender's frame slot is the peer's current slot, or 0xFF for a broadcast; the receiver reads the source slot the launcher wrote. |

## Game messages of the RE0 adapter (0x0100 and up, adapter-owned)

The launcher relays these as opaque bytes. The state-mode messages (0x0140..0x017f) and the older ones are listed with their wire headers in `docs/codemap/re0.md`; the lobby messages run in every partner mode:

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0117 | SESSION_LOAD | host to all | yes | i32 slot, u32 FNV-1a 32 of the slot's block, u32 serial (never 0): the host loaded that slot from its title; a guest at its title or Co-op page loads it when its copy hashes the same (`lobby_wire.h`) |
| 0x0118 | LOBBY_PRESENCE | each game to all | yes | u8 place (title, page, loading, in game), u8 flags (1 host pick, 2 guest checked, 4 guest matches), u8 pick slot, u8 reserved, u32 pick hash (`lobby_wire.h`) |

## Game messages of the DS2 adapter (0x0100 and up, adapter-owned)

The launcher relays these as opaque bytes; the source slot is the transport's. Integers little-endian, structs packed as written (`adapters/ds2/src`). Ids in `adapters/ds2` are not shared with RE0's (`adapters/re0/src/protocol.h` reuses the range for its own messages).

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0100 | PLAYER_STATE | all | no | u32 seq, f32 pos[3], f32 yaw, u32 status (DS2: bits 0-7 health 0-254 or 255 unknown, 0x100 dead, 0x200 down, 0x400 loading, 0x800 driving, 0x1000 sender reports, 0x2000 menu; 0 = not reported, `partner_status.h`) |
| 0x0101-0x0107 | cargo list/take/add, pickups, drops | | see header | `cargo_transfer.h`, `cargo_ground.h` |
| 0x011A | CARGO_GONE | to the partner | yes | u32 kind, u32 reserved, u64 order id (0 = plain): the partner's rack lost the piece to a terminal or to the sender, delete your copy (`partner_cargo_wire.h`, 16 bytes) |
| 0x0130 | MOVE_ACK | to the sender | yes | u32 kind, u32 moved (0 = CARGO_GONE done, 1 = a give created), u64 order id: the receiver's own CARGO_LIST shows the move, the sender may stop holding it (`partner_cargo_wire.h`, 16 bytes) |
| 0x0131 | CARGO_ASK | guest to host | yes | u64 handle, u8 wants it (1 = the host gives its piece, 0 = the guest offers its own), 7 reserved: the host answers with the same give or take it would make itself (`cargo_transfer.h`, 16 bytes) |
| 0x0133 | CARGO_GIVE | guest to host | yes | the CARGO_ADD layout (`cargo_transfer.h`, 32 bytes): a piece the game moved into the host's rack; the host creates it and answers MOVE_ACK, or sends it back as CARGO_ADD and MOVE_ACK |
| 0x0108 | VEHICLE_STATE | driver/passenger to all | no | `vehicle_sync.h` (64 bytes) |
| 0x0109 | VEHICLE_LOAD | driver to all | yes | `vehicle_load.h` |
| 0x010A | ANIM_STATE | all | no | AnimHeader, optional u64 sender time, entries (below) |
| 0x010B | FACT_SET | host to all | yes | u32 count, u32 reserved, count x 24-byte facts (`fact_wire.h`) |
| 0x010C | EQUIP_STATE | all | yes | `equip_sync.h` |
| 0x0110 (guest to host) | STRUCT_REMOVE | guest | host | a collapse the guest's player did; the host removes it and announces it as STRUCT_REMOVE to everyone |
| 0x012A | STRUCT_REQUEST | guest | host | a guest's placement: STRUCT_CREATE payload with id 0xFFFFFFFF; the host builds it and announces it as STRUCT_CREATE |
| 0x010D-0x0110 | WORLD_ENV, ORDER_START, STRUCT_CREATE, STRUCT_REMOVE | | | reserved, not built |
| 0x0111 | AUTH_CLAIM | owner or host to all | yes | AuthMessage |
| 0x0112 | AUTH_DECLINE | declining owner to all | yes | AuthMessage |
| 0x0113 | AUTH_STOP | owner or host to all | yes | AuthMessage |
| 0x0114 | CLOCK_PING | to one slot | no | u64 sender time (µs) |
| 0x0115 | CLOCK_PONG | to the pinger | no | u64 echoed ping time, u64 answerer time (µs) |
| 0x0116 | ANIM_EVENT | all | yes | same payload as ANIM_STATE, entries are pulses |
| 0x0117 | FACT_SNAPSHOT | host to one slot (or all) | yes | u32 snapshot id, u16 chunk index, u16 chunk count, then a FACT_SET payload |
| 0x0118 | RESYNC | to one slot or all | yes | u32 scopes (1 facts, 2 authority, 4 anim, 8 story, 16 enemies, 32 structures, 64 lockers) |
| 0x0128 | BT_ENV | host to all | yes | u64 BT-active region set, bit r = region r (`bt_wire.h`, 8 bytes) |
| 0x0129 | CATCHER_EVENT | host to all | yes | u8 kind (1 territory, 2 tar), u8 flags (bit 0 the territory's bool), u8[2] reserved, u8[16] locator UUID (20 bytes) |
| 0x0137 | CRATER_SET | host to all | yes | the host's voidout craters (`crater_wire.h`): u16 count (at most 64), u16 reserved, then count x 48 bytes {u8[16] crater locator UUID, f64[3] position, f32 radius, u16 area, u8 kind, u8 reserved}; sent whole on a change, every 5 s and to a joining peer |
| 0x0138-0x0139 | BT_GRAB, BT_GRAB_STATE | host to the grabbed guest, guest to host | | reserved, not built: the relay of a BT's grab to the guest whose body it acted on needs live probes first (docs/DS2_NOTES.md, "BT grab") |
| 0x0134 | LOCKER_LIST | host to all, or to a joining or resyncing peer | yes | one shared locker's contents (`locker_wire.h`): u32 key (the construction owner's identifier, a hash of the construction id), u8 slot kind (28 Public, 29 PublicReceived), u8 flags (bit 0 = snapshot end: key, kind and count 0, the lockers not named since the snapshot began are empty), u16 count (at most 128), u32 reply-to request id (0 = none), u32 reserved, then count x 32-byte CARGO_ADD pieces; sent on a change, as a snapshot of every non-empty locker plus the end marker, and after a LOCKER_MOVE |
| 0x0135 | LOCKER_MOVE | guest to host | yes | 56 bytes: u32 request id (from 1), u32 key, u8 slot kind, u8 direction (1 put, 2 take), u16 + u32 reserved, u64 guest handle (the guest's undo only), one CARGO_ADD piece; the host refuses more than 20 per second per guest |
| 0x0136 | LOCKER_RESULT | host to the requesting guest | yes | u32 request id, u8 accepted, u8 reason (0 ok, 1 unknown locker, 2 full, 3 not there, 4 order piece held, 5 malformed, 6 rate), u16 reserved; a refusal makes the guest undo its move |
| 0x013A | PING_SET | each player to all | yes | the sender's placed user markers (`ping_wire.h`): u16 count (at most 100), u16 reserved, then count x 24 bytes {u64 marker id, f32[3] position (world metres, Z up), u8 group 0..4, u8 index 0..19, u8 sequence (1 or more), u8 reserved}; the whole set on a change and to a joining peer, an empty set clears the sender's pings; presentation only, drawn by the partner's overlay |
| 0x0119 | STORY_EVENT | host to all (a guest sends an order request to the host) | yes | `story_wire.h` (104 bytes): kind (mission start/success/fail, section active/inactive, area change, order request), mission id, args, section or start-section UUID, area transform |
| 0x012A | CUTSCENE_START | host to all | yes | `cutscene_wire.h` (60 bytes): u32 id, u8 category, u8[3] reserved, i32 stop frame, u8[16] SequenceResource UUID, u8[16] host Sequence entity UUID, u8[16] SequenceNetwork UUID (zero if unknown) |
| 0x012B | CUTSCENE_READY | guest to host | yes | u32 id (the guest holds its copy) |
| 0x012C | CUTSCENE_GO | host to all | yes | u32 id (both copies start) |
| 0x012D | CUTSCENE_END | host to all | yes | u32 id, i32 host frame at the stop, u8 stop reason (0 finished, 1 destroyed, 2 aborted, 3 scripted), u8[3] reserved |
| 0x0120 | ENEMY_HIT | guest to host | yes | `EnemyRef {u16 net id, u16 reserved, u8[16] entity UUID}` + `HitFields {f32 amount, u32 flags, i32 part index, u32 reserved, f32[4] origin, f32[4] impulse, f32[4] normal}` (84 bytes, `combat_wire.h`) |
| 0x0121 | PLAYER_HIT | host to the guest whose body was hit | yes | the same 84 bytes; the `EnemyRef` is the attacking enemy (zero = none) |
| 0x0124 | WEAPON_STATE | all | yes | `weapon_wire.h`: u16 weapon id (0 = holstered), u8 hand (EDSWeaponAttachPoint 0 default, 1 right, 2 left), u8 reserved; on change and to a joining peer |
| 0x0125 | WEAPON_FIRE | all | yes | `weapon_wire.h` (32 bytes): u16 weapon id, u8 behavior kind (1 Gun, 2 ShotGun, 3 BolaGun, 4 StickyGun, 5 GrenadeLauncher, 6 HandGrenade, 7 SingleShotBeam), u8 reserved, u32 pellets, f32 origin[3], f32 direction[3]; one per shot or throw, cosmetic only |

- **Time.** A timestamp is the sender's monotonic clock in microseconds (`TimeUs`). A receiver converts it with the per-peer offset from CLOCK_PING / CLOCK_PONG: the answerer stamps its time when it replies, the pinger takes the stamp as the middle of the round trip (`offset = peer - (t0 + t3) / 2`), and the estimate is the median of the last 16 samples (valid from 3). Pings go out every 250 ms until 16 samples are held, then every 2 s. A stream rendered from timestamps shows the report nearest `now - delay` with a delay of 150-300 ms that rises at once with measured lateness and falls 10 ms every 3 s.
- **ANIM_STATE / ANIM_EVENT payload.** `AnimHeader {u32 seq, u16 count, u16 flags}` (flag 1 = snapshot, flag 2 = timestamped), then, only with flag 2, `u64 sentUs`, then `count` entries `{u16 index, u8 type, value}` (value 1, 4 or 16 bytes for type bool 0, int 1, float 2, quat 3). Backward compatible: a payload without flag 2 is the old layout and decodes with no timestamp, so `proto` stays 1; the build check already refuses mismatched adapters, and every sender sets flag 2. ANIM_EVENT carries pulses (a boolean that left its resting value and came back within 500 ms) with the pulse value; the receiver writes it into the remote's animation for 100 ms on top of the partner's state.
- **Authority.** `AuthMessage {u64 id, u32 epoch, u8 owner, u8[3] reserved}` (16 bytes). Every machine keeps the same `{id, owner, epoch}` table (epoch starts at 1, every change adds one) and applies the same rules to the same messages. CLAIM: `owner` owns `id` at `epoch`; the sender must be `owner` or the host; a newer epoch replaces the record, the same epoch and owner is a duplicate, two owners at one epoch resolve to the lower slot, an older epoch is dropped. STOP: `owner` stops acting; valid from `owner` (a release, which also clears the barred list) or from the host; must carry the record's current epoch; the record becomes unowned at epoch + 1. DECLINE: sent by `owner` when it cannot host the object; the record becomes unowned at epoch + 1 and the slot is barred from owning it until a voluntary release; the host picks the next owner that is not barred. A slot that leaves loses its objects (unowned, epoch + 1). A joining peer, or a RESYNC for scope 2, makes each owner repeat its CLAIMs.
- **Cutscenes (0x012A-0x012D).** Only the host decides. The engine's Sequence start is held on the host (and on a guest) by a detour; the host sends START when it holds a shared Sequence (a story category, or any category whose resource is in the Cutscene game state, as the private room entry of category None is; the menu radio is local), each guest holds its own copy of the same SequenceResource (its own story graph reaching it, else the guest starts the SequenceNetwork named in START, else it adopts the Sequence entity by UUID) and answers READY, and the host sends GO when every peer answered or after 5 s (a guest that cannot ready must not stall the host; the timeout is logged). The host starts its own copy half the slowest peer's round trip after sending GO. A guest holds any shared Sequence the host did not announce and never plays it. The host sends END with its frame when its Sequence stops: a guest still playing stops its copy when the host's frame was more than 24 frames before the stop frame (a skip); a guest still holding plays it. The guest cannot skip. START is sent after the host's pending FACT_SET in the same tick, so the facts a cutscene's graph branches on arrive first. Off unless adapter.ini `cutscene_sync=1` (both machines).
- **FACT_SNAPSHOT.** The host keeps the last value of every fact it changed during gameplay and sends all of them, in chunks of up to 256 facts, to a peer when it joins and whenever a peer asks (RESYNC scope 1). A guest asks once its own gameplay has settled (8 s with its state machine active). Chunks apply on arrival: they are reliable, in order with the FACT_SET deltas, and writing a fact twice is harmless.
- **RESYNC.** Asks the receiver to resend whole snapshots of the parts named in `scopes`. A machine also resyncs on demand by creating `<game>\coop\resync_now.txt` (the adapter deletes it): it asks every peer and resends its own state.
- **Reject counters.** A message the adapter drops (stale epoch, unknown object, wrong sender, barred owner, malformed payload, held too long, queue full) is counted per message type and reason and logged as `rejects: 0x0111 stale_epoch=3 ...` every 5 s while the totals change.
- **Pending queue.** A message addressed to an object the receiver does not have yet may be held for at most 10 s, then dropped and counted as expired.
- **Enemy combat.** The host owns every enemy. A guest's damage to a puppet is not applied there: its amount, flags, part, hit vectors and the enemy's net id and UUID go to the host (ENEMY_HIT), which refuses a payload that does not parse, an amount outside (0, 10000], a part outside -1..255, a non-finite vector, more than 20 hits per enemy or 120 in all per second from one sender, an enemy whose authority record belongs to another slot, and an enemy that is missing or already dead (each counted by the reject counters), and applies the rest to the real enemy through the engine's damage function with the applying flag set and the guest's body as the attacker. Damage the host's enemies deal to the guest's body is sent to that guest (PLAYER_HIT) and applied to its own player. The host says when an enemy died with ENEMY_GONE(Died), the same message that reports a despawn: the guest kills the puppet with the engine's own kill. A frame is accepted only from the slot allowed to send it: ENEMY_HIT from any peer to the host, PLAYER_HIT from the host.

## Game messages of the Crimson Desert adapter (0x0300-0x03FF, adapter-owned)

Range 0x0300-0x03FF so logs never confuse them with another game's. Integers little-endian, structs packed (`adapters/cd/src/wire_cd.h`); a payload of the wrong size, a non-finite float or an out-of-range enum is refused and counted. Built so far: session, presence, equipment, minimap icon and pin, containment, field time, story (replay off by default) and the guest's save hold (with replay); the full list planned is in `coop-scratch\cd\design\adapter.md` section 11.

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0300 | CD_HELLO | each to all, on every join | yes | 44 bytes: u16 proto (1), u32 exe TimeDateStamp, u8[32] SHA-256 of the adapter DLL, u32 installed hook sets (1 funnels, 2 tick, 4 equipment, 8 minimap, 16 containment, 32 story), u8 catalogue ok, u8 role (0 host, 1 guest); a peer with another proto, stamp or hash gets nothing shared |
| 0x0301 | CD_WORLD | each to all | yes | 24 bytes: u16 load serial (0 = no load yet), u8 gate state (0 Title, 1 Loading, 2 AwaitLocalReady, 3 Ready), u8 flags (1 paused, 2 dead, 4 cutscene, 8 boss stage), u32 field key (0 unknown), f32 position[3], u32 own player key; on change and every 2 s |
| 0x0302 | CD_MARKER | each to all | yes | 20 bytes: u32 marker id, u64 sender clock ms, u64 QPC; lines up both machines' logs and captures |
| 0x0303 | CD_COUNTERS | each to all | no | u16 count (at most 64), count x {u16 id, u32 value}; every 5 s while the values change |
| 0x0310 | CHAR_TEMPLATE | each to all | yes | u16 generation, u16 load serial, u32 user size, u32 body size, then the sender's own two ActorLoadDataAck payloads (each at most 0x7FC0 bytes) untouched; after the sender's gate is Ready for 2 s, again on a new generation and to every joiner |
| 0x0311 | CHAR_MOVE | each to all | no | u16 load serial, u16 generation, u32 seq, the sender's MoveActorReq without its history (234 bytes) |
| 0x0312 | CHAR_ACTION | each to all | yes | the same 8-byte header, the sender's StartActionReq (725 bytes) |
| 0x0313 | CHAR_ROTATE | each to all | no | the same 8-byte header, the sender's RotateActorReq (176 bytes) |
| 0x0315 | CHAR_EQUIP | each to all | yes | u16 template generation, u32 sender's newest equipment serial, u8 count (at most 64), count x {u16 slot (at most 63), u32 static item id (0 empty), u8 template had the slot filled}: every slot changed since that template; on change and to every joiner |
| 0x0370 | FIELD_TIME | host to all | yes | 11 bytes: u64 raw time of day at sending (a day is 75,600,000 raw, night counts half), u16 scale, u8 flags (1 fixed-time level, 2 sleeping); every 5 s while the host is Ready |
| 0x0390 | STAGE_BRANCH | host to all | yes | 22 bytes: u32 log id (not 0), u32 seq (dense from 1), u32 sequencer key, u32 branch the host's instance left (0xFFFFFFFF unknown), u32 branch, u16 the host's Req flags; per branch the host's server applied, and the whole log to every joiner; a guest posts it only with story_replay=1 and the branch-table guard passing |
| 0x0318 | CHAR_GONE | each to all | yes | u8 reason (0 leaving, 1 loading): remove the copy |

## Game messages of the Dragon's Dogma 2 plugin (0x0200-0x02FF, adapter-owned)

Integers little-endian, structs packed (`adapters/dd2/src/wire_dd2.h`). Built so far: session, body streams (Arisen and main pawn), the world clock, body cards, ENEMY_ROSTER (0x0230: u16 the host's load serial, u16 count at most 2048, count x {u8[16] UniqueID row Guid, i32 index}; host to all once its roster settled 3 s, and to joiners), the enemy sync (0x0231-0x0234, on in co-op unless adapter.ini `enemy_sync=0`) and the profile module bit; the full list planned is in `coop-scratch\dd2\design\adapter.md` section 5.

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0200 | GAME_HELLO | each to all, on every join | yes | 16 bytes: u32 DD2.exe TimeDateStamp, u32 type database hash (FNV-1a of the type, method and field counts), u32 names hash (FNV-1a of the hooked rows), u16 proto (1), u16 module mask (1 world gate, 2 body, 4 clock, 8 offline, 16 puppet, 32 profile, 64 plague off, 128 roster, 256 enemy sync) |
| 0x0210 | BODY_STATE | each to all | no | 56 bytes: u32 seq, u64 sender us, u8 kind (0 Arisen, 1 main pawn), u8 flags (1 held, 32 dead, ...), u16 load serial, f64 position[3] (world), f32 yaw, f32 speed type, u32 action hash of layers 0 and 1; 30 Hz in plain gameplay, 4 Hz held |
| 0x0211 | ACTION_EVENT | each to all | yes | 24 bytes: u32 seq, u64 sender us, u8 kind, u8 layer, u8 priority, u8 flags, u32 action hash, u32 reserved; on every change of a layer's action |
| 0x0240 | CLOCK | host to all | yes | 24 bytes: f64 seconds since the start of the in-game day, u32 day, f32 time scale, u32 serial, u8 flags (1 locked), u8[3] reserved; every 5 s and on an hour change; a guest more than 60 game seconds off sets its clock (never backwards unless the host loaded since) |
| 0x0203 | BODY_CARD | owner to all | yes | 16-byte header: u8 kind (0 Arisen), u8 version (1), u16 entry count (at most 16), u32 FNV-1a 32 of the entry bytes, u32 entry bytes length (at most 256 KiB), u32 reserved; then entries {u16 tag (look slot: 0 HumanStatus, 1 Weapon, 2 Job, 3 PartSwap, 4 BodyEdit, 5 FaceEdit, 6 BodyDetail), u16 length, the context as the engine's via.Serializer JSON}; on change (checked every 10 s in plain gameplay) and to every joiner |
| 0x0231 | ENEMY_STATE | host to all | no | u16 the host's load serial, u16 count (at most 48), count x 68 bytes {u8[16] UniqueID row Guid, i32 index, f64 position[3], f32 yaw, f32 HP, u32 action hash[2], i32 locomotion speed, u8 flags (1 dead), u8[3]}; 10 Hz, the host's enemies within 120 m of a guest's Arisen |
| 0x0232 | ENEMY_HIT | guest to host | yes | 40 bytes: u8[16] Guid, i32 index, u8 attacker kind (0 Arisen, 1 main pawn), u8[3], f32 damage, f32 reaction, i32 reaction level, i32 damage type; the host replays it through HitManager.directDamage (at most 20 per target and 120 per sender each second, 0 < damage <= the target's full HP) |
| 0x0233 | ENEMY_SPAWN | host to all | yes | u8[16] Guid, i32 index, u32 length (1 to 32 KiB), then the enemy's generate record (app.GenerateManager.GenerateRequestCharacterSaveData) as the engine's reflection JSON; answers ENEMY_WANT, 4 per second |
| 0x0234 | ENEMY_WANT | guest to host | yes | the ENEMY_ROSTER layout: the keys the guest is missing once its own roster settled, each key once per guest load |
| 0x0201 | PLACE | each to all | yes | 16 bytes: u8 place (0 title, 1 loading, 2 in game, 3 menu, 4 cutscene, 5 down, 6 travel, 7 rift), u8 flags, u16 load serial, u32 world epoch, u32 state store serial, u32 profile commit serial (the last three 0 until those modules exist); on change and every 2 s |
