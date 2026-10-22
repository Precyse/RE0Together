namespace CoopLauncher;

public sealed record LobbyMember(ulong Id, string Name);

/// <summary>What a member publishes about itself: the game's own status line (rendezvous games), its session step
/// (<see cref="StepCodec"/>) and its own option values (<see cref="GameOptions.OwnValues"/>, encoded).</summary>
public enum MemberKey { GameStatus, Step, Options }

/// <summary>What the host keeps up to date in the lobby's own data: the roster (<see cref="RosterCodec"/>), the start of play
/// (Unix seconds) and its option values (<see cref="GameOptions.HostValues"/>, encoded, written again when the host's
/// character changes); the rest of the lobby data is written once when it is created.</summary>
public enum LobbyKey { Roster, Started, HostOptions }

/// <summary>Membership source for a session. Polled from the main loop.</summary>
public interface ILobby : IDisposable
{
    bool IsReady { get; }

    /// <summary>Set when the lobby cannot be used (join failure, game or protocol mismatch, connection lost).</summary>
    LobbyFailure? Failure { get; }

    string? GameId { get; }

    /// <summary>Code other players join with; 0 while the lobby does not exist yet.</summary>
    ulong Id { get; }

    ulong OwnerId { get; }

    /// <summary>The host's option values (GameOption.Host), set when the lobby was created, rewritten by the host through
    /// <see cref="LobbyKey.HostOptions"/> and read by every member.</summary>
    IReadOnlyDictionary<string, string> HostOptions { get; }

    /// <summary>The password the host made for a rendezvous game's session (empty until known, and for other games).</summary>
    string JoinSecret { get; }

    /// <summary>Publishes one of our own values for the other members to read.</summary>
    void SetMemberData(MemberKey key, string value);

    /// <summary>Publishes one of the lobby's own values for everyone to read; only the lobby's owner can.</summary>
    void SetLobbyData(LobbyKey key, string value);

    /// <summary>The lobby as its own data describes it (null until the lobby exists); read again when the data changes.</summary>
    LobbyListing? Listing { get; }

    /// <summary>The value a member published, empty when it has none.</summary>
    string MemberData(LobbyMember member, MemberKey key);

    /// <summary>Members compatible with our game and protocol.</summary>
    IReadOnlyList<LobbyMember> Members { get; }

    /// <summary>Increments whenever Members or OwnerId change.</summary>
    int Revision { get; }

    void Pump();
}
