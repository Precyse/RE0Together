# Co-op framework research (2026-09-28)

Sources studied (study-only; DD2gether license forbids derivatives, CrimsonDesertCoop has no license = all rights reserved):
- DD2gether alpha 0.31 — `Downloads\DD2gether Alpha 0.31 ...` (REFramework Lua ~97k lines + .NET 8 launcher; decompiled C# in session scratchpad)
- CrimsonDesertCoop — `Downloads\CrimsonDesertCoop` (C++ ASI, ~8k lines, abandoned 2026-05-22)

## DD2gether architecture
- **Launcher owns Steam** (appid 480, ISteamNetworkingMessages ch0, Reliable|NoNagle vs unreliable, AutoRestartBrokenSession, ping/backoff recovery, 10s heartbeat). Steam lobby max 4; owner = slot 0 = host, rest sorted by SteamID; lobby carries `wire`/modver.
- **Game <-> launcher over loopback**: Lua -> TCP 127.0.0.1:9091 newline-framed JSON; launcher -> UDP :9092 one datagram per message. Launcher strips/re-injects identity fields (`_cid/_src/_sid/_slot`) — identity comes from transport, not payload.
- **Epochs + world fence**: `_sid` rotates on lobby/world change; Lua holds gameplay closed until the launcher echoes a world token.
- **Tick**: fixed 30 Hz accumulator (max 4 catch-up steps), declarative module manifest -> orchestrator with 6 engine phases, role filters (host/guest/any), pcall isolation. Hook registry: one native hook per method, many ordered handlers, SKIP sentinel.
- **Remote players = puppets of the game's own character class.** Native AI blocked by SKIP hooks on requestActionCore / requestMoveAction / setDestination unless the call is flagged as a puppet request. Per tick: position+rotation (warp), action name (replayed via requestActionCore), speed (drives native locomotion so no sliding), motion bank/id/frame on 2 layers (corrector forces changeMotion on mismatch, 30-frame cooldown). Distance-tiered send cadence.
- **Edge events** (grab/climb start/end): episode-numbered, re-sent 5x @0.1s, idempotent.
- **Enemies/NPCs host-authoritative**: host allocates time-seeded sequential IDs + roster reconcile; guests spawn puppets via game prefab system, limit native spawning, AI off, can't die locally. Guests send **damage descriptors** (target id, hit shape, joint, flags); host rebuilds a real hit so staggers/kills are native; HP flows back.
- **Save isolation**: launcher redirects the game's save mount to a per-profile dir via a nonce-tagged routing file; in-game hook validates mount and blocks load/save until ack. Host campaign checkpoint shipped to guests in 32 KiB chunks.
- **Weaknesses**: JSON on hot path, hex chunking over unreliable, no RTT/clock sync, hardcoded if/elseif router, full-mesh broadcast.

## CrimsonDesertCoop state
- Keep: ASI + SafetyHook + DX12 overlay skeleton, MemoryScanner/AOB helpers, Steam SteamNetworkingSockets wrapper (fix handshake-before-connected bug), community offset catalog as leads.
- Broken/stub: client never reaches CONNECTED; remote player only on host; companion hijack picks arbitrary slot + nulls +0x48 (crash risk); animation sync is dead code poking evaluator fields; enemy sync reads wrong container with pointer-derived IDs; quest/cutscene/world/fast-travel log-only; inline hooks at mid-function sites; ticks on render thread; target game version unspecified.
- Blocking unknowns: play-action-by-id function, actor enumerator, stable entity IDs, quest/cutscene/world managers, fast-travel apply, game-thread tick.

## Cross-game framework plan
1. **Shared launcher** (Steam lobby/P2P, loopback bridge, identity/epochs, payload integrity, save redirection) — game-agnostic.
2. **Binary envelope** (1-byte type, length, reliable flag), fragmentation in launcher, `register(type, {reliable, rate, priority, authority, on_receive})` registry, RTT/clock-offset ping.
3. **Per-engine adapter** exposing the same contract: `enumerate_actors, spawn_puppet, block_ai(puppet), warp, play_action(id), set_locomotion(speed,dir), read_anim_state, force_anim, apply_hit(descriptor)`.
   - RE Engine (DD2, RE0 remaster): REFramework Lua — closest to DD2gether.
   - UE5 (Hogwarts Legacy): UE4SS/C++ — ACharacter spawn, AIController unpossess, montages by asset name, CharacterMovement.
   - BlackSpace (Crimson Desert): C++ ASI — must first find play-action and actor enumeration; hook game-thread update.
4. **Gameplay core** (engine-neutral): puppet manager (cid, pi, generation), episode edge events, host-authoritative entity IDs + roster, damage descriptors, distance cadence, anim corrector.
