# Multiplayer mods, pass 2: Decima tooling and more precedents for DS2

Researched 2026-10-03 from public sources (README pages, search summaries, repo front pages). Ideas only, in our own words; no GPL code copied (decima-workshop, decima-native and others are GPL-3.0, so read for structure, do not paste). Confidence: **[doc]** = repo README read, **[sec]** = search summary or press only, **[gap]** = could not verify. Most items below are README-level; no source files were read, so internals are marked where unknown. Pass 1 (`MULTIPLAYER_MODS.md`) already covers Nitrox, Tilted, SkyMP, Seamless Co-op, BeamMP, FiveM, CyberpunkMP, cyber.rest, Kenshi, Valheim; not repeated.

## 1. Decima engine: what exists (priority)

No co-op or multiplayer mod exists for any Decima game that we could find (HZD, HFW, DS1, DS2). We would be first. Guerrilla reportedly had working 2-player co-op in HZD early on and cut it [sec: https://wccftech.com/horizon-zero-dawn-working-coop/], so the engine's entity model is not inherently single-player, but nothing of that is public.

| Project | What it gives us | DS2 relevance | Source |
|---|---|---|---|
| **odradek** (ShadelessFox) | Asset viewer and **type-schema extractor** for HFW and **DS2 v1.10.89.0**; has an `odradek-rtti` module, nightly builds, CLI export [doc] | Direct: RTTI type and field offsets for DS2. Newest and only DS2-aware dumper. Check its schema output before dumping ourselves | https://github.com/ShadelessFox/odradek |
| **decima-native** (ShadelessFox, GPL-3) | Injector that exports the engine's RTTI as **JSON and IDA mappings**; RTTIKind enum, `Array<T>`/`Ref<T>` layouts [doc, sec] | Gives the structure layouts (arrays, refs, type kinds) we need to walk entities from C++. Game list not stated; verify against DS2 | https://github.com/ShadelessFox/decima-native |
| **Cauldron** (Hengle fork) | Rust mod loader via `winhttp` proxy DLL; crates `libdecima` (types and addresses), `cauldron` (core API), **`pulse` = RTTI and symbol dumper**. Lists HZD, HFW, DS1; **DS2 not mentioned** [doc]. Repo has a `.noai` marker, so do not feed its code to agents | Design reference for a plugin API; likely needs DS2 addresses | https://github.com/Hengle/cauldron |
| **DecimaLoader** (Fexty12573) | C++ plugin loader for HFW: `plugins/` folder, `plugin_initialize()` with an event-subscription options struct; StorageExpander plugin uses RTTI structs [doc] | Shape of a minimal event API (init, per-frame, shutdown) | https://github.com/Fexty12573/DecimaLoader |
| **hfw-gameplay-tweaks / hzdr-gameplay-tweaks** (Nukem9) | `winhttp.dll` injected cheat menu: player god mode, faction, inventory editor, **entity spawner with ~2000 catalogued entries**, NPC faction and variant overrides, **time-of-day and weather setup overrides**, free camera, photomode [doc] | Closest existing proof that in-process code can spawn entities by catalogue id, set faction, drive time and weather on Decima. Read their approach for spawn and weather entry points (RTTI names), then find the DS2 equivalents | https://github.com/Nukem9/hfw-gameplay-tweaks , https://github.com/Nukem9/hzdr-gameplay-tweaks |
| **DeathStranding2Fix** (Lyall) | ASI plugin (Ultimate ASI Loader + safetyhook), camera, FOV, cutscene pillarbox [doc] | Proves DS2 PC accepts ASI loading and inline hooks; a pattern source for camera and cutscene flags | https://codeberg.org/Lyall/DeathStranding2Fix |
| **Decima Workshop** (ShadelessFox, GPL-3) | GUI to browse and edit core objects with full type info, repack archives. Supports DS1, DS1 DC, HZD; **not DS2 or HFW** [doc] | Use odradek for DS2. Workshop is useful to read DS1 data to understand how Order/Structure/Weather objects are laid out (same lineage) | https://github.com/ShadelessFox/decima |
| **Decima-Explorer, HZDCoreEditor, ProjectZeroDawn, decima-loc, DeciWaves, DecimaTools** | Archive unpack/pack; .NET core file editing; HZD file-format research; localisation; audio extract; DS1 PC RE notes (Wunkolo) [doc/sec] | File-format docs for DS1 and HZD; the DS2 archive format may share structure. Lower priority than RTTI | https://github.com/Jayveer/Decima-Explorer , https://github.com/Nukem9/HZDCoreEditor , https://github.com/neptuwunium/ProjectZeroDawn , https://github.com/Wunkolo/DecimaTools |
| **DeathStrandingModLoader** (NRTnarathip) | MinHook-based injector for DS Director's Cut, `ModLauncher.exe` [doc, thin] | Little detail; ignore unless needed | https://github.com/NRTnarathip/DeathStrandingModLoader |
| **parcel-thief** (Skippeh) | Private server for **DS Director's Cut's own online layer**. Rust: `parcel-server`, `parcel-client` (launcher/injector), `parcel-common`, `parcel-proxy`, `parcel-save-tool`, `parcel-data-export`. HTTPS REST (endpoints `findQpidObjects`, `createObject`, delete highway resources/roads...); PostgreSQL; redirects via `parcel-server-url` file/env/flag; hard-coded auth URL offset in client [doc] | **Most DS-specific find.** It documents the game's own model for shared world objects: "Qpid objects" (anything buildable: ladders, postboxes, bridges, vehicles), lost cargo, shared lockers, highways/roads (repairable), likes and strand. This is the data schema for our structure-sync problem, and area ids Western/Central/Eastern. Account-id hashing unresolved there | https://github.com/Skippeh/parcel-thief |

### Decima takeaways
- Everyone hooks through a **proxy DLL (`winhttp.dll`) or ASI loader**, and everyone depends on **RTTI dumps** rather than hand-found offsets. DS2 has a ready dumper (odradek) and an in-game JSON/IDA exporter (decima-native).
- Spawn by **catalogue id** and faction override is already demonstrated on HFW. The spawner list implies entity archetypes are RTTI objects referenced by id, so a DS2 spawn path via the game's own spawn setup (consistent with PATTERNS "Borrow a loaded body", not raw memory creation) should be findable by RTTI name search.
- DS's online layer (Qpid objects, shared lockers, lost cargo) means the **engine already has serialisable "world-shared object" records with area ids**. Reusing that record format for structure sync is the cheapest path; parcel-thief shows the shape without us reverse engineering the server protocol (and we do not touch Kojima's servers).
- Gaps: no public Decima netcode analysis, no entity/weapon-attach docs, no DS2 Cauldron support confirmed.

## 2. Other mods, same topics

Compact form. Transport/NAT = T, remote body = R, animation = A, enemies/combat = E, world/quest/save = W, desync = D, tooling = Tl, failure = F.

### sekiro-coop (Rust, two players, DLL via me3) [doc]  https://github.com/mstampfli/sekiro-coop
Most relevant single-player-PC co-op with an authority design.
- **Arch**: seven layers: memory SDK (AOB patterns, per-version offsets, pointer chains), typed core, bridge (hook callbacks into a typed event stream), rollback (snapshot ring, delta compression with spawn/despawn tracking), authority table, net, injected DLL with a 60 Hz ticker and ImGui overlay.
- T: raw UDP plus handshake and reliability layer; no NAT traversal.
- R: **blocked**. Writing position into the character instance was accepted but ignored by the render path; plan is to drive puppets via the game's event script VM. Lesson for us: writing transform into the entity does not always move the rendered body; find the driver the renderer reads.
- A: unresolved (no known animation-play native).
- E: host owns all enemies; enemy state at 5 Hz delta-filtered by a 64-bit per-entity digest (about 97 percent skipped when idle); client applies only HP decrements. Proximity handoff with hysteresis, untested live.
- Hooks: six detours (SetFlag, ApplyEffect, DeleteEffect, GiveItem, AddExperience, WarpBonfire); **replicated events are applied by calling the original through the trampoline so they never re-enter the detour** (echo suppression by construction).
- D: state hash every 60 frames, three strikes kills the session.
- Tl: peer-simulator (UDP stand-in second instance), aob-scanner, live-inspector, determinism-probe, HANDOFF.md/GAPS.md.
- F: no damage-application hook found, rollback not wired, 2 players only.

### OOT True Co-op on Ship of Harkinian + Anchor [doc]  https://github.com/bghill95/OOT-True-Co-op , https://github.com/garrettjoecox/anchor
- **Anchor**: generic relay (rooms/teams); the client owns all game logic; server only forwards "give item"/"set flag" and serves "load save state from a remote player" for late join. About 25 packet types in the True Co-op fork.
- E: **one authority instance runs enemy AI, the other renders the same enemies (position, movement, animation); the guest's hits are sent to the authority and validated by the enemy's real damage code so i-frames and vulnerability windows hold.** Boss support through an `ActorSyncAdapter` interface (Gohma first), hooks in collision and skeleton-animation systems.
- F/D: kill-switch setting falls back to shared-HP-only mode. Sync is per-actor-type adapters, not generic.

### sm64ex-coop / sm64coopdx [sec]  https://github.com/coop-deluxe/sm64coopdx , https://deepwiki.com/coop-deluxe/sm64coopdx
- Client-server up to 32 players, direct sockets or the CoopNet relay (relay solves NAT).
- Other players are the game's own Mario objects driven by received state; every player's Mario state is local-authoritative for self.
- Objects use sync objects with an owner (`sync_object_is_owned_locally` API); Lua mods declare shared state with `gGlobalSyncTable` and per-player `gPlayerSyncTable`, auto-replicated. Idea: a tiny declarative shared-state table beats hand-written messages.
- F: mods written for one player break with more (Lua moveset thread); lesson that every "the player" assumption needs a per-index form.
- Unverified: object handoff rules, deepwiki page rate-limited when fetched [gap].

### Hollow Knight Multiplayer (HKMP, SSMP for Silksong) [sec/doc]  https://github.com/Extremelyd1/HKMP
- Client-server, peer-hosted or standalone; **UDP with DTLS, UDP hole punching through a matchmaking server (MMS), Steam P2P option**. Private lobby by code.
- Remote player: game objects with skins and name tags; per-player animation-clip sync [sec]. Addon API for server logic. PvP damage per ability type.
- Entity sync per scene with a scene "host" is reported, but we could not read it [gap].

### Super Mario Odyssey Online [doc]  https://github.com/CraftyBoss/SuperMarioOdysseyOnline
- Hooks via Starlight (exlaunch lineage) on Switch; dedicated server (community-written); up to 10 players; almost every capture (possession) synced, 2D mode and costume models. Moons shared. Transport detail not documented [gap]. Lesson: **sync "transformations" as state enums the game already has**, not raw animation.

### Breath of the Wild multiplayer [sec]
- Wii U version on Cemu; early approach: replace an NPC's placement and model with Link and feed inputs to both instances; online via Hamachi; 9 to 32 players reported. Closest precedent for "puppet = existing actor repurposed". Source not public [gap].

### CelesteNet [sec]  https://github.com/0x0ade/CelesteNet , https://deepwiki.com/0x0ade/CelesteNet/2-server-architecture
- Server roles: TCP acceptor (reliable), UDP receiver (unreliable), handshaker, TCP/UDP sender. Ghost entities, shared berries/keys. Lesson: **two lanes: reliable TCP for state events, UDP for poses**.

### Just Cause 2 Multiplayer [sec]  https://github.com/jc2mp
- SharedObject, NetworkObject, WorldNetworkObject classes; **server-controlled sync rates (vehicles 75 ms, on foot 120 ms)** and a **stream distance** that controls what is visible to whom. Lua gamemodes server and client.

### MTA:SA [doc]  https://wiki.multitheftauto.com/wiki/SetElementSyncer
- Only peds and vehicles get a **syncer** (a player who reports state to the server); syncable range 140 units for vehicles, 100 for peds; syncer re-selected automatically when out of range unless persisted; **only the syncer may change a ped's health**; last occupant of a vehicle becomes its syncer. Matches the hoster model in pass 1, with an explicit range handoff.

### Mafia II Online / MafiaHub Framework [sec]  https://github.com/MafiaHub/Framework , https://github.com/mafia2online/m2o-reborn
- ENet over UDP (original), ECS-based entity management and world streaming, **virtual worlds** partition replication (players only see entities in their virtual world), JS/TS scripting. Idea: virtual-world id as a replication filter for "which area/order instance".

### Dark Souls 3 / Remastered seamless co-op (Yui), Sekiro online [sec]  https://www.nexusmods.com/darksouls3/mods/1895
- DS3: replaces P2P with Steam networking; persistent sessions across death and boss kills; fog walls removed. Closed source; same lineage as ERSC. See sekiro-coop above for the only open attempt.

### Marvel's Spider-Man PC multiplayer (hbgda) [sec]  https://www.gamesradar.com/games/spider-man/marvels-spider-man-officially-has-its-first-working-multiplayer-mod-and-its-probably-the-closest-thing-weve-got-to-insomniacs-canceled-great-web-project/
- Closed beta, Patreon-gated, up to 16 players; free roam, hideouts, combat and stealth challenges work; **campaign and side quests were not designed for it and crash or bug** when tried. Strong precedent for scoping DS2 co-op to free roam plus host-only orders. No technical detail public [gap].

## 3. Adopt for DS2

| # | Open problem | Adopt | Source precedent | Concrete step |
|---|---|---|---|---|
| 1 | **Drawn weapon on remote body ejected from hand slot** | Treat the attach as game-owned state: drive the puppet's equip through the same action the game uses for the local player (draw/holster state enum) instead of writing the weapon transform; if the entity's transform write is ignored by render (as Sekiro's), find the attach/skeleton-binding object the renderer reads. Check the RTTI for attachment/slot types in the odradek dump and compare a working local-player instance with the puppet field by field | sekiro-coop render-path lesson; SMO capture as state enum; Nukem9 inventory/equip editing | Dump RTTI fields of the weapon-attach object on local player vs puppet, diff, find the slot/bone-binding field; watch-and-undo per PATTERNS "When there is no veto point" if it is re-ejected each frame |
| 2 | **Host-owned enemies, guest puppets** | Copy OOT True Co-op: guest hits are routed to host and applied via the enemy's real damage path; guest only renders position, movement, animation. Keep a kill-switch to shared-HP mode. Add MTA-style range-based syncer handoff, and Sekiro's per-entity 64-bit digest for 5 Hz delta filtering | OOT True Co-op, sekiro-coop, MTA | Per-archetype adapter interface (BT, MULE, boss) like `ActorSyncAdapter`; digest per enemy; host-only HP writes |
| 3 | **Order/quest ownership** | Host-only, per flag-set message; apply remote events by calling the original function **through the trampoline** so replication never re-enters the hook (no echo). Late join by loading host snapshot | sekiro-coop trampoline apply, Anchor late join, Spider-Man precedent (campaign breaks) | Wrap Order state setters; guest toasts for refused quest actions (already in pass 1 #6) |
| 4 | **Structure sync** | Use the game's own shared-object model: structures are "Qpid objects" with area id in DS online; see parcel-thief for field set and object types. Sync as create/destroy/damage deltas. Do not use Kojima servers | parcel-thief | Find the in-game create/delete function for a Qpid-type object by RTTI name; replicate through it; confirm record fields against parcel-common types |
| 5 | **Weather and time sync** | Time and weather overrides exist in-process on HFW (set time of day, weather setup, transition interval). Host broadcasts (time, weather id, transition) at join and on change; guests set via the same entry points and pause their own advance | hfw-gameplay-tweaks; JC2 server-controlled rates | Search DS2 RTTI for the HFW-equivalent weather and time-of-day objects; send absolute time plus clock offset |
| 6 | **Area streaming and despawn** | Per-player interest cells plus pending-snapshot queue (pass 1). Add MTA's rule: syncer re-chosen when out of range; and a virtual-world id (MafiaHub) so an entity belongs to an area instance. Hide puppet until the terrain under it exists | MTA syncer, MafiaHub virtual worlds, JC2 stream distance | Reuse existing ownership-epoch design with a range-based handoff and hysteresis (Sekiro's untested attempt shows to test it) |
| 7 | **Two-PC testing** | Build a **peer-simulator**: scripted fake second instance that completes handshake and streams recorded snapshots; plus an AOB validator and a snapshot-diff probe between instances. Add a state hash every N frames with a strike counter but keep it as a debug alarm, not a disconnect (pass 1 rejects full hashing) | sekiro-coop tooling | Extend `fake_peer.py` with replay of recorded traces; add digest diff tool for two instances on one machine |
| 8 | **Tooling speed-up** | Start RTTI work from the odradek DS2 schema and decima-native's JSON format; do not re-derive structure layouts for Array/Ref/RTTIKind. Use `winhttp.dll`/ASI loader conventions we already likely follow | odradek, decima-native, DecimaLoader | Spike: run odradek on DS2 1.10.89.0, check if it matches our game version; if so import its type names into the PATTERNS doc and the IDA/ghidra DB |
| 9 | **Late join, rejoin** | Anchor's model: new player loads a remote player's save state, then live messages. Matches PATTERNS "Late join = older save + live snapshot" | Anchor | Nothing new; confirms the existing design |
| 10 | **Transport/NAT** | Hole punch via a small rendezvous server or a relay fallback; reliable lane for state events, unreliable lane for poses. DTLS available in HKMP's design | HKMP, CelesteNet, sm64coopdx CoopNet | Document in the roadmap; our raw-IP join is fine for testing |

## 4. Not found or unverified
- No Decima multiplayer or networking RE writeup; no public DS2 entity-attach analysis; no confirmed DS2 support in Cauldron or decima-native (check repos directly).
- HKMP, sm64coopdx, SMO Online, BotW, Spider-Man, DS3 seamless: architecture below the README level is **not** verified (no source read). Treat their rows as pointers to read next, in this order: HKMP (scene-host), sm64coopdx (sync object ownership), CelesteNet (transport).
- parcel-thief's protocol details need source reading (`parcel-common`) before relying on them.

## 5. Sources
- https://github.com/ShadelessFox/odradek
- https://github.com/ShadelessFox/decima-native
- https://github.com/ShadelessFox/decima
- https://github.com/Hengle/cauldron
- https://github.com/Fexty12573/DecimaLoader
- https://github.com/Nukem9/hfw-gameplay-tweaks
- https://github.com/Nukem9/hzdr-gameplay-tweaks
- https://github.com/Nukem9/HZDCoreEditor
- https://github.com/Wunkolo/DecimaTools
- https://github.com/Jayveer/Decima-Explorer
- https://github.com/neptuwunium/ProjectZeroDawn
- https://github.com/Skippeh/parcel-thief
- https://github.com/NRTnarathip/DeathStrandingModLoader
- https://codeberg.org/Lyall/DeathStranding2Fix
- https://github.com/mstampfli/sekiro-coop
- https://github.com/bghill95/OOT-True-Co-op
- https://github.com/garrettjoecox/anchor
- https://github.com/coop-deluxe/sm64coopdx
- https://github.com/Extremelyd1/HKMP
- https://github.com/CraftyBoss/SuperMarioOdysseyOnline
- https://github.com/0x0ade/CelesteNet
- https://github.com/jc2mp
- https://wiki.multitheftauto.com/wiki/SetElementSyncer
- https://github.com/MafiaHub/Framework
- https://github.com/mafia2online/m2o-reborn
- https://www.nexusmods.com/darksouls3/mods/1895
- https://www.gamesradar.com/games/spider-man/marvels-spider-man-officially-has-its-first-working-multiplayer-mod-and-its-probably-the-closest-thing-weve-got-to-insomniacs-canceled-great-web-project/
- https://wccftech.com/horizon-zero-dawn-working-coop/
