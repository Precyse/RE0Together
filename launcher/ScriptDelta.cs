using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace CoopLauncher;

/// <summary>A script that cannot be built on this PC: the recipe is damaged, or a file it starts from is missing or is not the
/// supported version. The message names the file; it is shown to the player as the reason an install was refused.</summary>
public sealed class ScriptDeltaException(string message) : IOException(message);

/// <summary>One step of a script's recipe: copy <c>Length</c> bytes from <c>Offset</c> of the source, or insert <c>Insert</c>.</summary>
public readonly record struct DeltaOp(long Offset, long Length, byte[]? Insert);

/// <summary>One script of a delta: the file it starts from (<c>patch.vpp_pc:name</c> for an entry of a packfile in the game
/// folder, else a path inside the game folder), that file's SHA-256 and size, and the SHA-256 and size of the finished script.</summary>
public sealed record DeltaScript(string Name, string Source, string SourceSha256, long SourceSize, string Sha256, long Size, IReadOnlyList<DeltaOp> Ops);

/// <summary>The recipe for the co-op scripts of a game whose own scripts the repo must not hold (<c>adapters/sr2/scripts.delta.json</c>,
/// written by <c>tools/sr2/lua_delta.py</c>): per script, copy and insert ops over the bytes of the player's own file. Building
/// checks the source against its pinned SHA-256 and the result against its own, so every player ends up with the same bytes or
/// is refused.</summary>
public sealed class ScriptDelta
{
    private const int SupportedFormat = 1;
    private const char PackSeparator = ':';
    private const string CopyOp = "c";
    private const string InsertOp = "i";
    private const int OpCopyLength = 3;
    private const int OpInsertLength = 2;

    private readonly Dictionary<string, DeltaScript> _byName;

    private ScriptDelta(List<DeltaScript> scripts)
    {
        Scripts = scripts;
        _byName = scripts.ToDictionary(script => script.Name, StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>The scripts in the order of the file (ordinal by name).</summary>
    public IReadOnlyList<DeltaScript> Scripts { get; }

    public DeltaScript? Find(string name) => _byName.GetValueOrDefault(name);

    /// <summary>Reads a delta file. Throws <see cref="ScriptDeltaException"/> for one that is not valid JSON of the supported format.</summary>
    public static ScriptDelta Load(string path)
    {
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllBytes(path));
            var root = document.RootElement;
            if (root.GetProperty("format").GetInt32() != SupportedFormat) throw new ScriptDeltaException($"{Path.GetFileName(path)} has an unsupported format");
            var scripts = root.GetProperty("files").EnumerateArray().Select(ReadScript).OrderBy(script => script.Name, StringComparer.Ordinal).ToList();
            return new ScriptDelta(scripts);
        }
        catch (Exception e) when (e is JsonException or KeyNotFoundException or InvalidOperationException or FormatException or ArgumentException)
        {
            throw new ScriptDeltaException($"{Path.GetFileName(path)} is damaged: {e.Message}");
        }
    }

    private static DeltaScript ReadScript(JsonElement element)
    {
        var name = element.GetProperty("name").GetString()!;
        if (name.Length == 0 || name != Path.GetFileName(name)) throw new FormatException($"'{name}' is not a plain file name");
        return new DeltaScript(
            name, element.GetProperty("source").GetString()!,
            element.GetProperty("sourceSha256").GetString()!.ToUpperInvariant(), element.GetProperty("sourceSize").GetInt64(),
            element.GetProperty("sha256").GetString()!.ToUpperInvariant(), element.GetProperty("size").GetInt64(),
            element.GetProperty("ops").EnumerateArray().Select(ReadOp).ToList());
    }

    private static DeltaOp ReadOp(JsonElement op)
    {
        var parts = op.EnumerateArray().ToList();
        return parts[0].GetString() switch
        {
            CopyOp when parts.Count == OpCopyLength => new DeltaOp(parts[1].GetInt64(), parts[2].GetInt64(), null),
            InsertOp when parts.Count == OpInsertLength => new DeltaOp(0, 0, Latin1Bytes(parts[1].GetString()!)),
            _ => throw new FormatException("an op is neither a copy nor an insert"),
        };
    }

    private static byte[] Latin1Bytes(string text) =>
        text.Any(character => character > byte.MaxValue) ? throw new FormatException("an insert holds a character beyond latin-1") : Encoding.Latin1.GetBytes(text);

    /// <summary>The finished script <paramref name="name"/>, built from the player's own files under <paramref name="gameDir"/>.
    /// Throws <see cref="ScriptDeltaException"/> naming the file when it is missing or not the supported version.</summary>
    public byte[] Build(string name, string gameDir)
    {
        var script = Find(name) ?? throw new ScriptDeltaException($"{name} is not part of the script recipe");
        var source = ReadSource(script.Source, gameDir);
        if (source.Length != script.SourceSize || Hash(source) != script.SourceSha256)
            throw new ScriptDeltaException($"{SourceName(script.Source)} in {SourceHome(script.Source)} is not the supported version");
        var result = Apply(script, source);
        if (result.Length != script.Size || Hash(result) != script.Sha256)
            throw new ScriptDeltaException($"{script.Name} does not build to its pinned bytes");
        return result;
    }

    private static byte[] Apply(DeltaScript script, byte[] source)
    {
        using var result = new MemoryStream((int)script.Size);
        foreach (var op in script.Ops)
        {
            if (op.Insert != null) result.Write(op.Insert);
            else if (op.Offset < 0 || op.Length < 0 || op.Offset + op.Length > source.Length)
                throw new ScriptDeltaException($"{script.Name} copies bytes beyond the end of {SourceName(script.Source)}");
            else result.Write(source, (int)op.Offset, (int)op.Length);
        }
        return result.ToArray();
    }

    private static byte[] ReadSource(string source, string gameDir)
    {
        var file = SourceName(source);
        try
        {
            if (!IsPackEntry(source)) return File.ReadAllBytes(Path.Combine(gameDir, source));
            return PackFile.ReadEntry(Path.Combine(gameDir, SourceHome(source)), file) ?? throw new ScriptDeltaException($"{file} is not in {SourceHome(source)}");
        }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
        {
            throw new ScriptDeltaException($"{file} is missing from {SourceHome(source)}");
        }
        catch (InvalidDataException e)
        {
            throw new ScriptDeltaException($"{SourceHome(source)} cannot be read: {e.Message}");
        }
    }

    private static bool IsPackEntry(string source) => source.Contains(PackSeparator);

    /// <summary>Where a source lives, as the player knows it: the packfile, or the folder holding the file.</summary>
    private static string SourceHome(string source) =>
        IsPackEntry(source) ? source[..source.IndexOf(PackSeparator)] : Path.GetDirectoryName(source) ?? "";

    private static string SourceName(string source) =>
        IsPackEntry(source) ? source[(source.IndexOf(PackSeparator) + 1)..] : Path.GetFileName(source);

    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));
}
