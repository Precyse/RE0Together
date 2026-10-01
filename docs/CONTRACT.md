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
| 0x0002 | WELCOME | launcher->adapter | u8 local slot, u8 host slot, u8 max players, u32 epoch |
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
| 0x0013 | BUILD_INFO | host launcher->guest launcher | i32 build number (version.txt; 0 = dev build); sent as a peer joins, before the save files; a guest on a different build refuses to start the game |
| 0x0060 | SAVE_CHANGED | adapter->own launcher | ascii name of a save file the game just wrote to the Steam cloud; the host launcher re-sends the profile's save files to every peer (same FILE_* transfer as at join) |
| 0x0051 | CRASH_DUMP | guest launcher->host launcher | u8 name length, ascii name (`crash-*.dmp`), u32 offset, bytes (≤ 32 KiB); a dump the adapter wrote during the session, sent in order; the host writes `peer_<steamid>_<name>` beside its adapter log |
| 0x0050 | LOG_APPEND | guest launcher->host launcher | raw bytes newly appended to the guest's adapter log (≤ 32 KiB, every 2 s); the host appends them to `peer_<steamid>.log` beside its own adapter log |

- `epoch` increments every time the session membership resets (new lobby, host change). Adapters drop all game state on a new epoch.
- On adapter connect, the launcher answers HELLO with WELCOME followed by one PEER_UP per current peer.
- PING runs every 1 s per peer. RTT is smoothed (EWMA, alpha 0.125) and reported through PEER_STATS every 2 s.

## Session

- Slots: lobby owner = 0 = host. The others are sorted ascending by SteamID into 1..max-1. Recompute on every membership change. If the host leaves, the session ends (epoch bump, PEER_DOWN for all).
- Lobby data: `cf_game=<id>`, `cf_proto=<n>`, `cf_ver=<launcher version>`. Members whose `cf_game`/`cf_proto` mismatch get no slot.
- Protocol version `proto = 1`.

## Game profile (`launcher/games/<id>.json`)

```json
{ "id": "re0", "name": "Resident Evil 0 HD", "steamAppId": 339340, "exe": "re0hd.exe",
  "maxPlayers": 2, "port": 27960,
  "adapterFiles": [ { "src": "adapters/re0/dinput8.dll", "dst": "dinput8.dll" } ] }
```

## Save sync (launcher-owned, game-agnostic)

- Profile key `saveSync`: `{ "steamRemoteFiles": ["data0.bin"], "sessionDir": "coop/session", "adapterIni": "coop/adapter.ini" }`. Files come from `Steam\userdata\<accountId>\<steamAppId>\remote\` on the host.
- Host: on each new peer, send every file as FILE_BEGIN, FILE_CHUNKs and FILE_END, all reliable. The host never has a session dir (it plays its own save).
- Guest: write chunks to a temp file, verify the sha256, move it into `<game>\<sessionDir>\`, and reply FILE_ACK. Launch the game only after every file has been ACKed ok (60 s timeout, then abort with a message).
- Both sides set `coop=1` in `<game>\<adapterIni>` before launching. On launcher exit, and on launcher start to recover from a crash, delete the session dir and set `coop=0`, so a normal launch plays vanilla.
- The adapter redirects the game's save I/O to the session dir whenever that dir holds the files and `coop=1`.
- Games that keep their saves outside the Steam cloud folder add `hostSaveDir` (host source), `guestSaveDir` (guest destination, relative to the game folder) and `filePattern` (e.g. `*.dat`). The folders are templates: `{documents}` is the user's Documents folder, `{steamid64}` the signed-in Steam user (from Steam's registry key). The host sends every matching file after a FILE_MANIFEST. Example: `launcher/games/ds2.json`.

## Adapter responsibilities (every game)

1. Connect to the launcher, send HELLO, and wait for WELCOME. Reconnect every 2 s if the link drops. Never block the game thread on the socket.
2. Run the network on a fixed 30 Hz tick driven from a game-thread hook, never the render thread.
3. Keep an engine-neutral gameplay core and implement the per-engine primitives:
   `enumerate_actors`, `local_player`, `bind_remote(slot) -> body`, `suppress_ai(body)`, `warp(body, pos, rot)`, `play_action(body, id)`, `set_locomotion(body, speed, dir)`, `read_anim(body)`, `force_anim(body, anim)`, `apply_hit(descriptor)`.
4. Authority: the host (slot 0) owns enemies, world flags and pickups. Guests send their own player state plus hit descriptors.
