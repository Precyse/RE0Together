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

With any argument the launcher runs in the console as before; with none it opens the window and joins an accepted overlay invite automatically.

The host log prints `Lobby created: <id>`; friends can also join from the overlay invite.

## Updates

At startup (window and CLI) the launcher checks `https://api.github.com/repos/<repo>/releases/tags/latest`. The repo comes from `update.json` next to the exe (`{ "repo": "OWNER/REPO" }`; the placeholder or a missing file disables updating). The release title is `Build <number>`; if it is newer than `version.txt` next to the exe (missing = 0) the launcher downloads `RE0-Coop.zip`, renames existing files in the package to `*.old`, copies the new files over, relaunches with the same arguments and exits. `*.old` files are deleted on the next start. Only runs from the packaged layout `<root>\launcher\app\coop-launcher.exe` and only writes below `<root>`. `.github/workflows/release.yml` builds and publishes the release on every push to main.

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

Profiles live in `games/<id>.json`. Adapter files are copied into the game folder (found via Steam's libraryfolders.vdf); a differing foreign file is backed up once to `<dst>.cfbak`.
