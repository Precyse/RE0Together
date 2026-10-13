# Launcher handoff (2026-10-07)

## Where things stand
- Launcher code is on main under launcher/. It is a Windows-native app with its own settings and shows both mods (RE0 and DS2) together.
- Latest release asset: "Build 164" (264c61e line). No launcher work was done in this session, so this is unchanged.
- Merged into main: launcher-app, launcher-invite, launcher-ui. launcher-ui (9ff14ee) has no commits ahead of main and no uncommitted changes.
- Latest launcher commits on main: 0bfe786 (merge ds2-qol), 100b8bc (HostFollow: follow only once the host has left the current lobby), 5e1ed6d (guest follows the host's new lobby, close confirms mid-session, game-build check test), a7f1d88 (startup cleanup leaves a running game's session folder alone; game names wrap; preview package never self-updates), 53234ca (redesign in the broadcast tool's operator look: game rail with Steam art, hero, session, player seats, log drawer).

## Open
- No known open launcher bug recorded. Needs its own check: run the launcher, confirm both mods show, confirm the guest follows a host's new lobby, and confirm close during a session asks for confirmation.
- Preview workflow for launcher-* branches is documented in CODEMAP.md (launcher GUI views and the preview workflow).

## Rules
- Do not commit or push unless asked.
- Do not restart a live session or game to test the launcher; stage an update as a zip instead.
- Game installs are read-only.
