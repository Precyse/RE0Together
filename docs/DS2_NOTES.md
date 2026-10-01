# DEATH STRANDING 2: reverse-engineering notes

Roadmap and stages: `DS2_ROADMAP.md`.

## Target design: the host is the world server
- **Initial world sync:** the guest starts from a full snapshot of the host's world: the host's save transferred to the guest's session folder and loaded there (RE0's launcher save sync and save_redirect). Strand content (other players' structures, signs, items) that is fetched online per account must be captured on the host and sent; the guest must not fetch its own during co-op. Open question: which strand content is in the save.
- **Dynamic stream:** the host streams structures (built, damaged, destroyed), world cargo, enemies, BTs, weather and timefall, order and quest progress, facility connections. The guest's game never decides these: local authority is suppressed and the host's events are applied (RE0 guest enemies and flags).
- **The guest is an ally:** runs, carries, fights and picks up; never creates or progresses the world.
- **Guest restrictions (first):** no terminals, orders or quest triggers on the guest (blocked at the interaction check, as RE0's act-on-trigger detour); guest world pickups are requested from the host, which removes the object from its world and confirms before the guest adds it to its rack.
- **Guest as a container:** on the host, the guest's body is a cargo container. First version: an adapter-drawn give/take menu that moves items between the host's and the guest's racks with the game's own add/remove item calls on each side; later, the game's native container/transfer UI on the remote body.
- **Own cargo stays own:** each machine simulates its own Sam's inventory, rack, weight and balance; the partner's load is a visual mirror sent on change.

## Recon
- Install: `G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH` (Steam app 3280350, buildid 23923251). DEATH STRANDING DIRECTOR'S CUT is not installed on this PC (only old settings and telemetry folders on D: and F:), so the work targets DS2 directly.
- Engine: Decima (Kojima Productions fork; PC port by Nixxes). `DS2.exe` x64, image base 0x140000000, **ASLR on** (addresses below are file VAs; the adapter finds code by byte pattern). `LocalCacheWinGame\fullgame.dll` is a 140 MB code library with no imports and exports such as `InitRTTI` and HTN planner tables (compiled game data).
- Graphics: DX12 only; Streamline (`sl.interposer.dll`, DLSS / frame generation), FSR and XeSS loaders. Keep frame generation off while testing the overlay.
- Protection: no anti-cheat module or strings (EasyAntiCheat, BattlEye and the rest are absent; "Javelin" and "Ricochet" hits are game content). No SteamStub or packer sections, no Denuvo signs. Online features are the asynchronous Social Strand System (PSN account linking through the PlayStation PC SDK runtime).
- Saves: Steam cloud (`profile.dat`, `autosave*.dat`, `manualsave*.dat`, `checkpointsave*.dat`); the PoC never touches them. Crash reports: `%APPDATA%\KOJIMA PRODUCTIONS\DEATH STRANDING 2 - ON THE BEACH\crs`.
- Community route: no loader. Prior art studied clean-room (behaviour only, no code): ShadelessFox/decima-native (GPL-3.0; injects as a `winhttp.dll` proxy, dumps RTTI), ShadelessFox/odradek (DS2/HFW asset viewer and type-schema extractor).
- Chosen route: native proxy DLL (`version.dll`; the exe imports VERSION.dll for one function) plus MinHook, because no loader exists.

## Decima reflection (RTTI)
- Every reflected type has a static record in `.data` with id -1 until the factory registers it: `{i32 id, u8 kind, u8 flags, ...}`. Kind 4 = compound. Compound: base count +6, attribute count +7, size +0x10, name +0x40, bases +0x58 (16 bytes each: type, offset), attributes +0x60 (0x38 bytes each: type, u16 offset +8, u16 flags +0xA, name +0x10; a null type marks a category). The attribute count overshoots on some types; the table ends at the first malformed entry.
- `tools/ds2/rtti_scan.py` reads them straight from the exe (12315 compound types) and prints a type with inherited fields at absolute offsets.
- The exe also keeps MSVC RTTI (`.?AVPlayer@@`), so class vtables are found statically (`tools/ds2/disasm.py vtable <Class>`) and live objects are named by their vtable (`probe.py dump`).

| type | size | fields used |
|---|---|---|
| WorldTransform | 0x40 | WorldPosition (3 doubles) +0, RotMatrix (3x3 floats) +0x18 |
| Entity | 0x330 | Orientation (WorldTransform) +0xE8, Mover +0xC0, Model +0xC8, Components +0xA0 |
| CameraEntity : Entity | 0x560 | FOV +0x3D4 (**horizontal**, degrees; 73.7 in gameplay), NearPlane +0x43C (0.2), FarPlane +0x440 (10000) |
| Player | 0x128 | no reflected fields (see below) |
| PlayerGame : Player | 0x800 | the local player object |

## Script-exported functions
Decima registers script-callable functions by name ("Player_ExportedGetEntity" / "GetEntity"); the registration code loads the name and passes the function address. `tools/ds2/symbols.py` and the `Player` symbol group (registration at 0x1402534a0) give:

| function | address | body |
|---|---|---|
| Player::GetLocalPlayer(int) | 0x140256070 | `PlayerManager` global at 0x14623DF40; index 0 with the static filter {0, 1, 2, 0} at 0x1441EBEC0 returns `manager + 0x48 + index*8` |
| Player::GetEntity | 0x140268ff0 | `[player + 0x48]` |
| Player::GetLastActivatedCamera | 0x140254b00 | shared lock at player +0x100; camera stack count i32 +0xF0, data +0xF8, 0x80-byte entries; the last entry's first qword points at the camera's WeakPtrRTTITarget (+0x20), so camera = value - 0x20 |
| Player ctor | 0x140253970 | vtable 0x14312c108; PlayerGame ctor 0x140750a90 (vtable 0x143191b68) |

Confirmed live (2026-10-01): `PlayerManager` is a `PlayerManagerGame` (+0x48 and +0x60 both hold the `PlayerGame`), `Player +0x48` is a `DSPlayerEntity`, `Player +0xC8` a `GameViewGame`, `Player +0xE8` the `NetPlayerGame`, `Player +0x30` the `PlayerResourceGame`; the camera stack holds one `CameraEntity` in gameplay.

Local player chain used by the adapter: `PlayerManager -> +0x48 (Player*) -> +0x48 (Entity*) -> +0xE8 WorldTransform`. Camera: `Player -> camera stack -> CameraEntity -> +0xE8 WorldTransform, +0x3D4 FOV`.

## In-world remote body (2026-10-01)
**Humanoid working:** a background thread scans every private read-write region for `DSNpcGroundMover` objects and takes the nearest owner that has a model (`Entity.Model` +0xC8) and no `DSAnimalComponent` (components: `Entity` +0xA0 u32 count, +0xA8 pointer array). Near the DHV Magellan that is a porter ("Local Porters"): `tools/ds2/out/human-2.png` shows Sam and the porter side by side, driven by the echo peer. NPC entities live in other heaps than the player's (the earlier player-heap-only scan found only kangaroos); a full in-process scan takes a few seconds off the game threads. A process-wide census in the probe found DSPorterComponent x110, DSNpcGroundMover x27 (kangaroos, animals, bandits with DSBanditComponent and DSNpcEquipWeaponComponent, porters); many porter entities far away have no Mover/Model (not streamed in).

**First version:** with `remote_body=1` the adapter borrows an NPC the game already loaded (an entity whose `Entity.Mover` +0xC0 is a `DSNpcGroundMover`; the mover's +0x48 is its entity; plain components keep their entity at +0x28) from the player entity's heap allocation, moves it every simulation tick to the peer's smoothed pose with `Entity::SetWorldTransform`, and puts it back where it was found 3 s after the peer stops reporting (verified: the NPC returned about 550 m away). At the save point the only such NPCs were kangaroos (all four shared one EntityResource), so the first body is a kangaroo, not a humanoid: `tools/ds2/out/body-6.png`.
- **Simulation thread:** engine calls go through `main_thread`: Player::GetLastActivatedCamera is hooked; a 3 s census of its callers picks the busiest thread (one thread, about 340 calls/s). The game window's thread pumps with GetMessage and never reaches PeekMessageW, so it is not the simulation thread.
- **EntityResource::CreateEntity** (0x140169e60, behind the export at 0x140185790): (resource, const WorldPosition*, const RotMatrix*, unused, bool awake, name?, parent?, const {u8 kind; void* target}* context, spawnSetup?). Context kind 2 with no target adds to the world (0x14019bc60 -> 0x140134d60). Results: from the render thread it faulted in an engine list append (DS2.exe+0x2467c84, a race); from the simulation thread with the player's DSPlayerEntityResource it faulted in player-only component setup that expects a Player (DS2.exe+0xe4dfa4); with an NPC's EntityResource it faulted at DS2.exe+0x1239b66 (a component init given a null object, probably the missing SpawnSetup). Each fault left an engine lock held and froze the game: never catch a fault in the middle of an engine call and carry on.
- **Entity.Resource** (+0x68) is a StreamingRef: +0x68 -> pointer -> handle; handle +0x20 = the resource object.
- Next for a humanoid: borrow from a place with human NPCs (porters, Magellan crew) and filter by a humanoid-only component, or pass a real SpawnSetup to CreateEntity.

### First attempt (static notes)
- **Spawn exports are stubbed in the exe.** `SpawnSetup_ExportedCreateEntityFromSpawnSetup` (helper 0x140729410, registered from 0x140725e35) and `EconomyManagerResource_sExportedCreateMenuPreviewEntityForEntityResource` (helper 0x14085d0b0, from 0x14085622b) are registered with the shared function 0x1400bb3c0 = `xor eax, eax; ret`. Calling them would do nothing; the real spawn path has to come from SpawnSetup / Spawnpoint code itself (virtuals, `SpawnCommand`), not the script table.
- **Entity::SetWorldTransform is real and lock-safe:** export 0x14014a880 (`Entity_ExportedSetWorldTransform`, helper 0x1401469c0) calls 0x140120070(entity, const WorldTransform&): takes the entity's SRW lock at +0x2A8, copies the 0x40 bytes to +0xE8, sets dirty bit 0 at +0x98, unlocks; then 0x1401201a0(entity) propagates. This is the primitive to drive a puppet body once one exists.
- **No humanoid to borrow at the save point.** A live scan outdoors by the DHV Magellan found one DSPlayerEntity (Sam) and one AIEntityHumanoid, which is Sam's own AI view (+0x8 Scene, +0x10 the DSPlayerEntity). The RE4R/DD2gether "take a real character and switch off its brain" recipe needs a spawned or nearby NPC first.
- `probe.py instances` reads every private region (about 7 GB here) and took 7 minutes; narrow it before using it in a loop.
- Script-exported names that create or place entities: `CreateEntityFromSpawnSetup`, `CreateEntity`, `CreateMenuPreviewEntityForEntityResource` (builds a preview entity from an EntityResource, a candidate for a Sam look-alike without AI), `SetWorldTransform`, `PlaceOnWorldTransform`, `TeleportTransform`. Resolve them with `tools/ds2/symbols.py`.
- Reflected types: `SpawnSetup` (0x108), `SpawnpointGame`, `DSPlayerEntityResource` (0x580), `HumanoidResource`, `HumanoidComponentResource`.
- The reference mods' recipe (docs/FINDINGS.md: DD2gether, RE4R): spawn a real character, switch off its own brain at the animation-state-machine / AI level, then drive transform plus animation state from the owner.

## Live findings (2026-10-01, game version v1.10.89.0)
- Start-up: `DS2.exe` first shows its own launcher window (Play / Settings / Quit, not DX12), then the game. The title screen already runs the world of the last save with the player and a camera, so the chain resolves before Continue.
- `crs-handler.exe` (the crash reporter in the game folder) also loads `version.dll` from the game folder; the proxy runs the adapter only when the host exe is `DS2.exe`.
- Axes: Z up. RotMatrix rows are right, forward, up (the player's up row was (0, 0, 1)). Yaw = atan2(forward.x, forward.y).
- FOV is horizontal: with the vertical reading a marker over Sam sat about 130 px off; with the horizontal reading it sits on his head.
- Projection check (self marker at a fixed 1.75 m above the entity origin, which is at Sam's feet): the diamond sits on the top of his cap from behind indoors at 3 m, with the camera turned right, from a low angle looking up, and outdoors at 4 m from the front and from the side. The camera entity's transform is the render eye (not an orbit pivot): the alignment holds as the camera orbits. The swap chain is the output size (1920x1080) even with DLSS rendering lower, and the projection uses the output size, so the render scale does not matter. Z is up with no sign flip.
- The fixed offset follows posture, not the head bone: walking stooped under cargo puts the head about 0.3 m lower and forward of the origin's vertical, so the diamond floats above and behind his head in motion. Reading the head joint from the skeleton would pin it in every pose.
- DX12 overlay: the swap chain hook saw the game's queue (`dx12: swap chain created on queue`), back buffers are `R10G10B10A2_UNORM` (format 24) or `B8G8R8A8` (28) with 6 buffers; drawing ran from the launcher's title through gameplay with no fault, DLSS on.
- One-PC proof: two local launchers plus `tools/echo_peer.py --game ds2 --offset-x 3` (1 s delay). The echoed PLAYER_STATE shows as a marker named after the launcher's peer ("local-27982") 3 m beside Sam and follows him a second later.

## One-PC test
```
coop-launcher host ds2 --transport local --local-port 27981 --peer-port 27982 --no-launch
coop-launcher join 0 --transport local --local-port 27982 --peer-port 27981 --bridge-port 27990 --game ds2 --no-launch
python tools/echo_peer.py --port 27990 --game ds2 --delay 1.0 --offset-x 3
```
Copy `adapters/ds2/version.dll` into the game folder (there is no `version.dll` of the game's own to back up), optionally `coop/adapter.ini` with `self_marker=1`, start the game, press Play and Continue. Remove `version.dll` and the `coop` folder afterwards. Starting a launcher resets every profile with a `saveSync` block (RE0's `adapter.ini` gets `coop=0`); keep test launchers running rather than restarting them while another agent tests RE0.

## Traps
- `symbols.py` pairs a name with the fifth-argument address of the registration call; for some symbol groups that slot holds a shared no-op stub (0x1400bb3c0 for `CreateMenuPreviewEntityForEntityResource` and `CreateEntityFromSpawnSetup`). Read the registration code by hand before trusting such a result (the name lea is followed by the function lea, as in the Player group).
