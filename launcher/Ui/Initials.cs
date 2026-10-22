using System.Globalization;

namespace CoopLauncher.Gui;

/// <summary>The one rule for the letter that stands in for a player's avatar: the first character of the name in capitals.</summary>
internal static class Initials
{
    /// <summary>The first text element of the trimmed name, upper-cased; empty for an empty name.</summary>
    public static string Of(string name) =>
        name.Trim() is { Length: > 0 } trimmed ? new StringInfo(trimmed).SubstringByTextElements(0, 1).ToUpperInvariant() : string.Empty;
}
