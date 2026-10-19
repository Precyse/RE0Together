using System.Text;

namespace CoopLauncher;

/// <summary>
/// The name a rendezvous game's local player gets: the Steam persona name shaped to what the game's HUD and scoreboard take
/// (docs/CONTRACT.md "Join file"), and made different from the other lobby members' names, because the game denies a join
/// with a name already in the session. Pure; the adapter checks the same character set again at its boundary.
/// </summary>
public static class PlayerName
{
    public const int MaxLength = 15;

    private const char Replacement = '?';
    private const char FirstPrintable = ' ';
    private const char LastAscii = '~';
    private const int FirstLatin1 = 0xA1;
    private const int LastLatin1 = 0xFF;
    private const string DuplicateSeparator = " ";
    private const int FirstDuplicateNumber = 2;

    /// <summary>Printable ASCII and Latin-1 are kept, control characters dropped, every other code point becomes "?", then
    /// the edges are trimmed and the name cut to <see cref="MaxLength"/> characters.</summary>
    public static string Shape(string persona)
    {
        var shaped = new StringBuilder();
        foreach (var rune in persona.EnumerateRunes())
        {
            if (Rune.IsControl(rune)) continue;
            shaped.Append(IsKept(rune.Value) ? (char)rune.Value : Replacement);
        }
        return Cut(shaped.ToString().Trim(), MaxLength);
    }

    /// <summary>The local player's name for the join file, empty when there is none to write. A member whose shaped name
    /// equals another's (case-insensitive) gets " 2", " 3" ... in ascending Steam id order, the first one keeps the name,
    /// and the base is cut so the whole name stays within <see cref="MaxLength"/>.</summary>
    public static string ForLocal(ulong localId, IReadOnlyList<LobbyMember> members)
    {
        var local = members.FirstOrDefault(m => m.Id == localId);
        if (local is null) return string.Empty;
        var name = Shape(local.Name);
        if (name.Length == 0) return string.Empty;
        var sameName = members.Where(m => string.Equals(Shape(m.Name), name, StringComparison.OrdinalIgnoreCase))
                              .OrderBy(m => m.Id).ToList();
        var rank = sameName.FindIndex(m => m.Id == localId);
        if (rank == 0) return name;
        var suffix = DuplicateSeparator + (rank + FirstDuplicateNumber - 1);
        return Cut(name, MaxLength - suffix.Length) + suffix;
    }

    private static bool IsKept(int codePoint) =>
        (codePoint >= FirstPrintable && codePoint <= LastAscii) || (codePoint >= FirstLatin1 && codePoint <= LastLatin1);

    private static string Cut(string name, int length) => name.Length <= length ? name : name[..length].TrimEnd();
}
