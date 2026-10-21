# Co-op launcher

Game-agnostic launcher: owns Steam (lobby + P2P) and talks to the in-game adapter over loopback TCP. Spec: `docs/CONTRACT.md`.

## Build

The .NET 8 SDK lives at `C:\Users\ahmal\.dotnet-sdk` (not on PATH).

```
C:\Users\ahmal\.dotnet-sdk\dotnet.exe build -c Release
```

Output: `bin\Release\net8.0-windows\coop-launcher.exe` (with `steam_appid.txt` = 480 and `steam_api64.dll`). `RollForward=LatestMajor` lets it run on the installed .NET 9 runtime.

## Run (Steam)

The Steam client must be running.

```
coop-launcher host re0            create a lobby, start the game
coop-launcher join <lobbyId>      join a lobby (game taken from the lobby data)
coop-launcher +connect_lobby <id> same as join
coop-launcher                     open the window (game picker, Host, Lobby code + Join, friends hosting, Leave, log)
```

With any argument the launcher runs as a command-line program (it attaches to the console it was started from, so from cmd the prompt returns at once; use `start /wait` to block); with none it opens the window and joins an accepted overlay invite automatically. In the window the host starts the game with START GAME (a guest's game starts once the host's has), while a command-line host starts at once.

## Window

The window is a native app without Windows' title bar: the top strip is the title bar (drag it, double-click or drag to the screen top to maximize, Snap), with its own minimize and close at the right; native resize borders, shadow, taskbar entry and Alt+F4; icon, taskbar title `Co-op Launcher`, remembered size and position, DPI aware (system DPI), one window per user session (a second start brings the running window forward and exits; a running copy that does not answer is closed and replaced). Closing the window leaves the session and shuts Steam down once. Closing the window with a session open asks first. A guest follows its host into a new lobby (host crashed or relaunched) through the host's Steam rich presence (`cf_lobby`, `cf_game`). When Steam is not running the state reads Steam not running and the launcher retries every 3 seconds.

Layout (840x540 client at the least): top bar (brand, build, state lamp and words, the update button while a newer build is offered, Settings, window buttons), game rail, the game's hero (120 px), then Session (a cog at the right of its caption opens the game's options under it, closed at start), Options when open, and Players. Session: Host, the mod button (Install, Update or Repair, only when the mod needs it), the Lobby code field and Join; friends who are hosting appear under it with their own Join; in a lobby it shows Relaunch (when the game is gone), the lobby code with a clipboard icon that copies it, and Leave. A refusal (or an invite received in a lobby) shows one notice row under it with its fix button and Report. Players: one card per seat in two columns (Steam avatar or the name's initial, role, character, step, ping, save progress bar); an open seat reads Open. A guest sees the host's values of the host options, locked. The footer shows the newest player event; its arrow opens the full log. Closing with a session open asks first. Tab reaches every control, Esc closes Settings. Motion follows the broadcast tool (`Gui/Motion.cs`) and is skipped when Windows' animation effects are off.

## Settings

The Settings button (top bar) opens the settings view. Stored as JSON in `%AppData%\CoopLauncher\settings.json`, read once at start, written on every change: `CheckForUpdates` (download a newer build by itself at start), `LastGame` (the game selected last), `GameFolders` (per-game folder chosen with Browse, else Steam's), `Window` (restored bounds), `LobbyVisibility` (who can find the lobbies you host: `Friends`, the default, or `Everyone`, which lists them in every launcher's Lobbies tab; friends always see your lobby through rich presence). The launcher's own log is `%AppData%\CoopLauncher\logs\launcher.log` (the previous run as `launcher.prev.log`); the settings view opens that folder and also creates `report-<time>.zip` there (launcher logs, version.txt, per game the coop folder's adapter.log/ini, peer logs and crash dumps). A game's adapter log is `coopdapter.log` in its game folder.

The host log prints `Lobby created: <id>`; friends can also join from the overlay invite.

## Updates

At startup (window and CLI, unless the setting is off) the launcher checks `https://api.github.com/repos/<repo>/releases/tags/latest`. The repo comes from `update.json` next to the exe (`{ "repo": "OWNER/REPO" }`; the placeholder or a missing file disables updating). The release title is `Build <number>`; if it is newer than `version.txt` next to the exe (missing = 0) the launcher downloads `RE0-Coop.zip`, renames existing files in the package to `*.old`, copies the new files over, relaunches with the same arguments and exits. `*.old` files are deleted on the next start. Only runs from the packaged layout `<root>\launcher\app\coop-launcher.exe` and only writes below `<root>`. `.github/workflows/release.yml` builds and publishes the release on every push to main. The window reads the release when it opens and offers a newer build as `Update to N` in the top bar; the files are installed in the background (by themselves while no session is open when the setting is on, on the click when it is off), the button then reads `Restart`, and Restart closes the window and starts the new build. Mod buttons stay locked until then. Only one process at a time replaces the package (a lock per package folder); a start that finds it taken skips its update.

Update safety: the package is replaced under an `update.pending` marker. A copy that fails puts every original back; a replace cut short (the marker is still there at the next update check) is rolled back from the `*.old` files, otherwise leftover `*.old` files are deleted. The window opens first and stages a newer build in the background; the restart that installs it happens only when the player asks, after the window closed. A second start never closes a live launcher window or a command-line run; it closes a window only when that stays silent for 13 s and is not responding.

Files and logs: settings are written to `settings.json.tmp` and moved over `settings.json`, the previous readable file stays as `settings.json.bak` (used when the file is unreadable). `COOP_LAUNCHER_DATA` names another data folder instead of `%AppData%\CoopLauncher` (tests, UI harness). The host keeps the newest 10 `peer_<steamid>_<stamp>.log` per peer. The session archive (`<game>\coop\archive`) skips an unchanged set and keeps at most 20 archives, none older than 30 days except the newest. The report zip holds `report.txt` (builds, OS, mod state, skipped files with reasons), every launcher log and `settings.json`, never the join file, each entry cut to its last 8 MiB. A foreign file in the way of an adapter file is kept as a numbered backup (`.cfbak`, `.cfbak2`, ...) and a mod file another mod replaced shows as Modified until Install repairs it.

## Run (two players on one PC, no Steam)

```
coop-launcher host re0 --transport local --local-port 27961 --peer-port 27962 --bridge-port 27970 --no-launch
coop-launcher join 0   --transport local --local-port 27962 --peer-port 27961 --bridge-port 27971 --no-launch
python ..\tools\fake_adapter.py --port 27970 --name A
python ..\tools\fake_adapter.py --port 27971 --name B
python ..\tools\spoof_peer.py --peer-port 27962 --launcher-port 27961   (instead of the second launcher)
```

## Flags

- `--transport steam|local` (default steam)
- `--local-port N --peer-port M` UDP ports, required for local
- `--bridge-port N` adapter loopback port, overrides the profile port
- `--game <id>` profile for a local join (default: the only profile)
- `--save-source <dir>` host: folder with the save files (default Steam userdata remote folder)
- `--game-dir <dir>` game folder override (default resolved through Steam)
- `--no-launch` do not install adapters or start the game

Profiles live in `games/<id>.json`; `supportedBuilds` lists the Steam build ids the adapter fits (the installed one is read from the appmanifest; Host and Join refuse another build, and BUILD_INFO makes host and guest match). A profile may list `freshTypes` (hex frame types such as `"0x0140"`, game types only): the launcher then sends the unreliable frames of those types on a second Steam channel and, when several are waiting for the same peer at one pump, sends only the newest of each type; a profile without the key sends everything on channel 0 as before. Adapter files are copied into the game folder (found via Steam's libraryfolders.vdf); a differing foreign file is backed up once to `<dst>.cfbak`.

A guest whose host leaves the lobby keeps its session and game and waits up to 3 minutes for the host's next lobby (the state reads `Waiting for host`), then rebinds to it; a host whose own lobby fails keeps its session and opens a new lobby every 5 seconds for 3 minutes. Host and Join check the game folder, the game build, the required files and the mod first and say what is wrong (`Refusal`). A bad command line names the offending argument (`Unknown argument: X`).
