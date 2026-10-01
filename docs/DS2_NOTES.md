# DEATH STRANDING 2: reverse-engineering notes

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

## Leads for an in-world remote body (stretch goal, not yet tried)
- Script-exported names that create or place entities: `CreateEntityFromSpawnSetup`, `CreateEntity`, `CreateMenuPreviewEntityForEntityResource` (builds a preview entity from an EntityResource, a candidate for a Sam look-alike without AI), `SetWorldTransform`, `PlaceOnWorldTransform`, `TeleportTransform`. Resolve them with `tools/ds2/symbols.py`.
- Reflected types: `SpawnSetup` (0x108), `SpawnpointGame`, `DSPlayerEntityResource` (0x580), `HumanoidResource`, `HumanoidComponentResource`.
- The reference mods' recipe (docs/FINDINGS.md: DD2gether, RE4R): spawn a real character, switch off its own brain at the animation-state-machine / AI level, then drive transform plus animation state from the owner.

## Live findings (2026-10-01, game version v1.10.89.0)
- Start-up: `DS2.exe` first shows its own launcher window (Play / Settings / Quit, not DX12), then the game. The title screen already runs the world of the last save with the player and a camera, so the chain resolves before Continue.
- `crs-handler.exe` (the crash reporter in the game folder) also loads `version.dll` from the game folder; the proxy runs the adapter only when the host exe is `DS2.exe`.
- Axes: Z up. RotMatrix rows are right, forward, up (the player's up row was (0, 0, 1)). Yaw = atan2(forward.x, forward.y).
- FOV is horizontal: with the vertical reading a marker over Sam sat about 130 px off; with the horizontal reading it sits on his head.
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
