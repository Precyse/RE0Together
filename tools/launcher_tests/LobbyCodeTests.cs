using CoopLauncher;

namespace LauncherTests;

/// <summary>LobbyCode.TryParse: the digits as shown, with spaces, dashes or a label, or a joinlobby link; nothing else.</summary>
public static class LobbyCodeTests
{
    private const ulong Lobby = 109775243712345678;

    public static void Run()
    {
        Accepts("109775243712345678", "plain digits");
        Accepts("1097 7524 3712 345678", "digits in groups");
        Accepts("1097-7524-3712-345678", "digits with dashes");
        Accepts("  109775243712345678\r\n", "digits with surrounding whitespace");
        Accepts("Lobby: 1097 7524 3712 345678", "a labelled code");
        Accepts("steam://joinlobby/480/109775243712345678/76561198000000000", "a steam link with the host id");
        Accepts("steam://joinlobby/480/109775243712345678", "a steam link without the host id");
        Rejects("", "empty text");
        Rejects("   ", "blank text");
        Rejects("0", "zero");
        Rejects("0000", "zeros");
        Rejects("1234 abc 5678", "two separate numbers");
        Rejects("999999999999999999999", "21 digits");
        Rejects("99999999999999999999", "20 digits past the 64-bit range");
        Rejects("abcdef", "letters only");
        Rejects("steam://joinlobby/480", "a link without a lobby");
        Check.That(!LobbyCode.TryParse(null, out _), "null text is refused");
    }

    private static void Accepts(string text, string what)
    {
        Check.That(LobbyCode.TryParse(text, out var id) && id == Lobby, $"{what} gives the lobby id");
    }

    private static void Rejects(string text, string what)
    {
        Check.That(!LobbyCode.TryParse(text, out var id) && id == 0, $"{what} is refused");
    }
}
