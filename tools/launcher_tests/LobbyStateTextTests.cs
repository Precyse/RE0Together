using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>LOBBY_STATE text: member, option, value, outfit and own lines of RE0's lobby, the cleaned fields and the byte cap.</summary>
public static class LobbyStateTextTests
{
    private const ulong HostId = 1;
    private const ulong GuestId = 2;
    private const int ManyPlayers = 200;

    private static readonly GameProfile Re0 = GameProfile.Load("re0");

    public static void Run()
    {
        var text = Build(Status(Host(), Guest()));
        var lines = text.Split('\n');
        MemberLines(lines);
        OptionLines(lines);
        ValueLines(lines);
        OutfitLines(lines, DlcOutfitLines());
        OwnLine(lines);
        MemberWithoutOutfitOrCharacter();
        NamesAreCleanedAndCut();
        CapDropsWholeLines();
    }

    private static string Row(params string[] fields) => string.Join('\t', fields);

    private static PlayerSlot Host(string name = "Precyse") => new(HostId, name, IsHost: true, IsLocal: true, Options: "outfit_rebecca=7d");

    private static PlayerSlot Guest() => new(GuestId, "Amy", IsHost: false, IsLocal: false, Options: "outfit_billy=");

    private static AppStatus Status(params PlayerSlot[] players) => new(AppState.Hosting, 1)
    {
        Players = players, SeatCharacters = ["Rebecca", "Billy"],
        HostOptions = new Dictionary<string, string>
        {
            ["partner_mode"] = "state", ["character"] = "rebecca", ["load_slot"] = "0", ["load_hash"] = "e40c292c", [GameOptions.SeatsKey] = "2",
        },
    };

    private static string Build(AppStatus status, string? gameDir = null) =>
        LobbyStateText.Build(Re0, gameDir, status, new Dictionary<string, string> { ["outfit_rebecca"] = "7d", ["outfit_billy"] = "" },
            id => id == HostId ? (byte)0 : null);

    private static void MemberLines(string[] lines)
    {
        Check.That(lines[0] == Row("member", "0", "1", "1", "rebecca", "Rebecca", "Western", "Precyse"), $"LOBBY_STATE: the host's member line first ({lines[0]})");
        Check.That(lines[1] == Row("member", "255", "0", "0", "billy", "Billy", "Saved", "Amy"), $"LOBBY_STATE: a member without a slot is 255, the empty outfit reads Saved ({lines[1]})");
    }

    private static void OptionLines(string[] lines)
    {
        Check.That(lines[2] == Row("option", "partner_mode", "state", "State"), "LOBBY_STATE: the lobby's partner mode with its label");
        Check.That(lines[3] == Row("option", "world_pause", "never", "Off"), "LOBBY_STATE: an option the lobby does not carry shows its default");
        Check.That(lines[4] == Row("option", "character", "rebecca", "Rebecca"), "LOBBY_STATE: the host's character with its label");
        Check.That(!lines.Any(line => line.Contains("load_slot") || line.Contains("load_hash") || line.Contains(GameOptions.SeatsKey)), "LOBBY_STATE: no save slot, hash or player count line");
    }

    private static void ValueLines(string[] lines)
    {
        var values = lines.Where(line => line.StartsWith("value	")).ToArray();
        string[] expected =
        [
            Row("value", "partner_mode", "input", "Input"), Row("value", "partner_mode", "state", "State"),
            Row("value", "world_pause", "never", "Off"), Row("value", "world_pause", "menus", "On"),
            Row("value", "character", "rebecca", "Rebecca"), Row("value", "character", "billy", "Billy"),
        ];
        Check.That(values.SequenceEqual(expected), $"LOBBY_STATE: every value of the page's host options, in profile order ({string.Join(" | ", values)})");
    }

    private static string[] DlcOutfitLines()
    {
        var gameDir = Path.Combine(Path.GetTempPath(), "coop-lobby-state-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(gameDir, "DLC", "381713"));
            return Build(Status(Host(), Guest()), gameDir).Split('\n').Where(line => line.StartsWith("outfit\trebecca")).ToArray();
        }
        finally
        {
            Directory.Delete(gameDir, recursive: true);
        }
    }

    private static void OutfitLines(string[] lines, string[] withDlc)
    {
        var outfits = lines.Where(line => line.StartsWith("outfit\t")).ToArray();
        string[] expected =
        [
            Row("outfit", "rebecca", "", "Saved"), Row("outfit", "rebecca", "02", "S.T.A.R.S. 2002"), Row("outfit", "rebecca", "7d", "Western"),
            Row("outfit", "rebecca", "81", "Leather"), Row("outfit", "rebecca", "91", "Team Wesker"),
            Row("outfit", "billy", "", "Saved"), Row("outfit", "billy", "05", "Prisoner"), Row("outfit", "billy", "95", "Jacket"), Row("outfit", "billy", "a5", "Wesker"),
        ];
        Check.That(outfits.SequenceEqual(expected), $"LOBBY_STATE: each character's outfits in profile order, no DLC without its folder ({string.Join(" | ", outfits)})");
        Check.That(withDlc.Skip(5).SequenceEqual([Row("outfit", "rebecca", "71", "S.T.A.R.S. 1996"), Row("outfit", "rebecca", "89", "S.T.A.R.S. Prototype")]),
            "LOBBY_STATE: the DLC outfits of an installed DLC follow in profile order");
    }

    private static void OwnLine(string[] lines)
    {
        Check.That(lines.Contains(Row("own", "outfit_rebecca", "7d")) && lines.Contains(Row("own", "outfit_billy", "")), "LOBBY_STATE: this player's own values");
        Check.That(lines[^1].StartsWith("own\t"), "LOBBY_STATE: the own lines come last");
    }

    private static void MemberWithoutOutfitOrCharacter()
    {
        var noOptions = Build(Status(Host() with { Options = "" }, Guest())).Split('\n')[0];
        Check.That(noOptions == Row("member", "0", "1", "1", "rebecca", "Rebecca", "", "Precyse"), "LOBBY_STATE: a member that published no outfit has an empty outfit label");
        var noCharacters = LobbyStateText.Build(Re0 with { Options = [] }, null, Status(Host()), new Dictionary<string, string>(), _ => 0);
        Check.That(noCharacters == Row("member", "0", "1", "1", "", "", "", "Precyse"), $"LOBBY_STATE: a game without characters has empty character fields ({noCharacters})");
    }

    private static void NamesAreCleanedAndCut()
    {
        var cleaned = Build(Status(Host("Pre\tcy\r\nse"), Guest())).Split('\n');
        Check.That(cleaned[0].EndsWith("\tPre cy  se") && cleaned.Length > 2 && cleaned[1].StartsWith("member\t"), "LOBBY_STATE: tabs and line breaks in a name become spaces");
        var cut = Build(Status(Host(new string('n', 70)), Guest())).Split('\n')[0];
        Check.That(cut.EndsWith("\t" + new string('n', LobbyStateText.MaxNameChars)), "LOBBY_STATE: a name is cut to 64 characters");
        var pair = Build(Status(Host(new string('n', 63) + "\U0001F600"), Guest())).Split('\n')[0];
        Check.That(pair.EndsWith("\t" + new string('n', 63)), "LOBBY_STATE: a cut never leaves half a surrogate pair");
    }

    private static void CapDropsWholeLines()
    {
        var players = Enumerable.Range(0, ManyPlayers).Select(i => new PlayerSlot(100 + (ulong)i, new string('x', LobbyStateText.MaxNameChars), IsHost: i == 0, IsLocal: false)).ToArray();
        var lines = Build(Status(players)).Split('\n');
        var bytes = Encoding.UTF8.GetByteCount(string.Join('\n', lines));
        Check.That(bytes <= LobbyStateText.MaxBytes && lines.Length < ManyPlayers, $"LOBBY_STATE: the text is capped at 8192 bytes ({bytes} bytes, {lines.Length} lines)");
        Check.That(lines.All(line => line.StartsWith("member\t") && line.Split('\t').Length == 8), "LOBBY_STATE: past the cap whole lines are dropped, none is cut");
        Check.That(LobbyStateText.MaxBytes - bytes < Encoding.UTF8.GetByteCount(lines[^1]) + 1, "LOBBY_STATE: the cap drops only the lines that do not fit");
    }
}
