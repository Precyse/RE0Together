namespace CoopLauncher;

public sealed record LobbyMember(ulong Id, string Name);

/// <summary>Membership source for a session. Polled from the main loop.</summary>
public interface ILobby : IDisposable
{
    bool IsReady { get; }

    /// <summary>Set when the lobby cannot be used (join failure, game or protocol mismatch).</summary>
    string? Failure { get; }

    string? GameId { get; }

    /// <summary>Code other players join with; 0 while the lobby does not exist yet.</summary>
    ulong Id { get; }

    ulong OwnerId { get; }

    /// <summary>Members compatible with our game and protocol.</summary>
    IReadOnlyList<LobbyMember> Members { get; }

    /// <summary>Increments whenever Members or OwnerId change.</summary>
    int Revision { get; }

    void Pump();
}
