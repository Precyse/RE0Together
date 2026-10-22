using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using CoopLauncher;

namespace LauncherTests;

/// <summary>PackFile and ScriptDelta on synthetic packfiles and recipes in temp folders, and (opt in) the committed recipe against the
/// player's real game files. A script is built from the player's own bytes only when their hashes are the pinned ones.</summary>
public static class ScriptDeltaTests
{
    internal const string PackName = "pack.vpp_pc";
    internal const string FixesFolder = "Mods/Fixes";
    internal const string AppendedText = "-- co-op\r\n";
    private const string RealGameVariable = "SR2_GAME_DIR";
    private const string RealScriptsVariable = "SR2_SCRIPTS_COMPARE";
    private const string CommittedRecipe = "adapters/sr2/scripts.delta.json";
    private const int RealScriptCount = 50;
    private const int Alignment = 0x800;
    private const int HeaderSize = 0x170;
    private const int EntrySize = 28;

    /// <summary>A script of a synthetic recipe: where it starts from, those bytes, the finished bytes and the ops between.</summary>
    internal sealed record Script(string Name, string Source, byte[] SourceBytes, byte[] Target, object[][] Ops);

    public static void Run()
    {
        PackFileReadsAnEntryByName();
        PackFileRefusesWhatItCannotRead();
        RecipeBuildsFromThePlayersFiles();
        RecipeRefusesAFileThatIsNotTheSupportedOne();
        RecipeRefusesADamagedRecipe();
        CommittedRecipeBuildsTheRealScripts();
    }

    /// <summary>A recipe script that keeps the whole source and appends <see cref="AppendedText"/>.</summary>
    internal static Script Appending(string name, string source, byte[] sourceBytes) =>
        new(name, source, sourceBytes, [.. sourceBytes, .. Encoding.Latin1.GetBytes(AppendedText)],
            [["c", 0, sourceBytes.Length], ["i", AppendedText]]);

    internal static string RecipeJson(IEnumerable<Script> scripts, int format = 1) => JsonSerializer.Serialize(new
    {
        format,
        files = scripts.Select(script => new
        {
            name = script.Name, source = script.Source, sourceSha256 = Sha(script.SourceBytes), sourceSize = script.SourceBytes.Length,
            sha256 = Sha(script.Target), size = script.Target.Length, ops = script.Ops,
        }),
    });

    private static string Sha(byte[] bytes) => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes));

    /// <summary>Writes a version 4 packfile with the layout of the real ones: header, then directory, names, extensions and data on
    /// 0x800 boundaries. <paramref name="flags"/> and <paramref name="condensed"/> make it a pack the reader must refuse.</summary>
    internal static void WritePack(string path, IReadOnlyList<(string Name, byte[] Bytes)> entries, uint flags = 0, bool condensed = false)
    {
        var names = new MemoryStream();
        var extensions = new MemoryStream();
        var data = new MemoryStream();
        var directory = new byte[entries.Count * EntrySize];
        for (var index = 0; index < entries.Count; index++)
        {
            var (name, bytes) = entries[index];
            var dot = name.LastIndexOf('.');
            var entry = directory.AsSpan(index * EntrySize);
            BinaryPrimitives.WriteUInt32LittleEndian(entry, (uint)names.Position);
            BinaryPrimitives.WriteUInt32LittleEndian(entry[4..], (uint)extensions.Position);
            BinaryPrimitives.WriteUInt32LittleEndian(entry[12..], (uint)data.Position);
            BinaryPrimitives.WriteUInt32LittleEndian(entry[16..], (uint)bytes.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(entry[20..], uint.MaxValue);
            names.Write(Encoding.Latin1.GetBytes(name[..dot] + "\0"));
            extensions.Write(Encoding.Latin1.GetBytes(name[(dot + 1)..] + "\0"));
            data.Write(bytes);
        }
        var header = new byte[HeaderSize];
        BinaryPrimitives.WriteUInt32LittleEndian(header, 0x51890ACE);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(4), 4);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(0x150), flags);
        var counts = new uint[] { (uint)entries.Count, 0, (uint)directory.Length, (uint)names.Length, (uint)extensions.Length, (uint)data.Length, condensed ? 1u : uint.MaxValue };
        for (var index = 0; index < counts.Length; index++) BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(0x154 + index * 4), counts[index]);
        using var file = File.Create(path);
        file.Write(header);
        foreach (var part in new[] { directory, names.ToArray(), extensions.ToArray(), data.ToArray() })
        {
            file.Position = (file.Length + Alignment - 1) / Alignment * Alignment;
            file.Write(part);
        }
    }

    internal static byte[] Text(string text) => Encoding.Latin1.GetBytes(text);

    private static string TempFolder() => Path.Combine(Path.GetTempPath(), "cf_delta_" + Guid.NewGuid().ToString("N"));

    private static void PackFileReadsAnEntryByName()
    {
        var folder = Directory.CreateDirectory(TempFolder()).FullName;
        try
        {
            var path = Path.Combine(folder, PackName);
            WritePack(path, [("one.lua", Text("first\r\n")), ("two.xtbl", Text("<tbl/>")), ("big.lua", new byte[5000])]);
            Check.That(PackFile.ReadEntry(path, "one.lua")!.SequenceEqual(Text("first\r\n")), "an entry is read by name and extension");
            Check.That(PackFile.ReadEntry(path, "TWO.XTBL")!.SequenceEqual(Text("<tbl/>")), "the name is compared without case");
            Check.That(PackFile.ReadEntry(path, "big.lua")!.Length == 5000, "an entry that spans several alignment blocks is read whole");
            Check.That(PackFile.ReadEntry(path, "three.lua") == null, "an entry the pack does not have reads as null");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static void PackFileRefusesWhatItCannotRead()
    {
        var folder = Directory.CreateDirectory(TempFolder()).FullName;
        try
        {
            var entries = new[] { ("a.lua", Text("alpha")) };
            string Pack(string name) => Path.Combine(folder, name);
            WritePack(Pack("compressed"), entries, flags: 1);
            WritePack(Pack("condensed"), entries, condensed: true);
            File.WriteAllBytes(Pack("notapack"), new byte[HeaderSize]);
            File.WriteAllBytes(Pack("tiny"), new byte[10]);
            WritePack(Pack("whole"), entries);
            var whole = File.ReadAllBytes(Pack("whole"));
            File.WriteAllBytes(Pack("cut"), whole[..(whole.Length - 3)]);
            Check.That(Throws(() => PackFile.ReadEntry(Pack("compressed"), "a.lua")), "a compressed pack is refused");
            Check.That(Throws(() => PackFile.ReadEntry(Pack("condensed"), "a.lua")), "a condensed pack is refused");
            Check.That(Throws(() => PackFile.ReadEntry(Pack("notapack"), "a.lua")), "a file without the packfile magic is refused");
            Check.That(Throws(() => PackFile.ReadEntry(Pack("tiny"), "a.lua")), "a file shorter than the header is refused");
            Check.That(Throws(() => PackFile.ReadEntry(Pack("cut"), "a.lua")), "a pack that ends inside its data is refused");
            Check.That(PackFile.ReadEntry(Pack("whole"), "a.lua")!.SequenceEqual(Text("alpha")), "the same pack uncut reads fine");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static bool Throws(Action action)
    {
        try
        {
            action();
            return false;
        }
        catch (InvalidDataException)
        {
            return true;
        }
    }

    /// <summary>A synthetic game folder: a pack with two scripts and a Juiced-like folder with one, and the recipe that finishes them.</summary>
    internal static (string Game, string Recipe, List<Script> Scripts) NewGame(string folder)
    {
        var game = Directory.CreateDirectory(Path.Combine(folder, "game")).FullName;
        var alpha = Text("alpha\r\nsecond line\r\n");
        var beta = Text("beta\nno carriage returns\n");
        var fixedSource = Text("café \u0093quoted\u0094\r\nfixed\r\n");
        WritePack(Path.Combine(game, PackName), [("a.lua", alpha), ("b.lua", beta), ("noise.xtbl", new byte[3000])]);
        Directory.CreateDirectory(Path.Combine(game, FixesFolder));
        File.WriteAllBytes(Path.Combine(game, FixesFolder, "c.lua"), fixedSource);
        var replaced = Text("café \u0093quoted\u0094\r\nFIXED\r\n");
        var scripts = new List<Script>
        {
            Appending("a.lua", $"{PackName}:a.lua", alpha),
            Appending("b.lua", $"{PackName}:b.lua", beta),
            new("c.lua", $"{FixesFolder}/c.lua", fixedSource, replaced, [["c", 0, fixedSource.Length - 7], ["i", "FIXED\r\n"]]),
        };
        var recipe = Path.Combine(folder, "scripts.delta.json");
        File.WriteAllText(recipe, RecipeJson(scripts));
        return (game, recipe, scripts);
    }

    private static void RecipeBuildsFromThePlayersFiles()
    {
        var folder = Directory.CreateDirectory(TempFolder()).FullName;
        try
        {
            var (game, recipe, scripts) = NewGame(folder);
            var delta = ScriptDelta.Load(recipe);
            Check.That(delta.Scripts.Select(script => script.Name).SequenceEqual(["a.lua", "b.lua", "c.lua"]), "the recipe lists its scripts in name order");
            Check.That(delta.Find("A.LUA") != null && delta.Find("none.lua") == null, "a script is found by name without case");
            foreach (var script in scripts)
                Check.That(delta.Build(script.Name, game).SequenceEqual(script.Target), $"{script.Name} is built from the player's file, byte for byte");
            Check.That(delta.Build("c.lua", game).AsSpan(3, 1)[0] == 0xE9, "latin-1 bytes outside ASCII survive");
            Check.That(delta.Build("a.lua", game).SequenceEqual(delta.Build("a.lua", game)), "building twice gives the same bytes");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static void RecipeRefusesAFileThatIsNotTheSupportedOne()
    {
        var folder = Directory.CreateDirectory(TempFolder()).FullName;
        try
        {
            var (game, recipe, scripts) = NewGame(folder);
            var delta = ScriptDelta.Load(recipe);
            var fixes = Path.Combine(game, FixesFolder, "c.lua");
            File.WriteAllBytes(fixes, [.. scripts[2].SourceBytes, .. Text("x")]);
            Check.That(Refusal(delta, "c.lua", game) is { } fixesReason && fixesReason.Contains("c.lua") && fixesReason.Contains("Fixes"),
                "a Juiced file of another version is refused, naming the file and its folder");
            File.WriteAllBytes(fixes, scripts[2].SourceBytes);
            var pack = Path.Combine(game, PackName);
            WritePack(pack, [("a.lua", Text("alpha\r\nsecond line!\r\n")), ("b.lua", scripts[1].SourceBytes)]);
            Check.That(Refusal(delta, "a.lua", game) is { } packReason && packReason.Contains("a.lua") && packReason.Contains(PackName),
                "a pack entry of another version is refused, naming the entry and the pack");
            WritePack(pack, [("b.lua", scripts[1].SourceBytes)]);
            Check.That(Refusal(delta, "a.lua", game) is { } missingEntry && missingEntry.Contains("a.lua"), "a pack without the entry is refused");
            File.Delete(pack);
            Check.That(Refusal(delta, "a.lua", game) is { } missingPack && missingPack.Contains(PackName), "a game folder without the pack is refused");
            File.Delete(fixes);
            Check.That(Refusal(delta, "c.lua", game) is { } missingFile && missingFile.Contains("c.lua"), "a missing Juiced file is refused");
            Check.That(Refusal(delta, "none.lua", game) != null, "a script that is not in the recipe is refused");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static string? Refusal(ScriptDelta delta, string name, string game)
    {
        try
        {
            delta.Build(name, game);
            return null;
        }
        catch (ScriptDeltaException e)
        {
            return e.Message;
        }
    }

    private static void RecipeRefusesADamagedRecipe()
    {
        var folder = Directory.CreateDirectory(TempFolder()).FullName;
        try
        {
            var (game, _, scripts) = NewGame(folder);
            string Write(string name, string text)
            {
                var path = Path.Combine(folder, name);
                File.WriteAllText(path, text);
                return path;
            }

            Check.That(LoadFails(Write("cut.json", RecipeJson(scripts)[..40])), "a recipe cut short is refused");
            Check.That(LoadFails(Write("format.json", RecipeJson(scripts, format: 2))), "a recipe of another format is refused");
            Check.That(LoadFails(Write("empty.json", "{}")), "a recipe without its files is refused");
            var traversal = scripts[0] with { Name = "../a.lua" };
            Check.That(LoadFails(Write("name.json", RecipeJson([traversal]))), "a script name with a path in it is refused");
            var beyond = scripts[0] with { Ops = [["c", 0, scripts[0].SourceBytes.Length + 1]] };
            Check.That(Refusal(ScriptDelta.Load(Write("beyond.json", RecipeJson([beyond]))), "a.lua", game) is { } copy && copy.Contains("a.lua"),
                "a copy past the end of the source is refused");
            var drift = scripts[0] with { Ops = [["c", 0, scripts[0].SourceBytes.Length], ["i", AppendedText.Replace("co-op", "co-oP")]] };
            Check.That(Refusal(ScriptDelta.Load(Write("output.json", RecipeJson([drift]))), "a.lua", game) is { } output && output.Contains("a.lua"),
                "a script that does not build to its pinned hash is refused");
            var beyondLatin1 = scripts[0] with { Ops = [["i", "€"]] };
            Check.That(LoadFails(Write("latin.json", RecipeJson([beyondLatin1]))), "an insert beyond latin-1 is refused");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static bool LoadFails(string path)
    {
        try
        {
            ScriptDelta.Load(path);
            return false;
        }
        catch (ScriptDeltaException)
        {
            return true;
        }
    }

    /// <summary>With SR2_GAME_DIR (a read-only copy of the game's patch.vpp_pc and Juiced's TU3 Mission Fixes folder, laid out as the
    /// game folder is) and SR2_SCRIPTS_COMPARE (the scripts to match) set: every script of the committed recipe builds to those bytes.</summary>
    private static void CommittedRecipeBuildsTheRealScripts()
    {
        var game = Environment.GetEnvironmentVariable(RealGameVariable);
        var expected = Environment.GetEnvironmentVariable(RealScriptsVariable);
        if (string.IsNullOrEmpty(game) || string.IsNullOrEmpty(expected))
        {
            Console.WriteLine($"skip the real game files check (set {RealGameVariable} and {RealScriptsVariable})");
            return;
        }
        var delta = ScriptDelta.Load(Path.Combine(RepoPaths.RepoRoot, CommittedRecipe));
        Check.That(delta.Scripts.Count == RealScriptCount, $"the committed recipe has {RealScriptCount} scripts");
        var same = delta.Scripts.Count(script => delta.Build(script.Name, game).SequenceEqual(File.ReadAllBytes(Path.Combine(expected, script.Name))));
        Check.That(same == delta.Scripts.Count, $"every script of the committed recipe builds to the generated bytes ({same} of {delta.Scripts.Count})");
        Console.WriteLine($"real game files: {same} of {delta.Scripts.Count} scripts byte-identical");
    }
}
