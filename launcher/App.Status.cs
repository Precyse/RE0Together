namespace CoopLauncher;

/// <summary>What the loop tells the window (the status) and the other players (the published step).</summary>
public sealed partial class App
{
    /// <summary>What a lobby's host options imply, worked out again only when the lobby or its options change.</summary>
    private sealed record LobbyView(
        ILobby Lobby, IReadOnlyDictionary<string, string> HostOptions, int Seats, IReadOnlyList<string> SeatCharacters);

    private IReadOnlyList<FriendLobby> _friends = [];
    private long _nextFriendPollMs;
    private LobbyView? _view;
    private ILobby? _publishedTo;
    private string _publishedStep = string.Empty;
    private ILobby? _optionsPublishedTo;
    private AppSettings? _optionsPublishedFrom;
    private string _publishedOptions = string.Empty;

    private void PublishStatus()
    {
        var progress = LocalProgress();
        PublishStep(progress);
        PublishOptions();
        var status = CurrentStatus(progress);
        if (status == Status) return;
        Status = status;
        _avatars?.Want(AvatarIds(status));
        StatusChanged?.Invoke(status);
    }

    /// <summary>The users whose avatars the window shows: this player, the lobby's members, the friends hosting.</summary>
    private IEnumerable<ulong> AvatarIds(AppStatus status) =>
        status.Players.Select(player => player.Id).Concat(status.FriendLobbies.Select(friend => friend.FriendId)).Append(_transport!.LocalId);

    private (SessionStep Step, int Percent) LocalProgress()
    {
        var step = SessionProgress.Step(new StepInputs(
            LobbyReady: _lobby is { IsReady: true }, SessionStarted: _sessionStarted, Refused: _refusal != null && _lobby != null,
            BuildCheckPending: _buildCheck is { Ready: false }, SaveSyncPending: _saveSync is { Ready: false }, LaunchPending: _launchPending,
            Bridged: _bridge != null, BridgeReady: _bridge?.IsReady == true, BridgeWasReady: _bridgeWasReady));
        return (step, _saveMeter.Percent);
    }

    /// <summary>Tells the lobby this player's step when it changed (never every tick).</summary>
    private void PublishStep((SessionStep Step, int Percent) progress)
    {
        if (_lobby is not { IsReady: true } lobby) return;
        var text = StepCodec.Encode(progress.Step, progress.Percent, _refusal?.Kind);
        if (ReferenceEquals(lobby, _publishedTo) && text == _publishedStep) return;
        _publishedTo = lobby;
        _publishedStep = text;
        lobby.SetMemberData(MemberKey.Step, text);
    }

    /// <summary>Tells the lobby this player's own option values (the outfits it chose) when the lobby or the text changed; the
    /// text is worked out again only when the settings were replaced (never every tick).</summary>
    private void PublishOptions()
    {
        if (_lobby is not { IsReady: true } lobby || _profile == null) return;
        var settings = AppSettings.Current;
        if (ReferenceEquals(lobby, _optionsPublishedTo) && ReferenceEquals(settings, _optionsPublishedFrom)) return;
        var text = GameOptions.Encode(GameOptions.OwnValues(_profile, _gameDir));
        var sameTarget = ReferenceEquals(lobby, _optionsPublishedTo);
        _optionsPublishedTo = lobby;
        _optionsPublishedFrom = settings;
        if (sameTarget && text == _publishedOptions) return;
        _publishedOptions = text;
        lobby.SetMemberData(MemberKey.Options, text);
    }

    private AppStatus CurrentStatus((SessionStep Step, int Percent) progress)
    {
        var now = Environment.TickCount64;
        if (_lobby == null) return ReconnectStatus(now) ?? AppStatus.Idle with { Refusal = _refusal, FriendLobbies = _friends };
        if (!_lobby.IsReady) return ReconnectStatus(now) ?? new AppStatus(AppState.Connecting) { Invite = _invite };
        var view = ViewOf(_lobby);
        var players = PlayerSlots(_lobby, progress, now);
        var state = PartnersInGame(players) ? AppState.PeerConnected
            : _bridge?.IsReady == true ? AppState.GameRunning
            : _hostId == _transport!.LocalId ? AppState.Hosting
            : AppState.Joined;
        return new AppStatus(state, _lobby.Id, _session?.RttMs)
        {
            Players = players, GameId = _lobby.GameId, SeatCharacters = view.SeatCharacters, Bridged = _bridge != null,
            Refusal = _refusal, Step = progress.Step, SavePercent = progress.Percent, Seats = view.Seats, HostOptions = view.HostOptions,
            LaunchedAtMs = _launchedAtMs, Invite = _invite,
        };
    }

    /// <summary>Rejoining, or waiting for the host, while no lobby is ready; null when neither is going on.</summary>
    private AppStatus? ReconnectStatus(long nowMs)
    {
        var common = new AppStatus(AppState.Idle)
        {
            GameId = _profile?.Id, Bridged = _bridge != null, LaunchedAtMs = _launchedAtMs, Invite = _invite,
            Seats = _view?.Seats ?? 0, HostOptions = _view?.HostOptions ?? new Dictionary<string, string>(),
        };
        if (_rejoin is { } rejoin)
            return common with
            {
                State = AppState.Rejoining, LobbyId = rejoin.LobbyId, GameId = rejoin.GameId ?? common.GameId,
                RejoinAttempt = rejoin.Schedule.Attempts, SecondsLeft = rejoin.Schedule.SecondsLeft(nowMs),
            };
        if (_reopen is { } plan)
            return common with
            {
                State = AppState.Rejoining, LobbyId = plan.LobbyId, RejoinAttempt = plan.Schedule.Attempts, SecondsLeft = plan.Schedule.SecondsLeft(nowMs),
            };
        return _hostWait is { } wait
            ? common with { State = AppState.WaitingForHost, LobbyId = wait.LobbyId, SecondsLeft = wait.Schedule.SecondsLeft(nowMs) }
            : null;
    }

    /// <summary>The seat count and characters of the lobby's host options, recomputed when the lobby object or its options
    /// (the lobby decodes them again only when they change) are not the ones last worked out.</summary>
    private LobbyView ViewOf(ILobby lobby)
    {
        var options = lobby.HostOptions;
        if (_view is { } view && ReferenceEquals(view.Lobby, lobby) && ReferenceEquals(view.HostOptions, options)) return view;
        var profile = GameProfile.Load(lobby.GameId!);
        return _view = new LobbyView(lobby, options, GameOptions.Seats(profile, options), GameOptions.SeatCharacters(profile, options));
    }

    /// <summary>Members host first, each with what its game says, the step its launcher published (this launcher's own for the
    /// local seat) and how the session sees it.</summary>
    private List<PlayerSlot> PlayerSlots(ILobby lobby, (SessionStep Step, int Percent) local, long nowMs)
    {
        var hostId = _hostId != 0 ? _hostId : lobby.OwnerId;
        return lobby.Members
            .OrderByDescending(member => member.Id == hostId)
            .Select(member =>
            {
                var isLocal = member.Id == _transport!.LocalId;
                var (step, percent) = isLocal ? local : PublishedStep(lobby, member);
                return new PlayerSlot(
                    member.Id, member.Name, member.Id == hostId, isLocal, NativeJoin.Readable(lobby.MemberData(member, MemberKey.GameStatus)),
                    step, percent, _session?.RttOf(member.Id), _session?.IsHeld(member.Id) ?? false, _session?.IsSilent(member.Id, nowMs) ?? false,
                    lobby.MemberData(member, MemberKey.Options));
            })
            .ToList();
    }

    private static (SessionStep Step, int Percent) PublishedStep(ILobby lobby, LobbyMember member) =>
        StepCodec.TryDecode(lobby.MemberData(member, MemberKey.Step), out var step, out var percent, out _) ? (step, percent) : (SessionStep.None, 0);

    /// <summary>Every other slot of the session is a member in its game (its published step) or, from a launcher that publishes
    /// none, one that answers pings, and none is held (missing from the lobby) or silent.</summary>
    private bool PartnersInGame(IReadOnlyList<PlayerSlot> players) =>
        _session is { PeerConnected: true } session
        && session.PeerIds.All(id => players.FirstOrDefault(player => player.Id == id) is { Held: false, Silent: false } partner
            && (partner.Step == SessionStep.InGame || (partner.Step == SessionStep.None && partner.RttMs != null)));
}
