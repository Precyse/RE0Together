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
Replicate: log every change by name (cheap diagnostics); decide per phase: pause-mirror (menus/messages/map/save), run-on-both (cutscenes from shared scripts), or authority (dead/game over).
Seen in: RE0: sRoomControl +0xb8, request 0x60a340, 24 phases (RE0_NOTES "Room phases").

## Saves live behind a storage API the mod can proxy  [persistence]
Shape: the game reads and writes saves through a platform storage interface (Steam Remote Storage), reached through one import.
Find it: the import slot of the storage accessor; the interface vtable slots for write/read/exists/size.
Replicate: guest reads the host's save from a session folder; host cloud writes are reported so the launcher re-sends the file (full-file transfer with hash check, not through the game link).
Seen in: RE0: import 0xcb1458, ISteamRemoteStorage v012 slots 0/1/3/10/12; owner check 0x612600 bypassed.
