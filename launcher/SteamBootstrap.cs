using Steamworks;

namespace CoopLauncher;

/// <summary>SteamAPI lifetime, callback pump and overlay-invite capture.</summary>
public sealed class SteamBootstrap : IDisposable
{
    private readonly Callback<GameLobbyJoinRequested_t> _joinRequested;

    public SteamBootstrap()
    {
        if (SteamAPI.InitEx(out var error) != ESteamAPIInitResult.k_ESteamAPIInitResult_OK)
            throw new InvalidOperationException($"SteamAPI init failed: {error}");
        _joinRequested = Callback<GameLobbyJoinRequested_t>.Create(r =>
        {
            Log.Info($"Overlay invite for lobby {r.m_steamIDLobby.m_SteamID}");
            PendingInviteLobby = r.m_steamIDLobby.m_SteamID;
        });
        Log.Info($"Steam ready as {SteamFriends.GetPersonaName()} ({SteamUser.GetSteamID().m_SteamID})");
    }

    /// <summary>Lobby id from an overlay invite or join request, until consumed.</summary>
    public ulong? PendingInviteLobby { get; set; }

    public uint AccountId => SteamUser.GetSteamID().GetAccountID().m_AccountID;

    public void Pump() => SteamAPI.RunCallbacks();

    public void ShowInviteDialog(ulong lobbyId) => SteamFriends.ActivateGameOverlayInviteDialog(new CSteamID(lobbyId));

    public void Dispose()
    {
        _joinRequested.Dispose();
        SteamAPI.Shutdown();
    }
}
