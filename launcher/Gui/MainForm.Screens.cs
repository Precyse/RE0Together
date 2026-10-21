namespace CoopLauncher.Gui;

/// <summary>The window's screens: which one the status asks for and filling it, the settings and select views, the options and lobby
/// popups, the lobby browser's rows and the mini window.</summary>
public sealed partial class MainForm
{
    // ---------------------------------------------------------------- showing a status

    private void Apply(AppStatus status)
    {
        _applying = true;
        try
        {
            if (status.GameId is { } lobbyGame) Rail.SelectGame(lobbyGame);
            var free = status.State is AppState.Idle or AppState.Offline;
            var kind = ScreenFor(status);
            if (_sessionOpen && free && kind != ScreenKind.Joining)
            {
                _intent = Intent.None;
                _target = null;
                kind = ScreenFor(status);
            }
            Rail.Enabled = free;
            Rail.SetRunning(status.State is AppState.GameRunning or AppState.PeerConnected ? status.GameId : null);
            _settings.ShowMods(Rail.StatusOf, free, Updater.Staged);
            _update.SetFree(free);
            _top.ShowUpdate(_update.Label, _update.Enabled);
            if (_sessionOpen && free) RefreshMods();
            _sessionOpen = !free;
            if (GameOf(status) is { } game) ApplyGame(status, game, free, kind);
            ApplyOverlays(status, free, kind);
        }
        finally
        {
            _applying = false;
        }
    }

    /// <summary>Which screen shows: the open view, else what the status asks for.</summary>
    private ScreenKind ScreenFor(AppStatus status)
    {
        if (_view == ScreenKind.Select && Rail.Selected?.HasCharacterSelect != true) _view = null;
        return _view ?? Screens.For(status, _intent, _target, _tab);
    }

    /// <summary>The game a screen is about: the open lobby's (or the refused lobby's), else the one selected in the rail.</summary>
    private GameProfile? GameOf(AppStatus status) =>
        _games.FirstOrDefault(game => game.Id == (status.GameId ?? status.Refusal?.GameId)) ?? Rail.Selected;

    private void ApplyGame(AppStatus status, GameProfile game, bool free, ScreenKind kind)
    {
        var gameDir = GameFolders.Find(game);
        var art = Rail.ArtOf(game.Id) ?? Rail.SelectedArt!;
        _select.Backdrop = art.Hero;
        if (game.HasCharacterSelect) _select.Show(game, gameDir, status, free);
        _top.Show(kind is ScreenKind.Home or ScreenKind.Lobbies ? TopBarMode.Browse : _view != null ? TopBarMode.View : TopBarMode.Session, _buildLabel);
        _top.Tabs.SelectedIndex = (int)_tab;
        _drawer.Visible = kind is ScreenKind.Home or ScreenKind.Lobbies;
        _host.Show(kind, () => ShowScreen(kind, status, game, gameDir, art));
    }

    private void ShowScreen(ScreenKind kind, AppStatus status, GameProfile game, string? gameDir, SteamArt art)
    {
        var mod = Rail.StatusOf(game.Id);
        var now = Environment.TickCount64;
        switch (kind)
        {
            case ScreenKind.Home when Rail.Selected is { } selected:
                _home.Show(SessionCards.Home(status, selected, Rail.SelectedStatus, Updater.Staged, _update.NewerBuild, _games), selected, Rail.SelectedArt!, OptionsModel.Has(selected));
                break;
            case ScreenKind.Lobbies:
                ShowLobbies(status);
                break;
            case ScreenKind.Joining:
                _joining.Show(SessionScreens.Joining(status, game, mod, _target, _watch.Condition, _update.NewerBuild, now), art);
                break;
            case ScreenKind.Hosting or ScreenKind.Lobby:
                _session.Show(SessionScreens.Session(status, game, gameDir, mod, _watch.Condition, _events, now), art);
                break;
        }
    }

    /// <summary>What follows from the status and the screen: the lobby search runs while the Lobbies tab shows, the clock ticks while
    /// a session screen or a lobby popup needs it, a popup of another screen closes, the mini window follows the session.</summary>
    private void ApplyOverlays(AppStatus status, bool free, ScreenKind kind)
    {
        var lobbiesShown = kind == ScreenKind.Lobbies;
        if (_watchingLobbies != lobbiesShown)
        {
            _watchingLobbies = lobbiesShown;
            _app.WatchLobbies(lobbiesShown);
        }
        _watchTimer.Enabled = !free || _modal is { IsOpen: true, Panel: LobbyPopup };
        if (_modal.IsOpen && ModalScreen != kind) CloseModal();
        if (_mini == null) return;
        if (free || status.Invite != null) ExpandMini();
        else if (GameOf(status) is { } game)
            _mini.Show(SessionScreens.Mini(status, game, GameFolders.Find(game), _events), Rail.ArtOf(game.Id)!, this);
    }

    // ---------------------------------------------------------------- views

    /// <summary>Opens the settings or the character select over the screens, or (null) closes the open one.</summary>
    private void ShowView(ScreenKind? view)
    {
        if (_view == view) return;
        _view = view;
        Apply(_app.Status);
    }

    /// <summary>Esc closes the popup, else the open view; true when it closed something.</summary>
    private bool CloseOnEscape()
    {
        if (_modal.IsOpen)
        {
            CloseModal();
            return true;
        }
        if (_view == null) return false;
        ShowView(null);
        return true;
    }

    // ---------------------------------------------------------------- popups

    /// <summary>The screen the open popup belongs to: the options on the home screen, the lobby on the Lobbies tab.</summary>
    private ScreenKind ModalScreen => _modal.Panel is OptionsPopup ? ScreenKind.Home : ScreenKind.Lobbies;

    private void ToggleOptions()
    {
        if (_modal.IsOpen)
        {
            CloseModal();
            return;
        }
        if (Rail.Selected is not { } game) return;
        var status = _app.Status;
        var free = status.State is AppState.Idle or AppState.Offline;
        var popup = new OptionsPopup(game, GameFolders.Find(game), free ? null : status.HostOptions, !free, Rail.SelectedArt!);
        popup.Changed += () => Apply(_app.Status);
        popup.CloseClicked += CloseModal;
        OpenModal(popup);
        _home.OptionsOpen = true;
    }

    private void OpenLobbyPopup(LobbyRowModel model)
    {
        if (_modal.IsOpen) return;
        var popup = new LobbyPopup(model, Rail.StatusOf(model.Game.Id), Rail.ArtOf(model.Game.Id)!, DateTimeOffset.UtcNow.ToUnixTimeSeconds());
        popup.ActionClicked += RunLobbyAction;
        popup.CloseClicked += CloseModal;
        OpenModal(popup);
        _watchTimer.Enabled = true;
    }

    /// <summary>The popup's own button or a lobby row's: join the lobby, or install the mod or update the launcher that it needs.</summary>
    private void RunLobbyAction(LobbyRowModel model)
    {
        switch (model.Action)
        {
            case LobbyAction.Join:
                CloseModal();
                Join(new JoinTarget(model.Listing.LobbyId, model.Listing.HostName, model.Listing.HostId));
                break;
            case LobbyAction.UpdateMod:
                InstallMod(model.Game);
                break;
            case LobbyAction.UpdateLauncher:
                _update.Click();
                break;
        }
    }

    private void OpenModal(Control panel)
    {
        var area = new Rectangle(0, _top.Height, ClientSize.Width, ClientSize.Height - _top.Height);
        _modal.Bounds = area;
        var below = new Bitmap(area.Width, area.Height);
        using (var g = Graphics.FromImage(below))
            foreach (var part in new Control[] { _host, _drawer })
            {
                using var shot = MotionOverlay.Capture(part);
                g.DrawImageUnscaled(shot, part.Left - area.X, part.Top - area.Y);
            }
        _modal.Open(panel, below);
    }

    private void CloseModal()
    {
        _home.OptionsOpen = false;
        _modal.Close();
        _watchTimer.Enabled = _app.Status.State is not (AppState.Idle or AppState.Offline);
    }

    // ---------------------------------------------------------------- lobbies

    private void ShowLobbies(AppStatus status)
    {
        _lobbyRows = LobbyRowModels.Of(status, _lobbies.Filter, _lobbies.GameId, _games, Rail.StatusOf, BuildCheck.LocalBuild(), _update.NewerBuild);
        _lobbies.Show(_lobbyRows, status.LobbiesSearching);
        if (_modal.Panel is not LobbyPopup popup) return;
        var shown = _lobbyRows.FirstOrDefault(row => row.Listing.LobbyId == popup.Listing.LobbyId);
        if (shown != null) popup.Show(shown, Rail.StatusOf(shown.Game.Id), DateTimeOffset.UtcNow.ToUnixTimeSeconds());
    }

    // ---------------------------------------------------------------- mini mode

    /// <summary>MINI MODE: the small always-on-top window opens and the main window hides while it shows.</summary>
    private void StartMini()
    {
        var status = _app.Status;
        if (_mini != null || GameOf(status) is not { } game) return;
        _mini = new MiniWindow(_avatars, _art);
        _mini.ExpandClicked += ExpandMini;
        _mini.LeaveClicked += _app.Leave;
        _mini.PlaySoloClicked += _app.PlaySolo;
        _mini.Show(SessionScreens.Mini(status, game, GameFolders.Find(game), _events), Rail.ArtOf(game.Id)!, this);
        Hide();
    }

    /// <summary>Expand (or Alt+F4 on the mini window, the session ending, an invite arriving): the mini window closes and the main
    /// window comes back.</summary>
    private void ExpandMini()
    {
        if (_mini is not { } mini) return;
        _mini = null;
        mini.Close(() =>
        {
            Show();
            Raise();
            mini.Dispose();
        });
    }
}
