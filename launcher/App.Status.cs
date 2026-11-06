namespace CoopLauncher;

/// <summary>What the loop tells the window (the status) and the other players (the published step).</summary>
public sealed partial class App
{
    /// <summary>What a lobby's host options imply, worked out again only when the lobby or its options change.</summary>
    private sealed record LobbyView(
        ILobby Lobby, IReadOnlyDictionary<string, string> HostOptions, int Seats, IReadOnlyList<string> SeatCharacters);

    private const int MillisPerSecond = 1000;

    private IReadOnlyList<FriendLobby> _friendRows = [];
    private IReadOnlyList<FriendLobby> _friends = [];
    private IReadOnlyList<LobbyListing> _lobbies = [];
    private long _nextFriendPollMs;
    private LobbyView? _view;
    private ILobby? _publishedTo;
    private string _publishedStep = string.Empty;
    private ILobby? _optionsPublishedTo;
    private AppSettings? _optionsPublishedFrom;
    private string _publishedOptions = string.Empty;
    private string? _optionsPublishedSwap;
    private ILobby? _lobbyDataPublishedTo;
    private readonly Dictionary<LobbyKey, string> _publishedLobbyData = new();
    private long _hostStartedUnix;
    private ILobby? _timedLobby;
    private long? _lobbyOpenedAtMs;
    private SessionStep _timedStep;
    private long _stepSinceMs;
    private LobbyListing? _lastListing;
    private GameState? _gameState;
    private long _startBlockedCheckedMs;
    private string _startBlocked = string.Empty;
    private AppSettings? _labelSettings;
    private CaptureView? _labelCapture;
    private string _label = string.Empty;

    private void PublishStatus()
    {
        var progress = LocalProgress();
        PublishStep(progress);
        PublishOptions();
        TrackTimes(progress.Step);
        var status = CurrentStatus(progress);
        ServeSwaps(status);
        PublishLobbyData(status);
        PublishLobbyState(status);
        if (status == Status) return;
        Status = status;
        _avatars?.Want(AvatarIds(status));
        StatusChanged?.Invoke(status);
    }

    /// <summary>The users whose avatars the window shows: this player, the lobby's members, the friends hosting.</summary>
    private IEnumerable<ulong> AvatarIds(AppStatus status) =>
        status.Players.Select(player => player.Id).Concat(status.FriendLobbies.Select(friend => friend.FriendId)).Append(_transport!.LocalId);

    private StepReport LocalProgress()
    {
        var step = SessionProgress.Step(new StepInputs(
            LobbyReady: _lobby is { IsReady: true }, SessionStarted: _sessionStarted, Refused: _refusal != null && _lobby != null,
            BuildCheckPending: _buildCheck is { Ready: false }, SaveSyncPending: _saveSync is { Ready: false }, LaunchPending: _launchPending,
            Bridged: _bridge != null, BridgeReady: _bridge?.IsReady == true, BridgeWasReady: _bridgeWasReady,
            StartHeld: _launchPending && StartGateClosed(), ReadingCharacter: _capture is { InProgress: true },
            PickingCharacter: MustPickCharacter(), CharacterFailed: _capture is { View.State: CaptureState.Failed } && !_startRequested));
        return new StepReport(step, _saveMeter.Percent, _refusal?.Kind, ReportDetail(step), LocalLabel(), _gameState);
    }

    /// <summary>Why the host cannot start with guests yet: its campaign holds no saved world for them to receive (looked at once a second).
    /// Alone the host can start it, which is how a new campaign gets its first save.</summary>
    private string StartBlocked()
    {
        if (_session is not { IsHost: true, PeerIds.Count: > 0 } || _profile == null || _gameDir == null || !_launchPending) return string.Empty;
        var now = Environment.TickCount64;
        if (now - _startBlockedCheckedMs < MillisPerSecond) return _startBlocked;
        _startBlockedCheckedMs = now;
        return _startBlocked = OptionSources.CampaignHasWorld(_profile, _gameDir) ? string.Empty : OptionSources.NoWorldWords;
    }

    /// <summary>A guest of a game where it brings a character, held at the start gate, that has not pressed READY.</summary>
    private bool MustPickCharacter() =>
        _launchPending && _startHeld && _profile != null && _session?.IsHost == false && OptionSources.GuestPicksCharacter(_profile) && !_startRequested;

    /// <summary>What the published step says besides its name: the refusal's detail, or why the character could not be read.</summary>
    private string ReportDetail(SessionStep step) => step switch
    {
        SessionStep.Refused => _refusal?.Detail ?? string.Empty,
        SessionStep.CharacterNotRead => _capture?.View.Reason ?? string.Empty,
        _ => string.Empty,
    };

    /// <summary>The host's campaign or the guest's character as the others are told it, worked out again only when the settings were
    /// replaced or the capture changed (the campaign folders are read to name them).</summary>
    private string LocalLabel()
    {
        if (_profile == null || _gameDir == null) return string.Empty;
        var settings = AppSettings.Current;
        var capture = _capture?.View;
        if (ReferenceEquals(settings, _labelSettings) && capture == _labelCapture) return _label;
        _labelSettings = settings;
        _labelCapture = capture;
        return _label = _session?.IsHost == true ? OptionSources.CampaignLabel(_profile, _gameDir) : OptionSources.PickedCharacterLabel(_profile, _gameDir);
    }

    /// <summary>Tells the lobby this player's step when it changed (never every tick).</summary>
    private void PublishStep(StepReport progress)
    {
        if (_lobby is not { IsReady: true } lobby) return;
        var text = StepCodec.Encode(progress);
        if (ReferenceEquals(lobby, _publishedTo) && text == _publishedStep) return;
        _publishedTo = lobby;
        _publishedStep = text;
        lobby.SetMemberData(MemberKey.Step, text);
    }

    /// <summary>Tells the lobby this player's own option values (the outfits it chose, and a pending swap request) when the lobby or
    /// the text changed; the text is worked out again only when the settings were replaced or the request changed (never every tick).</summary>
    private void PublishOptions()
    {
        if (_lobby is not { IsReady: true } lobby || _profile == null) return;
        var settings = AppSettings.Current;
        if (ReferenceEquals(lobby, _optionsPublishedTo) && ReferenceEquals(settings, _optionsPublishedFrom) && _swapWanted == _optionsPublishedSwap) return;
        var own = GameOptions.OwnValues(_profile, _gameDir);
        if (_swapWanted != null) own[GameOptions.SwapRequestKey] = _swapWanted;
        var text = GameOptions.Encode(own);
        var sameTarget = ReferenceEquals(lobby, _optionsPublishedTo);
        _optionsPublishedTo = lobby;
        _optionsPublishedFrom = settings;
        _optionsPublishedSwap = _swapWanted;
        if (sameTarget && text == _publishedOptions) return;
        _publishedOptions = text;
        lobby.SetMemberData(MemberKey.Options, text);
    }

    /// <summary>The host's lobby text that changes while it lives, written when it changes: who is in and at which step (the
    /// Lobbies tab's roster) and the start of play (the first time the host's own game is in).</summary>
    private void PublishLobbyData(AppStatus status)
    {
        if (_lobby is not { IsReady: true } lobby || _hostId != _transport!.LocalId) return;
        PublishHostChoices(lobby);
        if (_hostStartedUnix == 0 && status.Step == SessionStep.InGame) _hostStartedUnix = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        PublishLobbyValue(lobby, LobbyKey.Roster, RosterCodec.Encode(status.Players.Select(player => new RosterEntry(player.Name, player.Step))));
        if (_hostStartedUnix != 0) PublishLobbyValue(lobby, LobbyKey.Started, _hostStartedUnix.ToString());
    }

    private void PublishLobbyValue(ILobby lobby, LobbyKey key, string text)
    {
        if (!ReferenceEquals(lobby, _lobbyDataPublishedTo))
        {
            _lobbyDataPublishedTo = lobby;
            _publishedLobbyData.Clear();
        }
        if (_publishedLobbyData.TryGetValue(key, out var published) && published == text) return;
        _publishedLobbyData[key] = text;
        lobby.SetLobbyData(key, text);
    }

    /// <summary>Stamps the moments the window counts from: when the open lobby became ready and when the local step last changed.</summary>
    private void TrackTimes(SessionStep step)
    {
        var now = Environment.TickCount64;
        if (_lobby is { IsReady: true } lobby)
        {
            if (!ReferenceEquals(lobby, _timedLobby))
            {
                _timedLobby = lobby;
                _lobbyOpenedAtMs = now;
            }
            _lastListing = lobby.Listing;
        }
        if (step == _timedStep) return;
        _timedStep = step;
        _stepSinceMs = now;
    }

    private AppStatus CurrentStatus(StepReport progress)
    {
        var now = Environment.TickCount64;
        if (_lobby == null)
            return ReconnectStatus(now) ?? AppStatus.Idle with
            {
                Refusal = _refusal, FriendLobbies = _friends, Lobbies = _lobbies, LobbiesSearching = _directory?.Searching ?? false,
            };
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
            Refusal = _refusal, StartBlocked = StartBlocked(), ConnectingToHost = _buildCheck is { Connecting: true }, Step = progress.Step, SavePercent = progress.Percent, Seats = view.Seats, HostOptions = view.HostOptions,
            LaunchedAtMs = _launchedAtMs, Invite = _invite, SaveFile = _saveMeter.CurrentFile, SaveBytes = _saveMeter.ReceivedBytes,
            SaveTotal = _saveMeter.TotalBytes, LobbyOpenedAtMs = _lobbyOpenedAtMs, StepSinceMs = StepSince(progress.Step),
            SessionStartedUnix = _lastListing?.StartedUnix ?? 0, Listing = _lastListing, StartRequested = _startRequested,
            Capture = _capture?.View,
        };
    }

    private long? StepSince(SessionStep step) => step == SessionStep.None ? null : _stepSinceMs;

    /// <summary>Rejoining, or waiting for the host, while no lobby is ready; null when neither is going on.</summary>
    private AppStatus? ReconnectStatus(long nowMs)
    {
        var common = new AppStatus(AppState.Idle)
        {
            GameId = _profile?.Id, Bridged = _bridge != null, LaunchedAtMs = _launchedAtMs, Invite = _invite,
            Seats = _view?.Seats ?? 0, HostOptions = _view?.HostOptions ?? new Dictionary<string, string>(),
            LobbyOpenedAtMs = _lobbyOpenedAtMs, SessionStartedUnix = _lastListing?.StartedUnix ?? 0, Listing = _lastListing,
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
    private List<PlayerSlot> PlayerSlots(ILobby lobby, StepReport local, long nowMs)
    {
        var hostId = _hostId != 0 ? _hostId : lobby.OwnerId;
        return lobby.Members
            .Where(member => _session?.IsKicked(member.Id) != true)
            .OrderByDescending(member => member.Id == hostId)
            .Select(member =>
            {
                var isLocal = member.Id == _transport!.LocalId;
                var report = isLocal ? local : PublishedStep(lobby, member);
                var transferring = Transferring(report.Step) || Transferring(local.Step);
                return new PlayerSlot(
                    member.Id, member.Name, member.Id == hostId, isLocal, NativeJoin.Readable(lobby.MemberData(member, MemberKey.GameStatus)),
                    report.Step, report.Percent, isLocal ? null : MeasuredRtt(member.Id, report.Step, local.Step), _session?.IsHeld(member.Id) ?? false,
                    !transferring && (_session?.IsSilent(member.Id, nowMs) ?? false), lobby.MemberData(member, MemberKey.Options),
                    HoldSecondsLeft(member.Id, nowMs), report.Refusal, !isLocal && (_session?.IsConnecting(member.Id) ?? false), report.Detail, report.Label, report.Game);
            })
            .ToList();
    }

    /// <summary>A member's ping, none while it or this player is still checking the build or moving the save: those pongs wait
    /// behind the transfer and read as seconds, so the average starts again once the transfer is over.</summary>
    private int? MeasuredRtt(ulong memberId, SessionStep memberStep, SessionStep localStep)
    {
        if (!Transferring(memberStep) && !Transferring(localStep)) return _session?.RttOf(memberId);
        _session?.RestartRtt(memberId);
        return null;
    }

    private static bool Transferring(SessionStep step) => step is SessionStep.CheckingBuild or SessionStep.ReceivingSave;

    private int HoldSecondsLeft(ulong memberId, long nowMs) =>
        _session is { } session ? (int)Math.Ceiling(session.HoldMsLeft(memberId, nowMs) / (double)MillisPerSecond) : 0;

    private static StepReport PublishedStep(ILobby lobby, LobbyMember member) =>
        StepCodec.TryDecode(lobby.MemberData(member, MemberKey.Step), out var report) ? report : StepReport.None;

    /// <summary>Every other slot of the session is a member in its game (its published step) or, from a launcher that publishes
    /// none, one that answers pings, and none is held (missing from the lobby) or silent.</summary>
    private bool PartnersInGame(IReadOnlyList<PlayerSlot> players) =>
        _session is { PeerConnected: true } session
        && session.PeerIds.All(id => players.FirstOrDefault(player => player.Id == id) is { Held: false, Silent: false } partner
            && (InWorld(partner) || (partner.Step == SessionStep.None && partner.RttMs != null)));

    /// <summary>In the world: what its adapter says when it says anything, else that its launcher published InGame.</summary>
    private static bool InWorld(PlayerSlot player) =>
        player.Game is { Place: not GamePlace.Unknown } game ? game.Place == GamePlace.World : player.Step == SessionStep.InGame;
}
