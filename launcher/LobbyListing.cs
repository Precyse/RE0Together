using System.Globalization;

namespace CoopLauncher;

/// <summary>The Steam lobby data keys of a co-op lobby (<c>cf_*</c>). The host writes them when it creates the lobby, apart from
/// the roster and the start time, which it keeps up to date; every member and every searcher reads them.</summary>
public static class LobbyKeys
{
    /// <summary>The marker that tells a co-op launcher's lobby from every other app's: AppID 480 is shared by every Steamworks tester.</summary>
    public const string App = "cf_app";
    public const string AppMarker = "coop-launcher";
    public const string Game = "cf_game";
    public const string Proto = "cf_proto";
    public const string Version = "cf_ver";
    public const string HostOptions = "cf_opts";
    public const string JoinSecret = "cf_join_pw";
    public const string Host = "cf_host";
    public const string Mod = "cf_mod";
    public const string GameBuild = "cf_gbuild";
    public const string Location = "cf_loc";
    public const string Roster = "cf_roster";
    public const string Started = "cf_started";
}

/// <summary>One lobby member as the host publishes it in <c>cf_roster</c>: the name and the step its launcher reports.</summary>
public sealed record RosterEntry(string Name, SessionStep Step);

/// <summary>The host-published roster text: one line per member, <c>name TAB step name</c>, lines joined by LF.</summary>
public static class RosterCodec
{
    private const char FieldSeparator = '\t';
    private const char LineSeparator = '\n';
    private const char Replacement = ' ';

    /// <summary>A name with a tab or a line break would break the lines, so they become spaces.</summary>
    public static string Encode(IEnumerable<RosterEntry> roster) =>
        string.Join(LineSeparator, roster.Select(entry => $"{Clean(entry.Name)}{FieldSeparator}{entry.Step}"));

    /// <summary>The entries of a roster text; a line that is not <c>name TAB known step</c> is skipped.</summary>
    public static IReadOnlyList<RosterEntry> Decode(string text)
    {
        var roster = new List<RosterEntry>();
        foreach (var line in text.Split(LineSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            var parts = line.TrimEnd('\r').Split(FieldSeparator);
            if (parts.Length == 2 && TryStep(parts[1], out var step)) roster.Add(new RosterEntry(parts[0], step));
        }
        return roster;
    }

    private static string Clean(string name) => name.Replace(FieldSeparator, Replacement).Replace(LineSeparator, Replacement).Replace('\r', Replacement);

    /// <summary>Only a step's exact name: not a number, not several names joined by commas.</summary>
    private static bool TryStep(string name, out SessionStep step) => Enum.TryParse(name, out step) && step.ToString() == name;
}

/// <summary>A lobby as the Lobbies tab, the friend cards and the joining screen show it, read from the lobby's own data
/// (<see cref="LobbyKeys"/>). Builds are 0 where the host publishes none (an older launcher); <see cref="PingMs"/> is null
/// while unknown; <see cref="StartedUnix"/> is 0 before the host's game ran.</summary>
public sealed record LobbyListing(
    ulong LobbyId, string GameId, ulong HostId, string HostName, int Players, int MaxPlayers,
    int LauncherBuild, int ModBuild, int GameBuild, IReadOnlyDictionary<string, string> HostOptions,
    IReadOnlyList<RosterEntry> Roster, long StartedUnix, int? PingMs)
{
    /// <summary>The listing of a lobby from its data (<paramref name="data"/> returns empty for a missing key): pure.</summary>
    public static LobbyListing Decode(Func<string, string> data, ulong lobbyId, ulong ownerId, int members, int max, int? ping) => new(
        lobbyId, data(LobbyKeys.Game), ownerId, data(LobbyKeys.Host), members, max,
        Number(data(LobbyKeys.Version)), Number(data(LobbyKeys.Mod)), Number(data(LobbyKeys.GameBuild)),
        GameOptions.Decode(data(LobbyKeys.HostOptions)), RosterCodec.Decode(data(LobbyKeys.Roster)),
        long.TryParse(data(LobbyKeys.Started), NumberStyles.None, CultureInfo.InvariantCulture, out var started) ? started : 0, ping);

    /// <summary>Equal when every value is, the host options and the roster by content (a record compares those by reference).</summary>
    public bool Equals(LobbyListing? other) =>
        other is not null && LobbyId == other.LobbyId && GameId == other.GameId && HostId == other.HostId && HostName == other.HostName
        && Players == other.Players && MaxPlayers == other.MaxPlayers && LauncherBuild == other.LauncherBuild
        && ModBuild == other.ModBuild && GameBuild == other.GameBuild && StartedUnix == other.StartedUnix && PingMs == other.PingMs
        && Roster.SequenceEqual(other.Roster) && HostOptions.Count == other.HostOptions.Count
        && HostOptions.All(pair => other.HostOptions.TryGetValue(pair.Key, out var value) && value == pair.Value);

    public override int GetHashCode() => HashCode.Combine(LobbyId, GameId, HostId, Players);

    private static int Number(string text) => int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out var number) ? number : 0;
}
