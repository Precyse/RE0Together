using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>The actions of <see cref="UiController"/>: what the page's commands do, with the rules of the launcher window. A command
/// the current state does not allow, or that names something that is not on show, is refused with a reason (the caller logs it).</summary>
internal sealed partial class UiController
{
    private const string Refused = "not allowed now";
    private const string UnknownTarget = "names nothing on show";
    private const int DoneShownMs = 1500;

    /// <summary>Runs a command from the page; one that is ignored is noted in the drop log.</summary>
    public void Run(UiCommand command, DropLog drops)
    {
        if (Handle(command) is { } reason) drops.Report(command.Type, reason);
    }

    /// <summary>Runs a command from the page; null when it was carried out, else why it was ignored.</summary>
    public string? Handle(UiCommand command)
    {
        var status = _app.Status;
        var free = Free;
        switch (command.Type)
        {
            case Cmd.TabSelect when _screen is ScreenKind.Home or ScreenKind.Lobbies:
                _tab = command.Text("tab") == "lobbies" ? Tab.Lobbies : Tab.Play;
                Apply(status);
                return null;
            case Cmd.GameSelect:
                return GameFor(command) is { } picked && free ? SelectFromRail(picked.Profile) : UnknownTarget;
            case Cmd.Host when CurrentHome()?.HostEnabled == true:
                HostOrLaunch();
                return null;
            case Cmd.JoinCode when CurrentHome()?.JoinEnabled == true:
                JoinTypedCode(command.Text("code"));
                return null;
            case Cmd.JoinFriend when status.State == AppState.Idle && !ModRunning:
                return status.FriendLobbies.Any(friend => friend.LobbyId == command.LobbyId()) ? Do(() => JoinFriend(command.LobbyId())) : UnknownTarget;
            case Cmd.LobbyOpen when _screen == ScreenKind.Lobbies && _popup == PopupKind.None:
                return RowFor(command) == null ? UnknownTarget : Do(() => OpenLobbyPopup(command.LobbyId()));
            case Cmd.LobbyAction when _screen == ScreenKind.Lobbies && !ModRunning:
                return RowFor(command) is { } row && LobbyRowModels.ButtonOf(row.Action).Enabled ? Do(() => RunLobbyAction(row)) : UnknownTarget;
            case Cmd.LobbiesFilter when _screen == ScreenKind.Lobbies:
                _filter = command.Text("filter") == "friends" ? LobbyFilter.Friends : LobbyFilter.All;
                Apply(status);
                return null;
            case Cmd.LobbiesGame when _screen == ScreenKind.Lobbies:
                return SetLobbyGame(command.OptionalText("gameId"));
            case Cmd.PopupOptions when _screen == ScreenKind.Home && SelectedGame is { } selected && OptionsModel.Has(selected.Profile):
                ToggleOptions();
                return null;
            case Cmd.PopupClose when _popup != PopupKind.None:
                ClosePopup();
                return null;
            case Cmd.OptionSet:
                return SetOption(command);
            case Cmd.ModInstall:
                return GameFor(command) is { } mod && _screen == ScreenKind.Home && mod.Profile.Id == _selectedId && CurrentHome()?.ModEnabled == true
                    ? Do(() => StartMod(mod.Profile, ModRunKind.Install)) : UnknownTarget;
            case Cmd.ModRetry when !ModRunning:
                return GameFor(command) is { } retry && _modRuns.GetValueOrDefault(retry.Profile.Id) is { Failed: true } failed
                    ? Do(() => StartMod(retry.Profile, failed.Kind)) : UnknownTarget;
            case Cmd.NoticeFix when NoticeFixAllowed():
                RunNoticeFix();
                return null;
            case Cmd.ReportCreate:
                LauncherActions.CreateReport(Profiles);
                return null;
            case Cmd.SessionCancel when _screen == ScreenKind.Joining:
                CancelSession();
                return null;
            case Cmd.SessionStart when CurrentSession() is { IsHost: true, StartEnabled: true }:
                _app.StartGame();
                return null;
            case Cmd.SessionLeave when !free:
                _app.Leave();
                return null;
            case Cmd.SessionCopyCode when status.LobbyId != 0:
                if (LauncherActions.CopyLobbyCode(status.LobbyId)) PageEvent?.Invoke("copied");
                return null;
            case Cmd.SessionRelaunch when _screen is ScreenKind.Joining or ScreenKind.Hosting or ScreenKind.Lobby:
                RelaunchGame();
                return null;
            case Cmd.SessionMini when _screen == ScreenKind.Lobby && !_miniOpen:
                StartMini();
                return null;
            case Cmd.SessionPlaySolo when _miniOpen && !free:
                _app.PlaySolo();
                return null;
            case Cmd.MiniExpand when _miniOpen:
                ExpandMini();
                return null;
            case Cmd.ViewOpen:
                return OpenView(command);
            case Cmd.ViewClose when _view != null:
                ShowView(null);
                return null;
            case Cmd.SelectStep when _screen == ScreenKind.Select:
                return StepSelect(command.Number("half"), command.Number("direction"));
            case Cmd.SelectSwap when _screen == ScreenKind.Select:
                return SwapSelect();
            case Cmd.SettingsUpdates when _screen == ScreenKind.Settings:
                LauncherActions.SetCheckForUpdates(command.Flag("on"));
                Apply(status);
                return null;
            case Cmd.SettingsVisibility when _screen == ScreenKind.Settings && free:
                var visibility = command.Text("value") == "everyone" ? LobbyVisibility.Everyone : LobbyVisibility.Friends;
                AppSettings.Update(settings => settings.WithLobbyVisibility(visibility));
                Apply(status);
                return null;
            case Cmd.SettingsLogs when _screen == ScreenKind.Settings:
                LauncherActions.OpenLogsFolder();
                return null;
            case Cmd.GameBrowse or Cmd.GameResetFolder or Cmd.GameUninstall or Cmd.GameArchive when _screen == ScreenKind.Settings:
                return RunGameCommand(command);
            case Cmd.UpdateCheck when _screen == ScreenKind.Settings:
                _update.CheckNow(automatic: AppSettings.Current.CheckForUpdates);
                return null;
            case Cmd.UpdateRestart when _popup == PopupKind.Update && _update.RestartOffered:
                _update.Restart();
                return null;
            case Cmd.UpdateClick when _update.Enabled:
                _update.Click();
                return null;
            default:
                return Refused;
        }
    }

    private static string? Do(Action action)
    {
        action();
        return null;
    }

    private GameEntry? GameFor(UiCommand command) => _games.FirstOrDefault(game => game.Profile.Id == command.Text("gameId"));

    private LobbyRowModel? RowFor(UiCommand command) => Input().LobbyRows().FirstOrDefault(row => row.Listing.LobbyId == command.LobbyId());

    private HomeView? CurrentHome() => _screen == ScreenKind.Home && SelectedGame is { } game
        ? ModRunViews.WhileRunning(SessionCards.Home(_app.Status, game.Profile, game.Status, Updater.Staged, _update.NewerBuild, Profiles), ModRunning)
        : null;

    private SessionModel? CurrentSession() => _screen is ScreenKind.Hosting or ScreenKind.Lobby && GameOf(_app.Status) is { } game
        ? SessionScreens.Session(_app.Status, game.Profile, GameFolders.Find(game.Profile), game.Status, _watch.Condition, _events, Environment.TickCount64)
        : null;

    /// <summary>The fix button works where it is shown: on a home or session notice while the status offers a fix, on the joining
    /// screen while its fix is usable.</summary>
    private bool NoticeFixAllowed()
    {
        var status = _app.Status;
        if (SessionCards.FixFor(status, _update.NewerBuild) == NoticeFix.None) return false;
        if (_screen != ScreenKind.Joining || GameOf(status) is not { } game) return true;
        return SessionScreens.Joining(status, game.Profile, game.Status, _target, _watch.Condition, _update.NewerBuild, Environment.TickCount64).FixEnabled;
    }

    // ---------------------------------------------------------------- games and sessions

    /// <summary>The player picked a game: an open view closes, a refusal of the earlier game goes, the choice is remembered.</summary>
    private string? SelectFromRail(GameProfile game)
    {
        if (game.Id == _selectedId) return null;
        _selectedId = game.Id;
        _view = null;
        _app.ClearRefusal();
        AppSettings.Update(settings => settings.WithLastGame(game.Id));
        Apply(_app.Status);
        return null;
    }

    /// <summary>The home hero's main button: opens a lobby, or for a game that matches its players itself starts the game.</summary>
    private void HostOrLaunch()
    {
        if (SelectedGame?.Profile is not { } game) return;
        if (game.NativeSession)
        {
            if (GameFolders.Find(game) is { } gameDir) GameLauncher.LaunchWithOptions(game, gameDir, GameOptions.Current(game, gameDir));
            return;
        }
        _intent = Intent.Host;
        _target = null;
        _app.Host(game.Id);
        Apply(_app.Status);
    }

    /// <summary>Asks to join a lobby: the joining screen shows at once for this target.</summary>
    private void Join(JoinTarget target)
    {
        _intent = Intent.Join;
        _target = target;
        Log.Info($"Join requested for lobby {target.LobbyId}");
        _app.Join(target.LobbyId);
        Apply(_app.Status);
    }

    private void JoinTypedCode(string text)
    {
        if (!LobbyCode.TryParse(text, out var lobbyId))
        {
            PageEvent?.Invoke("codeRefused");
            return;
        }
        Join(new JoinTarget(lobbyId, string.Empty, 0));
    }

    private void JoinFriend(ulong lobbyId)
    {
        var friend = _app.Status.FriendLobbies.FirstOrDefault(candidate => candidate.LobbyId == lobbyId);
        Join(new JoinTarget(lobbyId, friend?.Name ?? string.Empty, friend?.FriendId ?? 0));
    }

    /// <summary>Cancel on the joining screen: the session is left, a refusal goes and the window returns to its tab.</summary>
    private void CancelSession()
    {
        _app.Leave();
        _app.ClearRefusal();
        _intent = Intent.None;
        _target = null;
        Apply(_app.Status);
    }

    /// <summary>Starts the game again for the open session; the session folder is kept, so it catches up as it joins.</summary>
    private void RelaunchGame()
    {
        if (GameOf(_app.Status)?.Profile is { } game) GameLauncher.Launch(game, GameFolders.Find(game));
    }

    /// <summary>Installs, updates, repairs or uninstalls a game's mod off the UI thread. Its steps reach the page as they happen (at
    /// most every 50 ms); <paramref name="afterInstalled"/> runs when an install succeeded.</summary>
    private void StartMod(GameProfile game, ModRunKind kind, Action? afterInstalled = null)
    {
        if (ModRunning || GameFolders.Find(game) is not { } gameDir) return;
        ShowModProgress(game.Id, kind, new ModProgress(ModStep.Checking, 0, 0, 0));
        var progress = new ThrottledProgress(new Progress<ModProgress>(report => ShowModProgress(game.Id, kind, report)), () => Environment.TickCount64);
        Task.Run(() => kind == ModRunKind.Install ? ModInstaller.Install(game, gameDir, progress) : ModInstaller.Uninstall(game, gameDir, progress))
            .ContinueWith(task => FinishMod(game.Id, kind, task, afterInstalled), _ui);
    }

    private void ShowModProgress(string gameId, ModRunKind kind, ModProgress progress)
    {
        _modRuns[gameId] = new ModRunView(kind, progress);
        _update.SetFree(Free && !ModRunning);
        Changed?.Invoke();
    }

    /// <summary>Brings every installed mod that reads Update up to the package while its game is closed (<see cref="ModAutoUpdate"/>), off
    /// the UI thread. Each game shows as an ordinary install; a failure stays with its reason and Retry. Called when the package on disk
    /// has settled: at start, and after an update that needs no restart.</summary>
    private void AutoUpdateMods()
    {
        if (ModRunning || !Free || _update.Busy) return;
        var sinks = Profiles.ToDictionary(game => game.Id, game => (IProgress<ModProgress>)new ThrottledProgress(
            new Progress<ModProgress>(report => ShowModProgress(game.Id, ModRunKind.Install, report)), () => Environment.TickCount64));
        Task.Run(() => ModAutoUpdate.Run(Profiles, GameFolders.Find, game => sinks[game.Id])).ContinueWith(_ => FinishAutoUpdate(), _ui);
    }

    private void FinishAutoUpdate()
    {
        foreach (var gameId in _modRuns.Where(run => run.Value.Progress.Step == ModStep.Done).Select(run => run.Key).ToList())
            Task.Delay(DoneShownMs).ContinueWith(_ => ClearDone(gameId), _ui);
        RefreshMods();
    }

    /// <summary>The mod action ended: a success shows Done for a moment, a failure stays with its reason and Retry (also when the
    /// action itself broke), and the mod states are read again.</summary>
    private void FinishMod(string gameId, ModRunKind kind, Task<bool> task, Action? afterInstalled)
    {
        if (task.IsFaulted)
        {
            var reason = task.Exception!.GetBaseException().Message;
            Log.Info($"Mod action failed: {reason}");
            ShowModProgress(gameId, kind, new ModProgress(ModStep.Failed, _modRuns[gameId].Progress.Percent, 0, 0, reason));
        }
        else if (task.Result)
        {
            if (kind == ModRunKind.Install) afterInstalled?.Invoke();
            Task.Delay(DoneShownMs).ContinueWith(_ => ClearDone(gameId), _ui);
        }
        RefreshMods();
    }

    private void ClearDone(string gameId)
    {
        if (_modRuns.GetValueOrDefault(gameId) is not { Progress.Step: ModStep.Done }) return;
        _modRuns.Remove(gameId);
        Changed?.Invoke();
    }

    /// <summary>The fix button of a notice or of the joining screen, for the refusal or the invite on show.</summary>
    private void RunNoticeFix()
    {
        var status = _app.Status;
        var game = _games.FirstOrDefault(g => g.Profile.Id == status.Refusal?.GameId)?.Profile ?? SelectedGame?.Profile;
        switch (SessionCards.FixFor(status, _update.NewerBuild))
        {
            case NoticeFix.InstallMod or NoticeFix.UpdateMod when game != null:
                StartMod(game, ModRunKind.Install, _app.ClearRefusal);
                break;
            case NoticeFix.UpdateLauncher:
                _update.Click();
                break;
            case NoticeFix.Browse when game != null:
                if (LauncherActions.BrowseFor(game)) RefreshMods();
                break;
            case NoticeFix.JoinAgain when status.Refusal is { } refusal:
                Join(_target is { } target && target.LobbyId == refusal.LobbyId ? target : new JoinTarget(refusal.LobbyId, string.Empty, 0));
                break;
            case NoticeFix.Switch when status.Invite is { } invite:
                _intent = Intent.Join;
                _target = new JoinTarget(invite.LobbyId, invite.FromName, invite.FromId);
                _app.SwitchToInvite();
                break;
            case NoticeFix.StartSteam:
                LauncherActions.StartSteam();
                break;
        }
    }

    // ---------------------------------------------------------------- views, popups and the mini window

    /// <summary>Opens the settings or the character select over the screens, or (null) closes the open one.</summary>
    private void ShowView(ScreenKind? view)
    {
        if (_view == view) return;
        _view = view;
        Apply(_app.Status);
    }

    private string? OpenView(UiCommand command)
    {
        if (_view != null) return Refused;
        if (command.Text("view") == "settings")
        {
            ShowView(ScreenKind.Settings);
            return null;
        }
        if (GameOf(_app.Status)?.Profile is not { HasCharacterSelect: true } game) return Refused;
        if (command.OptionalText("character") is { } character) FocusCharacter(game, character);
        ShowView(ScreenKind.Select);
        return null;
    }

    /// <summary>Puts the select's focus on the half that plays this character (named by its label or its value), when its outfit
    /// can be changed.</summary>
    private void FocusCharacter(GameProfile game, string character)
    {
        var value = GameOptions.CharacterValue(game, character) ?? character;
        var model = SelectModels.Build(game, GameFolders.Find(game), _app.Status, Free);
        for (var half = 0; half < SelectModel.Halves; half++)
            if (model[half].Figure.Character == value && model[half].CanCycle) _selectFocus = half;
    }

    private string? StepSelect(int half, int direction)
    {
        if (GameOf(_app.Status)?.Profile is not { HasCharacterSelect: true } game) return Refused;
        var shown = SelectModels.Build(game, GameFolders.Find(game), _app.Status, Free)[half];
        if (!shown.CanCycle) return Refused;
        OptionsModel.Save(game.Id, shown.Figure.OptionKey, SelectModels.Neighbor(shown, direction).Value);
        Apply(_app.Status);
        return null;
    }

    private string? SwapSelect()
    {
        if (GameOf(_app.Status)?.Profile is not { HasCharacterSelect: true } game) return Refused;
        var model = SelectModels.Build(game, GameFolders.Find(game), _app.Status, Free);
        if (model.Centre != StageCentre.Swap) return Refused;
        if (Free || _app.Status.LocalHosts) OptionsModel.Save(game.Id, GameOptions.CharacterKey, model.SwapCharacter);
        else _app.RequestSeatSwap(model.SwapCharacter);
        Apply(_app.Status);
        return null;
    }

    private void ToggleOptions()
    {
        if (_popup != PopupKind.None)
        {
            ClosePopup();
            return;
        }
        _popup = PopupKind.Options;
        Apply(_app.Status);
    }

    private void OpenLobbyPopup(ulong lobbyId)
    {
        _popup = PopupKind.Lobby;
        _popupLobbyId = lobbyId;
        Apply(_app.Status);
    }

    private void ClosePopup()
    {
        _popup = PopupKind.None;
        Apply(_app.Status);
    }

    /// <summary>The popup's own button or a lobby row's: join the lobby, or install the mod or update the launcher that it needs.</summary>
    private void RunLobbyAction(LobbyRowModel model)
    {
        switch (model.Action)
        {
            case LobbyAction.Join:
                _popup = PopupKind.None;
                Join(new JoinTarget(model.Listing.LobbyId, model.Listing.HostName, model.Listing.HostId));
                break;
            case LobbyAction.UpdateMod:
                StartMod(model.Game, ModRunKind.Install);
                break;
            case LobbyAction.UpdateLauncher:
                _update.Click();
                break;
        }
    }

    private string? SetLobbyGame(string? gameId)
    {
        if (gameId != null && _games.All(game => game.Profile.Id != gameId)) return UnknownTarget;
        _lobbyGameId = gameId;
        Apply(_app.Status);
        return null;
    }

    /// <summary>MINI MODE: the small always-on-top window opens and the main window hides while it shows.</summary>
    private void StartMini()
    {
        if (GameOf(_app.Status) == null) return;
        _miniOpen = true;
        Apply(_app.Status);
    }

    /// <summary>Expand (or Alt+F4 on the mini window): the mini window closes and the main window comes back.</summary>
    public void ExpandMini()
    {
        if (!_miniOpen) return;
        _miniOpen = false;
        Apply(_app.Status);
    }

    // ---------------------------------------------------------------- options and settings

    /// <summary>Saves an option the page chose: a session option of the selected game while its popup is open, or a settings option of
    /// any game in the settings view; the value must be one the option offers here.</summary>
    private string? SetOption(UiCommand command)
    {
        var settingsOptions = _screen == ScreenKind.Settings;
        if (!settingsOptions && !(_popup == PopupKind.Options && _screen == ScreenKind.Home)) return Refused;
        if (!Free || GameFor(command) is not { } game || (!settingsOptions && game.Profile.Id != _selectedId)) return UnknownTarget;
        var option = OptionsModel.OptionsOf(game.Profile, settingsOptions).FirstOrDefault(candidate => candidate.Key == command.Text("key"));
        if (option == null || OptionsModel.Values(option, GameFolders.Find(game.Profile)).All(value => value.Value != command.Text("value"))) return UnknownTarget;
        OptionsModel.Save(game.Profile.Id, option.Key, command.Text("value"));
        Apply(_app.Status);
        return null;
    }

    private string? RunGameCommand(UiCommand command)
    {
        if (GameFor(command) is not { } game) return UnknownTarget;
        var free = Free && !ModRunning;
        switch (command.Type)
        {
            case Cmd.GameBrowse when SettingsModel.BrowseEnabled(free):
                if (LauncherActions.BrowseFor(game.Profile)) RefreshMods();
                return null;
            case Cmd.GameResetFolder when SettingsModel.ResetEnabled(game.Profile, free):
                LauncherActions.ResetFolder(game.Profile);
                RefreshMods();
                return null;
            case Cmd.GameUninstall when SettingsModel.UninstallEnabled(game.Status, free, Updater.Staged):
                StartMod(game.Profile, ModRunKind.Uninstall);
                return null;
            case Cmd.GameArchive when SettingsModel.ArchiveShown(game.Profile):
                LauncherActions.OpenArchive(game.Profile);
                return null;
            default:
                return Refused;
        }
    }
}
