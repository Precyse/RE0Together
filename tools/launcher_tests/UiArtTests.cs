using System.Drawing;
using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>UiArt on a temp game folder with small synthetic archives: every part is written, a second refresh writes nothing, a
/// changed archive rewrites only its part, a missing archive leaves its part out, and the stamp text round trips.</summary>
public static class UiArtTests
{
    private const int Version = 7;
    private const int NameSize = 64;
    private const int EntrySize = NameSize + 16;
    private const int HeaderSize = 8;
    private const uint TextureType = 0x241f5deb;
    private const uint LayoutType = 0x2d462600;
    private const int PanelWidth = 1024;
    private const int PanelHeight = 512;
    private const int SmallSize = 8;
    private const byte FormatBc3 = 0x18;
    private const byte FormatBc3YCbCr = 0x2B;
    private const string FontArc = "nativePC/arc/message/msg_eng.arc";
    private const string DialogArc = "nativePC/arc/ui/resident.arc";
    private const string OutfitArc = "nativePC/arc/ui/cos_02.arc";
    private static readonly DateTime LongAgo = new(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);

    private static readonly GameProfile Profile = new("t", "Test", 1, "t.exe", 2, 1, [], "coop/adapter.ini",
        Options: [new GameOption("outfit_rebecca", "Rebecca outfit", [new OptionValue(string.Empty, "Saved"), new OptionValue("02", "S.T.A.R.S. 2002")], Character: "rebecca")],
        OutfitArt: new ArtSource("nativePC/arc/ui/cos_{value}.arc", "cos_{value}_ID_HQ"),
        UiArt: new UiArtSource(FontArc, "font_eng_00_ID", "font_eng", DialogArc, "Dialog_00_ID_HQ"));

    public static void Run()
    {
        StampRoundTrips();
        var root = Path.Combine(Path.GetTempPath(), "cf_uiart_" + Guid.NewGuid().ToString("N"));
        var previous = Environment.GetEnvironmentVariable(AppData.DataRootVariable);
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, Path.Combine(root, "data"));
        try
        {
            WritesEveryPartOnce(Path.Combine(root, "game"));
            LeavesOutWhatTheGameDoesNotHave(Path.Combine(root, "partial"));
            WritesNothingWithoutAProfileEntry(Path.Combine(root, "plain"));
        }
        finally
        {
            Environment.SetEnvironmentVariable(AppData.DataRootVariable, previous);
            Directory.Delete(root, recursive: true);
        }
    }

    private static void StampRoundTrips()
    {
        var keys = new Dictionary<string, string> { ["outfit/02"] = "02-1234abcd.png", ["font"] = "0f0f0f0f" };
        var text = UiArt.FormatStamp(keys);
        Check.That(text == "font=0f0f0f0f\noutfit/02=02-1234abcd.png\n", "the stamp lists its parts in order");
        var parsed = UiArt.ParseStamp(text.Replace("\n", "\r\n") + "broken line\n=nokey\n");
        Check.That(parsed.Count == 2 && parsed["font"] == "0f0f0f0f" && parsed["outfit/02"] == "02-1234abcd.png",
            "a stamp parses with CR LF and skips lines without a part and a separator");
        Check.That(UiArt.ParseStamp(string.Empty).Count == 0, "no stamp text gives no keys");
    }

    private static void WritesEveryPartOnce(string game)
    {
        WriteGame(game, layout: [1, 2, 3], withDialog: true);
        UiArt.Refresh(Profile, game);
        var folder = UiArt.FolderOf(game);
        Check.That(File.ReadAllBytes(Path.Combine(folder, "font_eng.gfd")).SequenceEqual(new byte[] { 1, 2, 3 }), "the layout is written as it is in the archive");
        Check.That(SizeOf(Path.Combine(folder, "font_eng.png")) == new Size(SmallSize, SmallSize), "the font atlas is written as a picture");
        Check.That(SizeOf(Path.Combine(folder, "dialog_leather.png")) == new Size(720, 395), "the leather is cut from the panel");
        Check.That(SizeOf(Path.Combine(folder, "dialog_rod_top.png")) == new Size(746, 46), "the top rod is cut from the panel");
        Check.That(SizeOf(Path.Combine(folder, "dialog_rod_bottom.png")) == new Size(746, 48), "the bottom rod is cut from the panel");
        Check.That(File.Exists(Path.Combine(folder, "outfits", "02.png")), "an outfit with art is copied to outfits");
        Check.That(!File.Exists(Path.Combine(folder, "outfits", ".png")), "the saved outfit has no picture");
        var stamp = UiArt.ParseStamp(File.ReadAllText(Path.Combine(folder, "art.key")));
        Check.That(stamp.Keys.Order().SequenceEqual(["dialog", "font", "outfit/02"]), "the stamp names every part written");

        var files = Directory.GetFiles(folder, "*", SearchOption.AllDirectories);
        foreach (var file in files) File.SetLastWriteTimeUtc(file, LongAgo);
        UiArt.Refresh(Profile, game);
        Check.That(files.All(file => File.GetLastWriteTimeUtc(file) == LongAgo || Path.GetFileName(file) == "art.key"), "a second refresh writes no part again");

        WriteArchive(Path.Combine(game, FontArc.Replace('/', Path.DirectorySeparatorChar)), [Entry("ui\\00_font\\font_eng_00_ID", TextureType, SmallTexture(FormatBc3)),
            Entry("ui\\00_font\\font_eng", LayoutType, [4, 5, 6, 7])]);
        UiArt.Refresh(Profile, game);
        Check.That(File.ReadAllBytes(Path.Combine(folder, "font_eng.gfd")).SequenceEqual(new byte[] { 4, 5, 6, 7 }), "a changed font archive writes the font again");
        Check.That(File.GetLastWriteTimeUtc(Path.Combine(folder, "dialog_leather.png")) == LongAgo, "and leaves the other parts alone");

        File.Delete(Path.Combine(folder, "dialog_rod_top.png"));
        UiArt.Refresh(Profile, game);
        Check.That(File.Exists(Path.Combine(folder, "dialog_rod_top.png")), "a part whose file is gone is written again");
    }

    private static void LeavesOutWhatTheGameDoesNotHave(string game)
    {
        WriteGame(game, layout: [1], withDialog: false);
        UiArt.Refresh(Profile, game);
        var folder = UiArt.FolderOf(game);
        Check.That(File.Exists(Path.Combine(folder, "font_eng.png")), "the font is written without the dialog archive");
        Check.That(!File.Exists(Path.Combine(folder, "dialog_leather.png")), "no dialog archive, no dialog files");
        Check.That(!UiArt.ParseStamp(File.ReadAllText(Path.Combine(folder, "art.key"))).ContainsKey("dialog"), "and no dialog key in the stamp");
    }

    private static void WritesNothingWithoutAProfileEntry(string game)
    {
        Directory.CreateDirectory(game);
        UiArt.Refresh(Profile with { UiArt = null }, game);
        Check.That(!Directory.Exists(UiArt.FolderOf(game)), "a game without uiArt gets no folder");
    }

    private static Size SizeOf(string path)
    {
        using var bitmap = new Bitmap(path);
        return bitmap.Size;
    }

    private static void WriteGame(string game, byte[] layout, bool withDialog)
    {
        WriteArchive(Path.Combine(game, FontArc.Replace('/', Path.DirectorySeparatorChar)), [Entry("ui\\00_font\\font_eng_00_ID", TextureType, SmallTexture(FormatBc3)),
            Entry("ui\\00_font\\font_eng", LayoutType, layout)]);
        if (withDialog)
            WriteArchive(Path.Combine(game, DialogArc.Replace('/', Path.DirectorySeparatorChar)), [Entry("ui\\14_dialog\\tex\\Dialog_00_ID_HQ", TextureType, Texture(PanelWidth, PanelHeight, FormatBc3YCbCr))]);
        WriteArchive(Path.Combine(game, OutfitArc.Replace('/', Path.DirectorySeparatorChar)), [Entry("ui\\cos_02_ID_HQ", TextureType, SmallTexture(FormatBc3))]);
    }

    private static byte[] SmallTexture(byte format) => Texture(SmallSize, SmallSize, format);

    /// <summary>A texture body of blank BC3 blocks: the 20 byte header (dimensions, format, first mip at 20) and the blocks.</summary>
    private static byte[] Texture(int width, int height, byte format)
    {
        const int header = 20;
        var blocks = (width + 3) / 4 * ((height + 3) / 4);
        var body = new byte[header + blocks * 16];
        "TEX\0"u8.CopyTo(body);
        BitConverter.GetBytes((uint)(width << 6 | height << 19)).CopyTo(body, 8);
        BitConverter.GetBytes((uint)format << 8).CopyTo(body, 12);
        BitConverter.GetBytes((uint)header).CopyTo(body, 16);
        return body;
    }

    private static (string Name, uint Type, byte[] Body) Entry(string name, uint type, byte[] body) => (name, type, body);

    /// <summary>An ARC v7 file with every body stored uncompressed.</summary>
    private static void WriteArchive(string path, (string Name, uint Type, byte[] Body)[] entries)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var data = new MemoryStream();
        data.Write("ARC\0"u8);
        data.Write(BitConverter.GetBytes((ushort)Version));
        data.Write(BitConverter.GetBytes((ushort)entries.Length));
        var offset = HeaderSize + entries.Length * EntrySize;
        foreach (var (name, type, body) in entries)
        {
            var record = new byte[EntrySize];
            System.Text.Encoding.ASCII.GetBytes(name).CopyTo(record, 0);
            BitConverter.GetBytes(type).CopyTo(record, NameSize);
            BitConverter.GetBytes((uint)body.Length).CopyTo(record, NameSize + 4);
            BitConverter.GetBytes((uint)body.Length).CopyTo(record, NameSize + 8);
            BitConverter.GetBytes((uint)offset).CopyTo(record, NameSize + 12);
            data.Write(record);
            offset += body.Length;
        }
        foreach (var (_, _, body) in entries) data.Write(body);
        File.WriteAllBytes(path, data.ToArray());
    }
}
