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
coop-launcher                     open the window (game picker, Host, lobby code + Join, Invite, Leave, log)
```

With any argument the launcher runs as a command-line program (it attaches to the console it was started from, so from cmd the prompt returns at once; use `start /wait` to block); with none it opens the window and joins an accepted overlay invite automatically.

## Window

The window is a native app: icon, title `Co-op Launcher`, remembered size and position, DPI aware (system DPI), one window per user session (a second start brings the running window forward and exits; a running copy that does not answer is closed and replaced). Closing the window leaves the session and shuts Steam down once. When Steam is not running the state reads Offline and the launcher retries every 3 seconds.

## Settings

The Settings button (top bar) opens the settings view. Stored as JSON in `%AppData%\CoopLauncher\settings.json`, read once at start, written on every change: `CheckForUpdates` (check for a newer build at start; the Update button always checks), `GameFolders` (per-game folder chosen with Browse, else Steam's), `Window` (restored bounds). The launcher's own log is `%AppData%\CoopLauncher\logs\launcher.log` (the previous run as `launcher.prev.log`); the settings view opens that folder and also creates `report-<time>.zip` there (launcher logs, version.txt, per game the coop folder's adapter.log/ini, peer logs and crash dumps). A game's adapter log is `coopdapter.log` in its game folder.

The host log prints `Lobby created: <id>`; friends can also join from the overlay invite.

## Updates

At startup (window and CLI, unless the setting is off) the launcher checks `https://api.github.com/repos/<repo>/releases/tags/latest`. The repo comes from `update.json` next to the exe (`{ "repo": "OWNER/REPO" }`; the placeholder or a missing file disables updating). The release title is `Build <number>`; if it is newer than `version.txt` next to the exe (missing = 0) the launcher downloads `RE0-Coop.zip`, renames existing files in the package to `*.old`, copies the new files over, relaunches with the same arguments and exits. `*.old` files are deleted on the next start. Only runs from the packaged layout `<root>\launcher\app\coop-launcher.exe` and only writes below `<root>`. `.github/workflows/release.yml` builds and publishes the release on every push to main. The window's Update button only installs the new files; reopen the launcher to use them.

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

Profiles live in `games/<id>.json`; `supportedBuilds` lists the Steam build ids the adapter fits (the installed one is read from the appmanifest; Host and Join refuse another build, and BUILD_INFO makes host and guest match). Adapter files are copied into the game folder (found via Steam's libraryfolders.vdf); a differing foreign file is backed up once to `<dst>.cfbak`.
