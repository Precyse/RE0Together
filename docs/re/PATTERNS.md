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
Seen in: RE0: sRoomControl +0xb8, request 0x60a340, 24 phases (RE0_NOTES "Room phases").

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

## Mirror the other player's travel with the engine's own transitions  [state-transition, world-state]
Shape: the engine tracks where every party member is, but the record is hidden; writing positions or rooms by hand gets overwritten or crashes.
Find it: the transitions that move a character between areas (door start, character-switch phase) and the flag that makes others follow.
Replicate: replay the remote player's transition locally: make their character current (swap or switch phase), run the transition with follow off, switch back. The engine keeps its own bookkeeping consistent.
Seen in: RE0: split_rooms (sDoorLoad::start 0x552b50, Change phase, follow flag sPlayer +0x40).
