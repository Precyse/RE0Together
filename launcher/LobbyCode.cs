using System.Globalization;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>Reads a lobby id out of what a player pastes: the digits as shown ("1097 7524 3712 345678"), with a label
/// ("Lobby: ..."), or a <c>steam://joinlobby/&lt;appid&gt;/&lt;lobby&gt;/&lt;host&gt;</c> link. Pure.</summary>
public static partial class LobbyCode
{
    private const string LinkMarker = "joinlobby/";
    private const char PathSeparator = '/';
    private const int LinkLobbySegment = 1;
    private const int MaxDigits = 20;

    [GeneratedRegex(@"\d+")]
    private static partial Regex DigitRuns();

    /// <summary>False unless the text holds exactly one run of 1..20 digits (spaces and dashes inside it are ignored)
    /// that is a non-zero 64-bit number.</summary>
    public static bool TryParse(string? text, out ulong lobbyId)
    {
        lobbyId = 0;
        if (string.IsNullOrWhiteSpace(text)) return false;
        var runs = DigitRuns().Matches(Compact(LinkLobby(text) ?? text));
        return runs.Count == 1 && runs[0].Length <= MaxDigits
            && ulong.TryParse(runs[0].Value, NumberStyles.None, CultureInfo.InvariantCulture, out lobbyId) && lobbyId != 0;
    }

    /// <summary>The lobby segment of a joinlobby link, null when the text is not one.</summary>
    private static string? LinkLobby(string text)
    {
        var at = text.IndexOf(LinkMarker, StringComparison.OrdinalIgnoreCase);
        if (at < 0) return null;
        var segments = text[(at + LinkMarker.Length)..].Split(PathSeparator);
        return segments.Length > LinkLobbySegment ? segments[LinkLobbySegment] : string.Empty;
    }

    private static string Compact(string text) => new(text.Where(c => !char.IsWhiteSpace(c) && c != '-').ToArray());
}
