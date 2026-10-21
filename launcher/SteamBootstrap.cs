using Steamworks;

namespace CoopLauncher;

/// <summary>Steam could not be started: <see cref="Result"/> says why (NoSteamClient = Steam is not running).</summary>
public sealed class SteamUnavailableException(ESteamAPIInitResult result, string detail = "")
    : InvalidOperationException($"SteamAPI init failed: {result} {detail}".TrimEnd())
{
    public ESteamAPIInitResult Result { get; } = result;
}

/// <summary>Steam went away while the launcher ran (it was closed or restarted); the loop ends and starts again when Steam is back.</summary>
public sealed class SteamLostException() : InvalidOperationException("Steam stopped running");

/// <summary>SteamAPI lifetime, callback pump, overlay-invite capture and the check that Steam is still there.</summary>
public sealed class SteamBootstrap : IDisposable
{
    private const long SteamCheckIntervalMs = 3000;

    private readonly Callback<GameLobbyJoinRequested_t> _joinRequested;
    private readonly Callback<SteamShutdown_t> _shutdown;
    private long _nextCheckMs;

    public SteamBootstrap()
    {
        var result = SteamAPI.InitEx(out var error);
        if (result != ESteamAPIInitResult.k_ESteamAPIInitResult_OK) throw new SteamUnavailableException(result, error);
        _joinRequested = Callback<GameLobbyJoinRequested_t>.Create(r =>
        {
            Log.Info($"Overlay invite for lobby {r.m_steamIDLobby.m_SteamID}");
            PendingInvite = (r.m_steamIDLobby.m_SteamID, r.m_steamIDFriend.m_SteamID);
        });
        _shutdown = Callback<SteamShutdown_t>.Create(_ => Lost = true);
        Log.Info($"Steam ready as {SteamFriends.GetPersonaName()} ({SteamUser.GetSteamID().m_SteamID})");
    }

    /// <summary>The lobby from an overlay invite or join request and the friend who sent it, until consumed.</summary>
    public (ulong LobbyId, ulong FromId)? PendingInvite { get; set; }

    /// <summary>Steam announced its shutdown or is no longer running.</summary>
    public bool Lost { get; private set; }

    public uint AccountId => SteamUser.GetSteamID().GetAccountID().m_AccountID;

    public void Pump()
    {
        SteamAPI.RunCallbacks();
        var now = Environment.TickCount64;
        if (now < _nextCheckMs) return;
        _nextCheckMs = now + SteamCheckIntervalMs;
        if (!SteamAPI.IsSteamRunning()) Lost = true;
    }

    public string PersonaName(ulong userId) => SteamFriends.GetFriendPersonaName(new CSteamID(userId)) ?? string.Empty;


    public void Dispose()
    {
        _joinRequested.Dispose();
        _shutdown.Dispose();
        SteamAPI.Shutdown();
    }
}
