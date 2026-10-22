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
| 0x0044 | FILE_MANIFEST | host launcher->guest launcher | u8 count, then per file u8 length + ascii name; sent before the files when the profile sends by `filePattern`; the guest then expects exactly these files |
| 0x0013 | BUILD_INFO | host launcher->guest launcher | i32 launcher build number (the launcher component's version in installed.json; 0 = dev build), then i32 installed game build (Steam buildid; 0 = unknown; absent from older launchers), then u8[32] mod hash (SHA-256 over the profile's adapterFiles as installed: per file the lowercase dst name, 0x00, u64 LE length, bytes; zero = not installed; absent from older launchers); sent as a peer joins, before the save files; a guest on a different launcher build or game build refuses to start the game, and so does one with another hash (Mod files differ) or none (Mod not installed) when the host's is not zero; the first difference found counts, in the order launcher build, game build, mod; a dev build (build 0) is guarded by the hash alone |
| 0x0060 | SAVE_CHANGED | adapter->own launcher | ascii name of a save file the game just wrote to the Steam cloud; the host launcher re-sends the profile's save files to every peer (same FILE_* transfer as at join) |
| 0x0051 | CRASH_DUMP | guest launcher->host launcher | u8 name length, ascii name (`crash-*.dmp`), u32 offset, bytes (≤ 32 KiB); a dump the adapter wrote during the session, sent in order; the host writes `peer_<steamid>_<name>` beside its adapter log |
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

`sessionKind` is `lobby` (default: the launcher's Steam lobby, transport and save sync) or `native` (the game matches players itself, as Saints Row 2 does through OpenSpy: the launcher installs the mod, writes the options into `adapterIni` and starts the game; no lobby, seats, save sync or adapter link). `rendezvous` is `native` plus a Steam lobby that says who hosts (`joinFile`, `statusFile`); with `"bridgeWhen": { "key", "value" }` the launcher also runs the bridge, build check and log forwarding when the host's value of option `key` equals `value`. A game option with `"host": true` is the host's alone: guests write the host's value to the adapter ini. `requires` lists `{ "file", "name" }` entries the game folder must hold; a missing one is shown in the rail line as `<name> not found`.

## Save sync (launcher-owned, game-agnostic)

- Profile key `saveSync`: `{ "steamRemoteFiles": ["data0.bin"], "sessionDir": "coop/session", }`. Files come from `Steam\userdata\<accountId>\<steamAppId>\remote\` on the host.
- Host: on each new peer, send every file as FILE_BEGIN, FILE_CHUNKs and FILE_END, all reliable. The host never has a session dir (it plays its own save).
- Guest: write chunks to a temp file, verify the sha256, move it into `<game>\<sessionDir>\`, and reply FILE_ACK. Launch the game only after every file has been ACKed ok (60 s timeout, then abort with a message).
- Both sides set `coop=1` in the profile's `adapterIni` (in the game folder) before launching. On launcher exit, and on launcher start to recover from a crash, delete the session dir and set `coop=0`, so a normal launch plays vanilla.
- The adapter redirects the game's save I/O to the session dir whenever that dir holds the files and `coop=1`.
- Games that keep their saves outside the Steam cloud folder add `hostSaveDir` (host source), `guestSaveDir` (guest destination, relative to the game folder) and `filePattern` (e.g. `*.dat`). The folders are templates: `{documents}` is the user's Documents folder, `{steamid64}` the signed-in Steam user (from Steam's registry key). The host sends every matching file after a FILE_MANIFEST. Example: `launcher/games/ds2.json`. Before the launcher deletes a session folder (exit, crash recovery) it copies the guest's files in it to `<sessionDir's parent>rchive\<yyyyMMdd-HHmmss>\` (staging and `.part` files excluded; the last 5 archives are kept), so the guest's own progress is never lost. With `filePattern` the host launcher also watches its save folder (the game writes saves after the join and sends no SAVE_CHANGED) and, 3 s after a matching file stops changing, sends it to every peer as FILE_MANIFEST (current list) plus FILE_BEGIN/CHUNK/END. The guest takes the first full set straight into the session folder; every later file goes to `<sessionDir>/staging` and is moved into the session folder only while the game exe is not running, so a running game's own saves are never replaced.

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

## Game messages of the SR2 adapter (0x0100 and up, adapter-owned)

Used only when the host's `transport` option is `steam`: the game's UDP datagrams travel through the launchers (`bridgeWhen` in `games/sr2.json`, port 27962). The launcher relays them as opaque bytes and overwrites the source slot, as for every game message. Integers little-endian.

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x0100 | GAME_DATAGRAM | adapter to adapter | no | u32 dst (IPv4 in host order; 0xFFFFFFFF = broadcast), then the game's UDP payload (1..1300 bytes). The sender's frame slot is the peer's current slot, or 0xFF for a broadcast; the receiver reads the source slot the launcher wrote. |

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
