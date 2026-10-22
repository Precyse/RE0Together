# DEATH STRANDING 2 adapter

`version.dll` proxy for DEATH STRANDING 2 (Steam 3280350): each player's Sam is sent as PLAYER_STATE (0x0100) at 60 Hz, and every peer shows up as a labelled marker above their position, drawn over the game's DX12 frames. Build and file layout: `CODEMAP.md`. Research notes and the one-PC test: `docs/DS2_NOTES.md`.

## Install and remove

Put `version.dll` and `coop\` in the game folder and start the game through Steam or the launcher (a direct DS2.exe start exits at the Steam check). Remove both to play vanilla. An unsupported DS2.exe build installs no hooks and shows one toast.

## Graphics

DLSS frame generation is fine. FSR or XeSS frame generation turns the overlay off (log line `dx12: frame generation ...`).

## Keys

- F6 warps you beside the partner (refused while you are dead, riding or driving, in a cutscene, a menu or a loading screen).
- F7 opens the cargo menu (your backpack and the guest's): arrows pick a piece and a side, Enter moves it across. The game does not see those keys while the menu is open. A guest asks the host, which decides.
- F9 resyncs: asks every peer and resends your own state. The game also sees F9.
- F at the door of the vehicle the partner drives rides along as a passenger; F again gets off.

## On screen

A marker with name, distance, health % and state (DEAD, DOWN, LOADING, MENU), an edge arrow when the partner is off screen, a "too far" line past 700 m. Toasts: joined, left, died, is back, unsupported game version. The partner's body is hidden while the partner is dead or loading.

## Pause

Game menus do not stop the world while linked; the partner sees MENU.

## Settings

`<game dir>\coop\adapter.ini`, defaults shown:

```
port=27980
overlay=1
self_marker=0
remote_body=1
enemy_sync=1
weapon_sync=1
cutscene_sync=0
gear_restore=0
weapon_attach_mode=1
```

- `self_marker=1` also marks your own Sam.
- `remote_body=0` turns the partner's body off: a second player entity (own camera, Sam's costume, the game's own walking and vehicle get-in and get-off) that follows the partner and rides the vehicle the partner drives.
- `enemy_sync=0` turns off sharing of the host's enemies: the host reports them, a guest's own enemies follow the host's, and a partner's hits on an enemy land on the host.
- `weapon_sync=0` turns off the partner's body holding the weapon the partner has drawn and playing the partner's shots; `weapon_attach_mode=N` tries another way of attaching that weapon.
- `cutscene_sync=1` shares cutscenes (not run live yet).
- `gear_restore=1` makes a guest keep the gear it gained across sessions (`coop\personal_gear.txt`, given back at the next join).

## Test-only settings

`test_commands`, `god_mode`, `diagnostics`, `orders_diagnostics`, `cutscene_log`, `cutscene_share_min_frames`, `log_facts`, `mirror_animation` are never in a shipped ini.

## Logs

`coop\adapter.log`; crash dumps in `coop\session\Documents`.
