namespace CoopLauncher;

/// <summary>The paths on which a session outlives its lobby: the lobby connection is lost and rejoined (guest), the host
/// left and its next lobby is awaited (guest), or the host's own lobby failed and a new one is opened (host). The session,
/// its game link and the running game are kept while it happens; <see cref="Session.Hold"/> keeps every slot.</summary>
public sealed partial class App
{
    /// <summary>The guest waits for the host's next lobby, for at most <see cref="MemberGrace.HostGraceMs"/>.</summary>
    private sealed record HostWait(ulong LobbyId, string? GameId, RetrySchedule Schedule);

    /// <summary>The host opens a new lobby for the same game and options until one is ready, for at most <see cref="MemberGrace.HostGraceMs"/>.</summary>
    private sealed record ReopenPlan(string GameId, IReadOnlyDictionary<string, string> Options, ulong LobbyId, RetrySchedule Schedule);

    private const long HostMissingConfirmMs = 2000;

    private Rejoin? _rejoin;
    private HostWait? _hostWait;
    private ReopenPlan? _reopen;
    private long? _hostMissingSinceMs;

    private bool Reconnecting => _rejoin != null || _hostWait != null || _reopen != null;

    /// <summary>Nothing is open and nothing is being rebuilt.</summary>
    private bool IsIdle => _lobby == null && !Reconnecting;

    private void CancelReconnect()
    {
        _rejoin = null;
        _hostWait = null;
        _reopen = null;
    }

    /// <summary>A started session whose lobby failed keeps its game link and services while the lobby is rebuilt (a guest
    /// rejoins it, a host opens a new one). A lobby that fails during an attempt just ends that attempt. False when there is no
    /// session to hold.</summary>
    private bool HoldSession(LobbyFailure failure)
    {
        if (_session is not { Ended: false } session || !session.Hold()) return false;
        Log.Info($"Lobby failure: {failure.Message}, keeping the session");
        var options = _view?.HostOptions ?? GameOptions.HostValues(_profile!, _gameDir);
        DisposeLobby();
        if (Reconnecting) return true;
        if (session.IsHost)
            _reopen = new ReopenPlan(_profile!.Id, options, session.LobbyId, NewSchedule());
        else
            _rejoin = new Rejoin(session.LobbyId) { GameId = _profile!.Id };
        return true;
    }

    private static RetrySchedule NewSchedule() => new(Environment.TickCount64, Rejoin.RetryIntervalMs, MemberGrace.HostGraceMs);

    /// <summary>A held session whose grace ran out is gone: it is dropped while the rejoin runs, so the rejoined lobby starts fresh.</summary>
    private void DropEndedHeldSession()
    {
        if (_session is { Ended: true } && _rejoin != null) DropSession();
    }

    /// <summary>The new lobby replaces the failed one in a held session when its owner is the session's host (the same lobby
    /// rejoined, or the host's new lobby); a lobby with another owner drops the session and the lobby is started over.</summary>
    private void RejoinHeldSession()
    {
        if (_session is not { Held: true } session || _lobby is not { IsReady: true } lobby) return;
        if (lobby.OwnerId != session.HostId)
        {
            DropSession();
            return;
        }
        session.Rebind(lobby);
        _nativeJoin?.Rebind(lobby);
        CancelReconnect();
        Log.Event("Rejoined the session");
        _follow = NewHostFollow();
    }

    private HostFollow? NewHostFollow() =>
        _steam != null && _lobby is { } lobby && lobby.OwnerId != _transport!.LocalId ? new HostFollow(lobby.OwnerId, _profile!.Id) : null;

    /// <summary>A guest's host is gone from the lobby (for <see cref="HostMissingConfirmMs"/>, so a member list that blinks does
    /// not send the guest away): the session and the game stay, the old lobby is left, and the host's next lobby is awaited
    /// (<see cref="WaitForHost"/>).</summary>
    private void WatchHost()
    {
        if (Reconnecting || _lobby == null || _session is not { HostMissing: true } session)
        {
            _hostMissingSinceMs = null;
            return;
        }
        var now = Environment.TickCount64;
        _hostMissingSinceMs ??= now;
        if (now - _hostMissingSinceMs < HostMissingConfirmMs) return;
        _hostMissingSinceMs = null;
        Log.Event("Waiting for the host");
        session.Hold();
        _hostWait = new HostWait(session.LobbyId, _profile!.Id, NewSchedule());
        DisposeLobby();
    }

    /// <summary>Joins the lobby the host publishes for the game; gives up after the grace.</summary>
    private void WaitForHost()
    {
        if (_hostWait is not { } wait) return;
        if (_session is not { Ended: false } || wait.Schedule.GaveUp(Environment.TickCount64))
        {
            Log.Info("The host did not come back");
            Refuse(new Refusal(RefusalKind.HostLeft, "", wait.GameId, wait.LobbyId));
            CancelReconnect();
            EndSession();
            return;
        }
        FollowHostTo(wait.LobbyId);
    }

    /// <summary>The held host opens a new lobby every retry interval until one is ready (<see cref="RejoinHeldSession"/> rebinds
    /// the session to it); gives up after the grace.</summary>
    private void ReopenLobby()
    {
        if (_reopen is not { } plan) return;
        var now = Environment.TickCount64;
        if (plan.Schedule.GaveUp(now))
        {
            Log.Info("Could not open a new lobby, giving up");
            Refuse(new Refusal(RefusalKind.SteamOffline, "", plan.GameId, plan.LobbyId));
            CancelReconnect();
            EndSession();
            return;
        }
        if (_lobby != null || !plan.Schedule.AttemptDue(now)) return;
        Log.Info($"Opening a new lobby (attempt {plan.Schedule.Attempts})");
        var profile = GameProfile.Load(plan.GameId);
        _lobby = SteamLobby.Create(profile, plan.Options, ResolveGameDir(profile));
    }

    /// <summary>A guest without a session whose host opened a new lobby leaves the old one and joins the new one, through the
    /// same retrying path as a lost connection.</summary>
    private void FollowHost()
    {
        if (_session != null || _lobby == null || _follow?.MovedTo(_lobby) is not { } lobbyId) return;
        var gameId = _lobby.GameId;
        Log.Info($"The host opened lobby {lobbyId}, following");
        EndSession();
        _rejoin ??= new Rejoin(lobbyId) { GameId = gameId };
    }

    /// <summary>Drives a pending rejoin: an attempt every retry interval, or the host's next lobby when it publishes one. False
    /// when it gave up and the CLI should exit.</summary>
    private bool TryRejoin()
    {
        if (_rejoin == null || _lobby != null) return true;
        if (_rejoin.GaveUp)
        {
            Log.Info($"Could not rejoin lobby {_rejoin.LobbyId}, giving up");
            Refuse(new Refusal(RefusalKind.RejoinGaveUp, "", _rejoin.GameId, _rejoin.LobbyId));
            _rejoin = null;
            EndSession();
            return _interactive;
        }
        if (_rejoin.AttemptDue()) _lobby = JoinLobby(_rejoin.LobbyId);
        else FollowHostTo(_rejoin.LobbyId);
        return true;
    }

    /// <summary>Joins the lobby the host publishes other than <paramref name="oldLobbyId"/>, when there is one.</summary>
    private void FollowHostTo(ulong oldLobbyId)
    {
        if (_lobby != null || _follow?.Find(oldLobbyId) is not { } lobbyId) return;
        Log.Info($"The host opened lobby {lobbyId}, following");
        _lobby = JoinLobby(lobbyId);
    }
}
