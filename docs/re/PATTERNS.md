# Cross-game RE patterns

The shared library for the `coop-re` skill. Each pattern is written so it can be recognised in another engine.

- Add a "Seen in" line whenever a game shows the same shape.
- Correct a pattern when a game contradicts it.
- Keep entries short and move game detail to the game notes.

Format:

```
## <name>  [tags]
Shape: what the engine does, engine-neutral.
Find it: fastest way to locate it.
Replicate: the co-op recipe (see the coop-re skill, reference/replication.md).
Seen in: <game>: <address/function>, notes file section.
```

## Door or level transition runs through one loader  [state-transition, presentation]
Shape: a door or trigger asks a loader object to start. The loader plays the transition (animation, fade), counts down, and only then changes the room or level. The room change itself also decides which party members come along.
Find it: write-watch the loader's target or timer field during a door, then walk up to the function that stores the room and entry.
Replicate: run on owner, send arguments, at the loader start (not the room change). Party carry follows the engine's own rule.
Seen in: RE0: sDoorLoad::start 0x552b50, then changeRoom 0x610c60, then partner carry in 0x61e2c0 (RE0_NOTES "Rooms, doors").

## Interactions are checked for the controlled character only  [control, state-transition]
Shape: event scripts test "the controlled player is in trigger zone N and pressed action". Other characters track their zone but are never asked.
Find it: the script condition opcode that reads the controlled-player pointer and the action pad.
Replicate: evaluate as the local player's character when the peer holds the camera. When the check passes, move focus to that character first so the action runs as the focused one.
Seen in: RE0: 0x564070, trigger zone at player +0x16e4.

## Party follow is an engine flag plus membership  [control, state-transition]
Shape: one global "partner follows" flag. A transition carries the partner only if the flag is set and the partner belongs to the current area.
Find it: diff the state with follow on against follow off; read the transition's partner branch.
Replicate: satisfy, don't fake. Mirror the co-op party mode into the flag.
Seen in: RE0: sPlayer +0x40, condition in 0x61e2c0.

## Story state is one bitset  [world-state, persistence]
Shape: scripts set and test flags by index in one manager. The save copies the same block.
Find it: script opcode names (FlagSet, SetEnemyFlag), then the handler's `this` global, then the ctor's memset size.
Replicate: bitset diff, polled, with echo suppression.
Seen in: RE0: sFlagManager 0xdcc014, bits +0x20, 0x11c bytes; set(index, count, value) 0x59c980.

## UI reads "the current player"  [ui-perspective]
Shape: inventory and status screens build from the focused character at open time.
Find it: write-watch the submenu state and take the function that sets it to its first value.
Replicate: local perspective swap for the menu's lifetime; pause camera parity while the menu is open.
Seen in: RE0: sSubMenu::open 0x5d9030.

## Pause and menu stop the world through one update  [presentation, authority]
Shape: a single "update all units" function runs each frame; menus stop the world by not running it.
Find it: follow the vtable of the unit manager; the per-group update loop.
Replicate: freeze mirror. Skip it while the peer is in a menu.
Seen in: RE0: sUnit::updateAll 0x727b50.

## Remote-controlled character driven by pad replay  [control]
Shape: a character's brain object reads a pad. Swapping the brain type (player versus AI) changes who drives it.
Find it: the think or brain pointer on the character and the pad getter slot in its vtable.
Replicate: give the remote character a player brain; while it moves, redirect the pad getter to a network pad. Restore the AI brain when the peer leaves.
Seen in: RE0: cPlayerThink / cPlayerSubThink, getPad 0x600630.

## Screens are a phase machine  [presentation, ui-perspective]
Shape: one manager holds an array of phase objects (main, map, menu, message, save, cutscene, movie, dead) and a current index; a request call queues the next phase.
Find it: class names of the phase objects (cRoomPhase*), then the manager that owns them; read the index table from the live array.
Replicate: log every change by name (cheap diagnostics); hold the other world while a player is in a menu, message, map, save screen or cutscene (a player who skips waits); death is authority-replicated.
Seen in: RE0: sRoomControl +0xb8, request 0x60a340 (reached through sRoomControl::requestPhase 0x610e00), 24 phases (RE0_NOTES "Room phases").

## Saves live behind a storage API the mod can proxy  [persistence]
Shape: the game reads and writes saves through a platform storage interface (Steam Remote Storage), reached through one import.
Find it: the import slot of the storage accessor; the interface vtable slots for write/read/exists/size.
Replicate: guest reads the host's save from a session folder; host cloud writes are reported so the launcher re-sends the file (full-file transfer with hash check, not through the game link).
Seen in: RE0: import 0xcb1458, ISteamRemoteStorage v012 slots 0/1/3/10/12; owner check 0x612600 bypassed.

## Character switching is a screen phase  [control, state-transition]
Shape: switching to the other character is a phase of the screen machine (not a pointer swap), because it may need to load the other character's area.
Find it: the phase requests made from the player's input code (push <phase>; call requestPhase).
Replicate: in the same area a direct controlled-pointer swap is enough; apart, request the switch phase on both machines.
Seen in: RE0: Change phase 9 requested from 0x4fed48 / 0x50395e.

## Drive menus with a virtual keyboard, pin the data underneath  [ui-perspective, persistence]
Shape: menus need a few confirms to reach gameplay (logos, title, load list, continue). Their meaning is simple (Enter on defaults) even when their code is not.
Find it: the input device the game polls (DirectInput GetDeviceState for RE0) and the data call the menu ends in (the load request with the cursor slot).
Replicate: tap Enter from the input proxy while outside gameplay and mute the real keys; hook the data call to force the session's value (the host's slot). Wait for the host to be in game first.
Seen in: RE0: virtual_keys (DirectInput8 proxy), session_slot (0x6134c0 / 0x613500), auto_join.

## Late join = older save + live snapshot  [persistence, world-state]
Shape: the joiner can only load a saved state; the host's live state has moved on (room, inventories, flags, AI-driven partner).
Find it: reuse the existing sync modules' read/apply points.
Replicate: after the joiner loads, request a snapshot; apply flags and inventories, travel through the host's last door (engine transition as teleport), place the partner; hold the joiner's own state broadcasts until caught up.
Seen in: RE0: join_sync.

## Areas are records; move a member with the engine's own record calls  [state-transition, world-state]
Shape: the engine keeps a small pool of area records (the loaded area plus dormant ones a party member was left in); each character points at its record. Writing the pointer or position by hand leaves callbacks and registries half-updated.
Find it: the transition that carries a partner (door into a new area): it finds or loads the target record, places the character on the entry spot, assigns it (leave/enter callbacks) and updates per-area registries. Those calls are the toolkit.
Replicate: when the remote player changes area, run that same sequence for their character alone on this machine: into the loaded area it appears at the door, out of it it leaves, between other areas its dormant record follows. No screen change, and the save and later doors see consistent engine state. Compare areas by the record's id, not by display fields that read "loading" mid-transition.
Seen in: RE0: scene records in sSceneInfo 0xdcbf40 (0x61e0f0 find/load, 0x61ed50 place, 0x619e30 assign, 0x61dde0 release), `scene::move`, split_rooms. Replaced an earlier replay (focus their character, run their door, focus back) that worked but took over the screen.

## A player action with several exits is cut at its input  [control]
Shape: one button starts an action that ends in different engine paths depending on state (a direct swap here, a phase request there).
Find it: hook the obvious exit, test every state; when an exit is missed, follow the action back to the input it reads.
Replicate: hide the input from the game during co-op (input proxy), keep a cheap corrective rule for inputs the proxy cannot see (gamepads).
Seen in: RE0: character switch V and Solo/Team E (think 0x4fec64 states; requestPhase(Change) is only the apart exit), hidden through the DirectInput keyboard proxy; on controllers Y and LT (the same in every controller type) through a filter on the XInputGetState import (0xcb13b0, imported by ordinal 2). The options screen's controller diagram is the fastest way to learn fixed pad bindings.

## The engine's reflection lists fields by name  [world-state, presentation]
Shape: the engine keeps its own type records (class name, base classes, fields with names and offsets) for serialisation and tools. They sit in static data, so a scan of the executable file finds them without running the game.
Find it: strings of familiar class names (Entity, Player, Camera) referenced from data that also points at small arrays of {type, offset, name} records; check a type whose size you can predict (a transform with a position and a rotation).
Replicate: read field offsets from the records instead of guessing from memory diffs; a struct offset that the reflection gives also survives most patches.
Seen in: RE0: MT Framework DTI (class names only, `probe.py classes`). DS2: Decima RTTI compound records with attribute offsets, `tools/ds2/rtti_scan.py` (Entity.Orientation +0xE8, CameraEntity.FOV +0x3D4; DS2_NOTES "Decima reflection").

## Script-exported functions name the entry points  [control, presentation]
Shape: the engine registers functions for its scripting or graph language by name ("GetLocalPlayer", "GetEntity", "GetLastActivatedCamera"). Each registration pairs a name string with the function address.
Find it: the name string, its one code reference (the registration), and the function pointer passed beside it. The bodies are tiny accessors that reveal the global and the field offsets.
Replicate: use these accessors' globals and offsets (found by byte pattern at run time) as the adapter's view of "the local player", "its body" and "the active camera".
Seen in: DS2: Player symbol group registered at 0x1402534a0; GetLocalPlayer 0x140256070 (PlayerManager global + 0x48), GetEntity `[player+0x48]`, GetLastActivatedCamera 0x140254b00 (camera stack at +0xF0/+0xF8), `tools/ds2/symbols.py`. Same role as RE0's script opcode table (`Story state is one bitset`).

## A remote player is first a projected marker  [presentation]
Shape: before the peer has a body in the world, the cheapest visible proof is a marker drawn over the frame at the peer's reported position.
Find it: the active camera object (position, rotation basis, field of view); the present call of the graphics API for drawing.
Replicate: hook Present (learn the command queue from swap chain creation on DX12), draw with Dear ImGui, project with the camera's own transform and field of view; keep the projection pure and unit tested. A local self-marker checks the projection against the game's own image (it caught DS2's field of view being horizontal, not vertical). A proxy DLL in the game folder is also loaded by helper processes there (crash reporters); run the adapter only in the game's exe.
Seen in: DS2: `adapters/ds2/src/dx12_hook.cpp`, `marker_overlay.cpp`, `world_to_screen.h`.

## Borrow a loaded body before you can create one  [presentation, control]
Shape: creating an entity runs the engine's full spawn pipeline (spawn setup, owner objects, thread rules); moving an existing one is a single locked transform write.
Find it: count component vtables in the player entity's heap (movers, AI) to see which characters are loaded; a component's owner pointer leads to its entity.
Replicate: for a first remote body, borrow a loaded NPC, drive its transform from the peer's state on the simulation thread, and put it back where it was found when the peer leaves. Move on to a created body once the spawn setup is understood.
Seen in: DS2: DSNpcGroundMover +0x48 -> Entity, Entity::SetWorldTransform, `remote_body`, `ds2/body.cpp`. Same idea as RE4R's "Player 2" game object and CrimsonDesertCoop's companion hijack (docs/FINDINGS.md).

## Find the simulation thread by counting callers of a per-frame accessor  [control]
Shape: modern engines run the window pump, render and simulation on different threads; engine calls that create or link objects are only safe on the simulation thread.
Find it: hook a small function the game calls every frame (DS2: Player::GetLastActivatedCamera) and count calling threads for a few seconds; the busiest is the simulation thread. The window's own thread may pump with GetMessage and never reach a PeekMessage hook.
Replicate: queue adapter work and run it from that hook on that thread only.
Seen in: DS2: `main_thread.cpp`. Trap: catching an access violation in the middle of an engine call leaves its locks held and freezes the game; fix the call instead of catching it.
