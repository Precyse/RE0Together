# CODEMAP: launcher

The C# launcher: core, view models, the WebView2 host and the page. Index and "where to look for X": `CODEMAP.md`.

## launcher/ (C# .NET 8, namespace CoopLauncher)

| file | owns | key members |
|---|---|---|
| Program.cs | entry point (a windowed exe; a command-line start attaches to its console): `--webview-check` alone = prints the WebView2 runtime version (or `missing`) and exits 0/1 without a window | `Main`, `RunWindow`, `RunCli`, `WebCheckFlag` |
| CliOptions.cs | argument parsing (`--host-options k=v;k=v`: a local join's host option values, which a Steam lobby carries) | `CliOptions.Parse`, `Usage` |
| App.cs | wiring and ~100 Hz main loop (partial class: `App.cs`, `App.Status.cs`, `App.Reconnect.cs`); CLI runs one session, interactive (GUI) takes commands and returns to idle | `Run`, `Host`, `Join`, `Leave`, `Invite`, `SwitchToInvite`, ... |
| App.Status.cs | the status the loop publishes and the step it tells the lobby | `PublishStatus`, `CurrentStatus`, `PublishStep`, `PublishLobbyData`, `TrackTimes`, `ViewOf` |
| App.AdapterLink.cs | what the loop and the game's own Co-op page tell each other over the adapter link | `OnAdapterOptionSet`, `PublishLobbyState`, `OnAdapterLinked`, `OnSlotsChanged` |
| App.Swap.cs | trading characters between seats until the game starts: a guest's `RequestSeatSwap` is published in its options (`GameOptions.SwapRequestKey`, the character it wants) | `RequestSeatSwap`, `ServeSwaps`, `PublishHostCharacter` |
| App.Reconnect.cs | the paths on which a session outlives its lobby (the session, the game link and the running game are kept, `Session.Hold`) | `HoldSession`, `WatchHost`, `WaitForHost`, `ReopenLobby`, `RejoinHeldSession`, `TryRejoin`, ... |
| StartChecks.cs | what must hold before a lobby opens or a joined session starts (`Refusal?` result) | `StartChecks.Run` |
| AppStatus.cs | display status of the loop: `AppState` (idle, connecting, hosting, joined, game running, peer connected + RTT, rejoining, waiting for host, offline + `OfflineReason`), the open lobby's game, `Players` | `AppStatus`, `AppState`, `PlayerSlot`, `FriendLobby`, `PendingInvite`, `OfflineReason` |
| Updater.cs | self-update of the packaged layout from the channel's manifest (read through ReleaseFeed) | `TryInstall`, `Stage`, `Apply`, `Staged`, `RestartRequested`, `RelaunchStaged`, ... |
| ReleaseFeed.cs | the one cached read of the channel's `manifest.json` (`https://github.com/<repo>/releases/download/<channel>/`, repo and channel from `update.json`, channel defaults to `latest`) | `Read`, `Get`, `Release.Build`, `Release.Changed`, `Release.UpdateAvailable` |
| ReleaseManifest.cs | the manifest records (build = the CI run number, channel, commit, full package, components with their `Version` and files: path, SHA-256, size) and the strict `Parse` | `ReleaseManifest.Parse`, `Component` |
| InstalledRelease.cs | `installed.json` `{build, channel, commit, components}`: which release a package came from and the version of each component (`launcher`, one per game) | `PathFor`, `ReadLocal`, `Read`, `VersionOf`, `Changed`, `Matches`, ... |
| VersionLines.cs | the words of the log lines that name a component's version, one place for the updater and the mod installer | `Installed`, `Updated`, `SubjectOf` |
| ComponentUpdate.cs | the part of an update that touches no installed file | `Plan`, `Stage`, `Matches`, `Sha256Of`, `UpdatePlan` |
| AssetSource.cs | `IAssetSource`: where release assets come from (HTTP in `ReleaseFeed`, a folder in tests) | `Download` |
| ModAutoUpdate.cs | mods follow the package: installs again every installed mod that reads Update while its game is not running | `Run` |
| ModInstaller.cs | a game's mod = its profile adapterFiles (folder and generated entries already expanded to files) in the game folder | `Status`, `Install`, `Uninstall`, `ContentHash`, `ModState` |
| InstanceGuard.cs | which other launcher processes may be closed | `ShouldClose`, `CloseOtherLaunchers`, `IsCliRun`, `CliMutexName` |
| SingleInstance.cs | one launcher window per user session: named mutex; the focus listener is registered inside `Acquire` (answers a second start at once, queues the bring-forward until `OnShowRequested`) | `Acquire`, `OnShowRequested`, `HungAfterMs` |
| AppData.cs | `%AppData%\CoopLauncher` paths (or the folder named by the environment variable `COOP_LAUNCHER_DATA`, which tests point at a temp folder) | `SettingsFile`, `LogsDir`, `ArtDir`, `WebViewDir`, `LogFile`, `PreviousLogFile`, ... |
| AppSettings.cs | the launcher's settings | `AppSettings.Current`, `Read`, `Update`, `WithGameFolder`, `Option`, `WithOption`, ... |
| GameFolders.cs | a game's folder: the settings override, else Steam's (`SteamLibrary.FindGameDir`); a found folder is remembered per game id until `Invalidate` (a game not found is looked for again) | `Find`, `Invalidate` |
| ParentConsole.cs | command-line starts attach to the starting console (the exe is windowed, no console flash) | `Attach` |
| MtFramework.cs | readers of MT Framework data, read only: `MtArc` (ARC v7: header and table, one entry read and inflated from a file opened shared) and `MtTexture` | `MtArc.ReadEntry`, `MtTexture.Decode`, `DecodedTexture` |
| OutfitArt.cs | where an outfit's picture is in the game folder (`Locate`: the profile's `outfitArt` pattern, a DLC outfit's first `DLC\<id>\costume_*` that has it, a value's own `art`), how it is taken | `OutfitArt.Locate`, `Load`, `Silhouette` |
| Fnv1a.cs | FNV-1a 32 bit over bytes, the one hash for cache keys (`OutfitArt`) and for a save slot's identity (the adapter computes the same over the same slot bytes) | `Fnv1a.Hash` |
| Blowfish.cs, BlowfishTables.cs | Blowfish in ECB mode as the RE0 save uses it | `Blowfish`, `DecryptEcb`, `EncryptEcb` |
| PeImage.cs | reads bytes of a Windows executable by virtual address through its section table (PE32 and PE32+, read only, shared open) | `PeImage.ReadAt` |
| SaveIndex.cs | the pure decoder of a game's save file (profile `saveIndex`) | `SaveIndex.Parse`, `Empty`, `Failed`, `NameHash`, `HashOf`, `SaveSlot.Label` |
| LobbyStateText.cs | the pure LOBBY_STATE text (docs/CONTRACT.md 0x0062) | `LobbyStateText.Build` |
| AdapterOptionRequest.cs | the pure rule for an option the game's Co-op page asks to change | `AdapterOptionRequest.WhyRefused` |
| SaveIndexFiles.cs | reads the slots from this machine: `ReadHost` (the host's save where `SaveSyncCoordinator.HostSaveDir` finds it) and `ReadSession` (a guest's synced copy in `coop\session`) | `ReadHost`, `ReadSession` |
| OptionSources.cs | options whose values this machine supplies at run time (profile option `source`) | `SlotOption`, `Values`, `SlotHash`, `SessionCopyMatches`, `SlotKey`, `HashKey` |
| tools/launcher_tests/AvatarTests.cs | unit test of the avatar pixel conversion (RGBA to BGRA, to a bitmap) | `AvatarTests.Run` |
| tools/launcher_tests/CardStateTests.cs | unit tests of the card state mapping (states and words) | `CardStateTests.Run` |
| tools/launcher_tests/SessionScreensTests.cs | unit tests of the session screens' mappings | `SessionScreensTests.Run` |
| tools/launcher_tests/SessionCardsTests.cs, GameStatusTests.cs | unit tests of the window's pure mappings: friends, notices and their fixes, resolved refusals, the home view; the rail / settings lines, the mod button and `CanHost` | `SessionCardsTests.Run`, `GameStatusTests.Run` |
| tools/launcher_tests/ScreensTests.cs, LobbyRowsTests.cs | unit tests of `Screens.For` (a guest in the lobby is on hosting, joining only for a refusal or a starting game) / `Between` and of `LobbyRowModels` (rows, filters, the action rules, the red mod build) | `ScreensTests.Run`, `LobbyRowsTests.Run` |
| tools/launcher_tests/CrashFilesTests.cs | unit tests of crash reporting: destination path and names, `CrashQueue` (new after start, stable size, once, 64 MB cap), `CrashIncidents` | `CrashFilesTests.Run` |
| tools/launcher_tests/LobbyListingTests.cs, LobbyDirectoryTests.cs | unit tests of the lobby data: `LobbyListing.Decode` from a dictionary (missing and malformed numbers), the `RosterCodec` round trip and malformed lines, listing equality, `LobbyDirectory.Select` | `LobbyListingTests.Run`, `LobbyDirectoryTests.Run` |
| tools/launcher_tests/BlowfishTests.cs, SaveIndexTests.cs, OptionSourcesTests.cs, SaveFixture.cs | unit tests of the forced save load's launcher half | `BlowfishTests.Run`, `SaveIndexTests.Run`, `OptionSourcesTests.Run` |
| Session.cs | slots, epochs, membership diffs, frame routing (a frame for a slot with no peer is dropped); `SlotOf(member)` (the session slot, null before the slot map has it); forwards the bridge's `OptionSet` | `ApplyMembership`, `Hold`, `Rebind`, `HostMissing`, `RttOf`, `IsSilent`, ... |
| FreshQueue.cs | pure newest-wins queue for a profile's `freshTypes` (unit tested) | `FreshQueue.Accepts`, `Enqueue`, `Drain` (an empty queue returns at once: every pump calls it), `ParseTypes`, `ForProfile` |
| MemberGrace.cs | pure grace state (unit tested): `Update(slotMembers, present, now, graceFor)` says who is held, who returned inside the grace (resync, not reset) and who expired, each member with its own grace | `MemberGrace.Update`, `ExpiryDue`, `HeldIds`, `ExpireAll`, `MsLeft`, `PeerGraceMs`, ... |
| PeerBudget.cs | token buckets on peer game frames (unit tested) | `PeerBudget.Admit`, `Remove`, `TakeReport`, `TokenBucket` |
| SlotAssigner.cs | owner = 0, rest sorted by id | `Assign` |
| LoopbackBridge.cs | adapter TCP link: HELLO check, heartbeat, timeout, relay; `SaveChanged` event for SAVE_CHANGED 0x0060, `OptionSet` (payload) for OPTION_SET 0x0061 | `Pump`, `Send`, `AdapterReady`, `GameFrame`, `Port`, `DroppedUnreliable` |
| Framing.cs | frame record, wire/loopback codec, message type constants; `Reliable(type, payload)` + `NoSlot` = the one builder of launcher-owned reliable frames | `Frame`, `Msg`, `Framing`, `Reliable` |
| ControlMessages.cs | control payload builders/parsers (all through `Framing.Reliable`, timestamps unreliable); `Welcome` = slot, host slot, max players, epoch, u64 local id (15 bytes) | `Welcome`, `PeerUp`, `TryParseHello` |
| PeerStats.cs | ping schedule, EWMA RTT per slot (`RttOf`, the slowest as `RttMs`), last answer per slot (`SilentFor`: no pong for 5 s, a slot never answered counts from its first ping), stats report | `Tick`, `OnPong`, `RttOf`, `SilentFor`, `Remove` |
| ITransport.cs | peer carrier interface; `Send(peer, frame, channel)` with `TransportChannel` Main (0) / Fresh (1) | `ITransport`, `TransportChannel` |
| SteamTransport.cs | ISteamNetworkingMessages channels 0 (Main) and 1 (Fresh), both received, member-only sessions | `Send`, `Pump` |
| LocalTransport.cs | UDP loopback transport + presence keepalive; one lane, the channel is ignored | `PeerPresent` |
| ILobby.cs | membership source interface; `Failure` = `LobbyFailure` (the refusal kind and the log line); `HostOptions` = the host's option values every member reads | `ILobby`, `MemberKey`, `LobbyKey`, `LobbyMember` |
| SteamLobby.cs | Steam lobby create/join (every string Steam hands back goes through `Text`: Steamworks.NET returns null for missing lobby/member data and persona names, the rest of the launcher sees empty), cf_* data | `Create`, `Join`, `Pump`, `SetLobbyData`, `Listing` |
| LocalLobby.cs | fake lobby for local testing (no join secret, the local member's data and the host's `SetLobbyData` kept in memory, `Listing` decoded from them with two seats) | `LocalLobby` |
| SteamBootstrap.cs | SteamAPI init (`SteamUnavailableException` carries Steam's init result, `NoSteamClient` = Steam is not running), callbacks, overlay invites | `PendingInvite`, `Lost`, `Pump`, `PersonaName`, `SteamNetworkingUtils.InitRelayNetworkAccess`, `SteamUnavailableException`, ... |
| ModProgress.cs | what a mod install, uninstall or launcher update reports | `ProgressMath.Percent`, `ProgressReporter`, `ThrottledProgress` |
| ModFiles.cs | pure rules of a mod's file list: `Expand` | `Expand`, `Decide`, `Removed`, `FormatManifest`, `ParseManifest` |
| PackFile.cs | reads one named entry of a Saints Row 2 version 4 `.vpp_pc` read-only (header 0x150/0x154, 0x800 alignment, directory, names, extensions) | `PackFile.ReadEntry` |
| ScriptDelta.cs | the recipe `adapters/sr2/scripts.delta.json` (written by `tools/sr2/lua_delta.py`) | `ScriptDelta.Load`, `Find`, `Build`, `ScriptDeltaException` |
| games/re0.json, games/sr2.json, games/ds2.json | per-game profiles: exe, adapter files, options and their values (re0 keys mirror adapters/re0/src/option_values.h); ds2 is not shipped |  |
| GameProfile.cs | `games/<id>.json` loader, `adapterFiles` | `Load`, `ListIds`, `NativeSession`, `RendezvousSession`, `MissingRequirement`, `HasCharacterSelect`, ... |
| GameOptions.cs | the player's option values (settings choice if still offered, else the first value) | `Current`, `HostValues`, `OwnValues`, `ForAdapter`, `Encode`, `Decode`, ... |
| SaveSyncCoordinator.cs | save sync per session: host sender or guest receiver, launch gate; the guest takes the host id from `session.HostId` | `Create`, `Ready`, `TimedOut`, `EnableAdapter`, `Pump`, `SaveSyncTimeout` |
| LogForwarder.cs | guest log mirror to the host: new adapter-log bytes, one 32 KiB chunk every 2 s (LOG_APPEND 0x0050) until `Flush` lifts the pace and sends the rest at once | `Create`, `Pump`, `Flush`, `ExpiredPeerLogs`, `IsPeerLog` |
| CrashSender.cs | guest crash files to the host: every second `Poll` reads the game process | `Create`, `Pump`, `Poll` |
| CrashReceiver.cs | host: takes the guest's crash files (FILE_BEGIN / CHUNK / END, the `<stamp>/<file>` wire name checked by `CrashRules.TryParseWireName`, 64 MiB, sha256) | `Attach`, `OnFrame` |
| CrashRules.cs | the pure rules of crash reporting: `CrashRules` (`Destination`, wire name `<stamp>/<flat name>`, `TrySplitSpec` of a profile `crashFiles` entry, `MaxFileBytes` 64 MiB, `MaxIncidents` 3), `CrashQueue` | `CrashRules.Destination`, `TryParseWireName`, `CrashQueue.Take`, `CrashIncidents.Next` |
| FileStreamer.cs | the sending half of the FILE_* transfer used by `SaveSender` (host saves) and `CrashSender` (guest crash files) | `Enqueue`, `Remove`, `RemovePeer`, `OnAck`, `Pump` |
| BuildCheck.cs | same build on every machine: host sends BUILD_INFO 0x0013 as a peer joins; a guest on a different build stops before the game starts (dev builds only warn) | `BuildCheck.Create`, `LocalBuild`, `Ready`, `Mismatch`, `Reason`, `Compared`, ... |
| Rejoin.cs | `RetrySchedule` (pure, unit tested: attempts due once per interval from one interval after the start, `GaveUp`, `SecondsLeft`) and `Rejoin` | `RetrySchedule`, `Rejoin`, `App.TryRejoin` |
| HostFollow.cs | a guest polls its host's published lobby (2 s) | `HostFollow.Find`, `MovedTo`, `App.FollowHost`, `App.WaitForHost` |
| LobbyPresence.cs | the host's open lobby and game as Steam rich presence (`cf_lobby`, `cf_game`), set by SteamLobby.Create, cleared on dispose | `Publish`, `Clear`, `Read` |
| Refusal.cs | why a start was refused: `RefusalKind`, `Refusal` (kind, detail, game, lobby), `LobbyFailure` (a lobby that cannot be used), the `BuildMismatch` enum, `RefusalVersions` | `Refusals`, `RefusalKind`, `RefusalVersions` |
| SessionProgress.cs | the step a player is in: `SessionStep` (`Ready` = everything prepared, the launch waits for the start gate), the pure `SessionProgress.Step(StepInputs)` rule, `SessionProgress.SelectionLocked(step)` | `SessionProgress.Step`, `StartGate.Open`, `StepCodec`, `SaveProgressMeter` |
| LobbyListing.cs | the lobby data keys `LobbyKeys` | `LobbyKeys`, `RosterCodec.Encode`, `Decode`, `LobbyListing.Decode` |
| LobbyDirectory.cs | finding other lobbies (Steam, loop thread) | `Search`, `ReadFriends`, `FriendListing`, `Results`, `Searching`, `Revision`, ... |
| LobbyCode.cs | a lobby id out of what a player pastes (digits, spaces, dashes, a label, a `steam://joinlobby/<app>/<lobby>/<host>` link); unit tested | `LobbyCode.TryParse` |
| FriendLobbies.cs | friends running the launcher with a lobby in their rich presence (Steam, loop thread) and the pure `Select` (known games, one row per lobby, sorted by name, at most 4) | `Read`, `Select` |
| SteamAvatars.cs | Steam avatars of the wanted users as RGBA (`AvatarImage`) | `Want`, `Pump`, `ResendAll`, `Loaded` |
| SaveSender.cs | host: chooses the files for a new peer and queues them on a `FileStreamer` (paced FILE_BEGIN/CHUNK/END, resent until ACK ok) | `SendTo`, `SendChanged`, `OnAck`, `Pump` |
| SavePaths.cs | save folder templates: `{documents}`, `{steamid64}` (Steam's ActiveUser registry value) | `Expand`, `SteamAccountId` |
| SaveWatcher.cs | host: FileSystemWatcher on the save folder; reports a changed save once it has been quiet for 3 s (retry by touching it again) | `Touch`, `TakeSettled` |
| SaveReceiver.cs | guest: temp file (`TempSuffix` .part), sha256 check, move into session dir (or the profile's `guestSaveDir`), FILE_ACK; expects the profile's list or the host's manifest | `OnFrame`, `Complete`, `PromoteStaged`, `MaxFileBytes`, `TempSuffix` |
| FileMessages.cs | FILE_* payload builders/parsers (through `Framing.Reliable`); `IsWireName` and `MaxManifestNames` = what the manifest can carry | `Begin`, `Chunk`, `Manifest`, `TryParseBegin`, `TryParseManifest`, `IsWireName`, ... |
| AdapterSettings.cs | `coop=` and the game's option keys in the adapter ini (one `SetKeys` writer; `WriteOptions` = the option keys alone, for a native-session game), session dir cleanup after copying its files | `EnableCoop(gameDir, profile, options)`, `WriteOptions`, `Reset`, `ExpiredArchives`, `ArchiveFolder`, `ArchiveRoot` |
| NativeJoin.cs | rendezvous games (SR2): writes `<game>\coop\sr2_join.txt` | `Create`, `Pump`, `Rebind`, `Readable` |
| PlayerName.cs | the in-game name from the Steam persona names (pure; tests in `tools/sr2/player_name_test`, run with `dotnet run -c Release`) | `Shape`, `ForLocal` |
| GameLauncher.cs | Steam game start, returns a `LaunchResult` (started, already running, unsupported game build, mod not installed (never installs), Steam start failed) | `Launch`, `LaunchWithOptions`, `IsRunning`, `Probe`, `LaunchResult`, `GameProcess` |
| GameBuilds.cs | the installed Steam build of a game (appmanifest beside its folder, else any library) against the profile's `supportedBuilds` (the adapter's addresses fit one executable) | `Installed`, `IsSupported`, `Describe` |
| GameWatch.cs | polled by the window while a session is open | `GameWatch.Update`, `Next`, `Linked`, `GameCondition` |
| ReportBundle.cs | one zip for sending: `report.txt` (launcher build, OS, per game folder / game build / mod state, included, truncated and skipped files with reasons), every `*.log` in the logs folder, `settings.json` | `Create` |
| SteamLibrary.cs | game folder and installed build id from libraryfolders.vdf + appmanifest; a file that cannot be read is skipped (logged once), a parsed file is reused until its write time changes | `FindGameDir`, `InstalledBuild(appId, gameDir)`, `ParseLibraryPaths`, `ParseManifest`, `ReadManifest` Custom Steam artwork: `CustomArtFiles`. |
| RepoPaths.cs | repo root / games dir discovery |  |
| Log.cs | timestamped console log; `Written` event feeds the GUI log pane; `Event` = a player-facing line (also an Info line) for `SubscribeEvents` listeners with a 50 line history | `Info`, `Event`, `FileOnly`, `Subscribe`, `SubscribeEvents`, `WriteToFile`, ... |

## launcher/Ui/ (pure view models the web snapshot uses, namespace CoopLauncher.Gui)

No controls and no painting: the mappings from `AppStatus` to what the page shows, unit tested in `tools/launcher_tests`.

| file | owns | key members |
|---|---|---|
| Ui/SteamArt.cs | where a game's capsule, hero and logo files are: the player's custom Steam art (`SteamLibrary.CustomArtFiles`, userdata config grid) first, then Steam's local librarycache | `SteamArt`, `SteamArt.Kind`, `FindFile` |
| Ui/GameStatus.cs | what a game's mod state says: the rail line | `GameStatus.Line`, `SettingsMod`, `SettingsGame`, `SettingsLine`, `Action`, `CanHost`, ... |
| Ui/SessionCards.cs | the pure mapping from `AppStatus` to what the home screen says | `SessionCards.Home`, `Friends`, `FixFor`, `NoticeFor`, `Resolved`, `NoticeFix` |
| Ui/UpdateFlow.cs | the window's one update path: reads the release in the background, offers a release when any component (the launcher or a game's mod) differs from `installed.json` | `UpdateFlow.Check`, `Click`, `CheckNow`, `CheckState`, `Restart`, `RestartOffered`, ... |
| Ui/CardState.cs | the card state of a player (`CardState` Idle, InGame, Progress, Problem, Open) from the published step, held / silent and ping, and its state words (pure) | `CardStates.Of`, `OfStep`, `Words`, `IsReady` |
| Ui/SessionModels.cs | the records of the session screens: `JoinTarget`, `JoiningModel` (title, seat line, host, steps, fix and its label, Start again), `SessionModel` | `JoiningModel`, `SessionModel`, `MiniModel`, `LobbyState`, `MiniAlert` |
| Ui/SessionScreens.cs, SessionScreens.Session.cs, SessionScreens.Mini.cs | the pure mappings from `AppStatus` to the session screens (unit tested, `SessionScreensTests`) | `SessionScreens.Joining`, `Session`, `Seats`, `Mini`, `CharacterOf` |
| Ui/Screens.cs | pure: which `ScreenKind` the status asks for | `Screens.For`, `Screens.Between` |
| Ui/LobbyRowModels.cs | pure: the Lobbies rows from the status (search plus friends' lobbies, filter, game) and each row's action (Join / Full / Update the launcher or the mod / unavailable) and red mod build | `LobbyRowModels.Of`, `ActionFor`, `ModDiffers`, `ButtonOf` |
| Ui/SelectModel.cs | the pure mapping from a status to the character select: before a session you are the left half (your `character` choice) and the other character the right with the swap | `SelectModels.Build`, `Neighbor`, `SelectModel`, `StageHalf` |
| Ui/RailModel.cs | pure rules of a rail row used by the web snapshot | `RailModel.Sub`, `Tone`, `Line`, `Locked` |
| Ui/LobbyPopupModel.cs | pure rules of the lobby popup: the seats (`PopupSeat`: named member, unnamed member, open seat; the host's row), `PLAYERS n/max`, game build ok, Network | `LobbyPopupModel.Seats`, `SeatCount`, `Caption`, `GameOk`, `Network`, `PlayingText` |
| Ui/OptionsModel.cs | pure rules of a game's option rows: which options a grid shows, whether the game has any, the value shown (the host's for a guest), the values offered in a folder, the save runtime options | `OptionsModel.OptionsOf`, `Has`, `ShownValue`, `Values`, `Save`, `SavePicker` |
| Ui/SettingsModel.cs | pure rules of a game's settings section: its line, Browse / Reset / Uninstall enabled, the archive folder and whether Archive shows | `SettingsModel.Line`, `BrowseEnabled`, `ResetEnabled`, `UninstallEnabled`, `ArchiveFolder`, `ArchiveShown` |
| Ui/LauncherActions.cs | the actions that touch Windows, used by the web controller | `LauncherActions.BrowseFor`, `ResetFolder`, `CreateReport`, `CopyLobbyCode`, `SetCheckForUpdates`, `OpenLogsFolder`, ... |
| Ui/Initials.cs | the one rule for the letter that stands in for an avatar (first text element, upper-case) | `Initials.Of` |
| Ui/StatusText.cs | the words of a refusal (`Refusal`); `Save differs from host's` | `StatusText.Refusal` |
| Ui/PlayerCard.cs | `PlayerCard` | `PlayerCard`, `CardLayout`, `Figure` |
| Ui/StepRow.cs | one line of a step list (`StepRow`: text, `StepMark`, trailing number, bar share or indeterminate, mono lines, label/value pairs) | `StepRow`, `StepMark` |
| Ui/Notice.cs | the one line the home or a session screen has to say (key, words, fix action, Report) | `Notice` |
| Ui/FriendCard.cs | a friend hosting a lobby (avatar id, name, game, lobby id, selected, player count) | `FriendCard` |

## launcher/WebHost/ (the WebView2 window, namespace CoopLauncher.Web)

The page itself is `launcher/web/` (html, css, ES modules; embedded in the exe as `web/...`, served by `WebAssets`). The host knows the page only by the bridge of `launcher/web/SPEC.md` section 3: it posts `hello`, `state`, `log` and `event` messages and reads the commands of `UiCommands`.

| file | owns | key members |
|---|---|---|
| WebHost/GuiHost.cs | GUI entry: log file, unhandled UI errors logged, App loop on a background thread | `Run`, `CreateWindow` |
| WebHost/ChromelessForm.cs | base window without the system title bar: WM_NCCALCSIZE gives the title bar to the client (maximized: client = work area), WM_NCHITTEST answers the top resize band and the caption strip | `ChromelessForm`, `CaptionHeight` |
| WebHost/NcHitTest.cs | WM_NCHITTEST message, answers and lParam screen point | `NcHitTest.ScreenPoint` |
| WebHost/NativeTheme.cs | Windows' dark frame (dark mode, border in the line colour) and dark native scrollbars | `DarkFrame`, `DarkScrollbars` |
| WebHost/WindowFlash.cs | flashes the window's taskbar button until it comes to the front (an invite arrived while it was behind) | `WindowFlash.Start` |
| WebHost/WindowMemory.cs | restores and saves the window rectangle and maximized state | `Restore`, `Save` |
| WebHost/Theme.cs | the window host's colours (`Bg`, `Bg2`, `Line`, `Raised`) and DPI metrics (`ResizeBand`, `Scale`, `Unscale`; the process is system DPI aware, so the scale is read once) | `Theme.Scale`, `Unscale`, `ResizeBand` |
| WebHost/ReducedMotion.cs | whether Windows' "Animation effects" is off (the page then plays no animation), read again after a user preference change; `Override` forces it for tests | `ReducedMotion.Reduced`, `Override` |
| WebHost/AvatarPixels.cs | Steam's RGBA avatar bytes to BGRA and to a 32bpp bitmap (also the pictures taken from a game) | `AvatarPixels.RgbaToBgra`, `ToBitmap` |
| WebHost/WebRuntime.cs | the WebView2 runtime check (`MinRuntime` 130): its version, `--webview-check`, the TaskDialog with the download link when it is missing | `Version`, `UsableVersion`, `Check`, `EnsureOrExplain`, `Explain` |
| WebHost/WebShell.cs | the main window: a `ChromelessForm` holding one `WebSurface` under the top resize band (re-laid on maximize / restore), size / place / dark frame, minimize, close | `WebShell`, `BringForward`, `OnCommand`, `SyncMini` |
| WebHost/MiniShell.cs | the always-on-top mini window (kept hidden for reuse): shown once its page drew the first state, `shown` / `miniClose` handshake with the 400 ms close timeout, Alt+F4 = Expand | `Open`, `Close`, `CloseTimeoutMs` |
| WebHost/WebSurface.cs | one hardened WebView2 (shared environment, SPEC 4 settings, navigation / new window / download / permission blocking, origin check) | `StartAsync`, `RequestState`, `Post`, `Suspend`, `Resume`, `PageReady`, ... |
| WebHost/WebAssets.cs | `WebResourceRequested` for both hosts: page files from the manifest resources, images loaded in the background behind a deferral | `Attach`, `FigureCacheCapacity` |
| WebHost/ImageRoutes.cs | the two hosts (`app.coop.example`, `img.coop.example`) and the strict image URL patterns, built for the snapshot and parsed by the assets | `WebHosts`, `ImageRoutes.SteamUrl`, `AvatarUrl`, `FigureUrl`, `Parse`, `ImageRoute` |
| WebHost/UiController.cs, WebHost/UiController.Actions.cs | the window's state and actions without controls | `UiController`, `Changed`, `BuildMain`, `BuildMini`, `Handle`, `Run`, ... |
| WebHost/UiCommands.cs | the command names (`Cmd`), the field table, parsing and validation (under 4 KB, depth 3, exact fields), the origin rule, the rate-limited file-only drop log | `Cmd`, `UiCommands.Parse`, `Types`, `OriginAllowed`, `UiCommand`, `DropLog` |
| WebHost/ModRunView.cs | a game's mod action on show (`ModRunView`: kind install / uninstall + the last `ModProgress`) and its pure mapping | `ModRunViews.Dto`, `ModRunViews.WhileRunning` |
| WebHost/UiDtos.cs | the JSON records of the snapshot (camelCase, enums as camelCase text, ids as text; `modRun` on the home and on each settings game) and `UiJson` | `MainState`, `MiniState`, `UiJson` |
| WebHost/UiSnapshot.cs, WebHost/UiSnapshot.Session.cs | the pure mapping from the controller's state to the snapshot (`SnapshotInput` in, `MainState` / `MiniState` out) using the existing mappers; the transition runs from the last posted screen | `UiSnapshot.MainPage`, `MiniPage`, `SnapshotInput`, `GameEntry` |
| WebHost/UiMessages.cs | the messages besides the snapshot (`hello`, `log`, `event`) and the `state` envelope with `seq` | `UiMessages.Hello`, `LogLines`, `Event`, `State` |
| WebHost/AvatarImages.cs, WebHost/Lru.cs | avatars encoded to PNG once on arrival (LRU of 128, a revision per avatar for the URL) and the small thread-safe LRU | `AvatarImages.Put`, `Revision`, `Png`, `Lru` |

## launcher/web (browse)

The WebView2 page (page spec: SPEC 2, 3, 6 of the launcher web UI). No build step; ES modules; text only through `textContent`.

| file | owns | key members |
|---|---|---|
| web/index.html, web/mini.html | the page shells: CSP meta, stylesheets, one module script (`main.js` / `mini-main.js`) |  |
| web/css/tokens.css | the one `:root`: colours, fonts (`--font-ui` Segoe UI Variable, `--font-display` and `--font-num` Bahnschrift; system fonts, nothing downloaded), glass tokens |  |
| web/css/base.css | reset, type, buttons (ghost, over-art and icon buttons share one hover / press language), field, select | `.btn`, `.field`, `.seg`, `.select`, `.toggle` |
| web/css/motion.css | every generic recipe as a class + keyframes (`m-*`), `html.paused`, `html.reduced` end states | `m-page-in`, `m-panel-in`, `m-modal-in`, `m-reveal`, `m-shake`, `m-stamp`, ... |
| web/css/browse.css | top bar, home, lobbies, popups, settings, drawer |  |
| web/js/main.js | boot, registry of every screen module, Esc rule, `page.rendered` after each applied state | `SCREEN_MODULES` |
| web/js/bridge.js | `COMMANDS` (SPEC 3.3), `connect`, `send`; WebView2 or the dev mock | `COMMANDS`, `connect`, `send` |
| web/js/dom.js | `h`, `svg`, `setText`, `setAttr`, `setVar`, `toggle`, keyed list with exit hook | `keyed`, `wasCreated` |
| web/js/motion.js | recipes as functions: `play`, `stagger`, `swapText`, `rollDigits`, `roll` (a number-bearing text rolls its digits, plain words are written as they are), `reel`, `shake`, `stampIn`, `iconSwap` | `play`, `tokenMs` |
| web/js/icons.js | the fixed SVG glyphs | `icon(name, size)` |
| web/js/screen-host.js | one element per screen, the transition of the state (page, panelIn, panelOut) | `createScreenHost` |
| web/js/perf.js | debug overlay: frame p95, long tasks, state apply time | `createPerf` |
| web/js/segments.js, web/js/topbar.js | sliding-pill segmented control; the top bar | `createSegments` |
| web/js/notice.js | the notice row (also used by session screens) | `createNotice` |
| web/js/home.js, hero-layout.js, rail.js, friends.js, codefield.js | Home: the hero (the whole pane while nothing is below it, 296 px otherwise; the art's clip edge and everything hanging from the hero's bottom move with transforms, never layout per frame) with the act | `home.create`, `createHeroLayout`, `run`, `arm`, `createRail`, `createFriends`, ... |
| web/js/lobbies.js, dropdown.js, format.js | Lobbies tab (filter, game dropdown, 56 px rows, skeleton then reveal); the painted dropdown (menu-dropdown recipe, keyboard); `pingText` | `createDropdown`, `pingText` |
| web/js/modal.js, popup-lobby.js, popup-options.js, popup-update.js, popup-header.js, confirm.js | modal layer (scrim + modal recipe), lobby popup (roster tints, facts, reel and digit roll), options popup (read-only for a guest), update popup | `modal.create`, `createConfirm`, `createPopupHeader` |
| web/js/mod-run.js | the mod action's step row (spinner or check or cross, text, percent, tweened bar) and Retry; used by the home hero (over the art) and each settings game's line | `createModRun` |
| web/js/settings.js, toggle.js | Settings view: Launcher section on a label / control grid | `settings.create`, `createToggle` |
| web/js/drawer.js, art.js | footer strip with the newest event and the log (last 500 lines, rises by transform); images that hide on failure, avatars | `drawer.create`, `createImage`, `createAvatar` |
| web/js/joining.js, session.js, select.js, mini-main.js | stubs for the session builder |  |
| web/dev/mock.js | dev transport: `?fixture=` / `?sequence=`, `&frame=`, `&reduced=1`, `&smoke=`, `window.__sent`, `html[data-ready]` | `createTransport` |
| tools/launcher_web/render.py, perf.py | headless Edge screenshots of fixtures and sequences (throwaway profile and server), `--frames` (ms or `%` of a sequence), `--reduced`, `--smoke`, `--dump-dom`, `--prepare-art` |  |

## launcher/web (session)

Joining, hosting, lobby, mini window, character select and their components (briefs: concepts SPEC 6-9). Text only through `textContent`; every label, flag and state comes in the DTO.

| file | owns | key members |
|---|---|---|
| web/js/spinner.js | the matrix loader (orbit variant), small and large | `createSpinner(size)` |
| web/js/steps.js | the step list: glyphs (circle, spinner, check drawn in, cross + row shake), rolling trailing digits, bar tween, indeterminate bar, lines, Host/You pairs; keyed by row index | `createSteps(size)`, `setGlyph`, `fillList` |
| web/js/cards.js | player cards in wide / tall / mini / miniPortrait | `createCards(ctx, layout)`, `createPortrait`, `createAvatar` |
| web/js/card-file.js | the field file card: unit and `P<seat> / <corner>` across the top, PLAYER / AGENT / FIELD KIT rows, the outfit figure on the right | `createFile` |
| web/js/image.js | `<img>` that hides itself when it fails to load, and its source setter | `createImage`, `setImage` |
| web/js/joining.js | joining screen: backdrop, host header (text swap), large steps, Cancel / fix / Start again | `create` |
| web/js/session.js | hosting and lobby (two instances by `ctx.screen`) | `create`, `createCode` |
| web/js/select.js | character select S1 / S3: halves, outfit slide, character swap crossing, sliding dots, Left/Right/Tab keys | `create`, `createHalf` |
| web/js/mini.js, web/js/mini-main.js, web/mini.html | the mini window page: header, seats or the alert, event footer; `shown` / `miniClose` play the modal recipes | `createMini` |
| web/css/card-file.css | the field file card: 150 px high, header strip, rows, figure masked into the card, status line, edge bar, READY stamp and flash; the amber progress edge without tint, glow or comet |  |
| web/css/cards.css | spinner, step list, check / cross recipes, card layouts, state layers and loops (bloom, breathe, pulse, comet), the portrait cards of the in-game lobby |  |
| web/css/session.css, web/css/mini.css | joining, hosting / lobby, select; the 360x220 mini page |  |
| web/dev/fixtures/(popup_update, settings_update_checking).js | the update popup over the settings view; the Check for updates button while it checks |  |
| web/dev/fixtures/(home_mod_installing, settings_mod_verifying, mod_install_failed) | the mod action's step row at 40 percent on the home, verifying in a settings row, and a failure with its reason and Retry |  |
| web/dev/fixtures/(joining, hosting, hosting_guest, lobby, select, mini, cards_states, seq_*).js, web/dev/sequences/*.js, web/dev/smoke/select_keys.js, mini_expand.js | session fixtures, the sequences |  |
