using System.Text.Json;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>A game's campaigns (profile key <c>campaigns</c>): named worlds kept in the game folder, each a folder
/// <c>&lt;dir&gt;\&lt;id&gt;\</c> with <see cref="InfoFile"/> (name, creation time, where it came from), the world's saves in
/// <c>saveFolder</c> and the character the adapter captured from it (<c>characterFile</c>). A host plays one by its id (the
/// <c>campaign</c> option: the adapter redirects the game's saves there and the save sync sends from there); a guest brings the
/// character of one (the <c>arisen</c> option). The game's own save is never touched.</summary>
public static partial class CampaignStore
{
    public const string InfoFile = "campaign.json";
    private const string DefaultName = "Campaign";
    private const string FallbackId = "campaign";
    private const int MaxIdLength = 48;
    private const int LevelLine = 2;  // after "dd2p <format>" and "tdb <hash>"
    private const string LevelWord = "level";

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };

    /// <summary>One campaign: its id (the folder name), its name, when it was made and its origin (null: made here).</summary>
    public sealed record Campaign(string Id, string Name, DateTime CreatedUtc, string? Source = null);

    /// <summary>The campaigns in the game folder, by name; a folder without a readable info file is skipped.</summary>
    public static IReadOnlyList<Campaign> List(GameProfile profile, string? gameDir)
    {
        if (profile.Campaigns is not { } config || gameDir == null) return [];
        var root = Path.Combine(gameDir, config.Dir);
        if (!Directory.Exists(root)) return [];
        var campaigns = new List<Campaign>();
        foreach (var folder in Directory.GetDirectories(root))
        {
            try
            {
                if (JsonSerializer.Deserialize<Campaign>(File.ReadAllText(Path.Combine(folder, InfoFile)), JsonOptions) is { } read)
                    campaigns.Add(read with { Id = Path.GetFileName(folder) });
            }
            catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
            {
                Log.Info($"Campaign folder {folder} skipped: {e.Message}");
            }
        }
        return campaigns.OrderBy(campaign => campaign.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    /// <summary>Makes a campaign named <paramref name="name"/> under a fresh id; its world starts as a copy of
    /// <paramref name="copyFrom"/>'s files (null: empty, so the game starts a new one). Throws IOException when it cannot be written.</summary>
    public static Campaign Create(GameProfile profile, string gameDir, string name, string? copyFrom = null, string? source = null)
    {
        var config = profile.Campaigns ?? throw new InvalidOperationException($"{profile.Name} has no campaigns");
        var root = Path.Combine(gameDir, config.Dir);
        var id = UniqueId(Slug(name), id => Directory.Exists(Path.Combine(root, id)));
        var campaign = new Campaign(id, name, DateTime.UtcNow, source);
        var saveDir = SaveDir(profile, gameDir, id);
        Directory.CreateDirectory(saveDir);
        if (copyFrom != null && Directory.Exists(copyFrom))
        {
            foreach (var file in Directory.GetFiles(copyFrom)) File.Copy(file, Path.Combine(saveDir, Path.GetFileName(file)));
        }
        File.WriteAllText(Path.Combine(root, id, InfoFile), JsonSerializer.Serialize(campaign with { Id = string.Empty }, JsonOptions));
        Log.Info($"Campaign {name} ({id}) made in {Path.Combine(root, id)}{(copyFrom != null ? $" from {copyFrom}" : string.Empty)}");
        return campaign;
    }

    /// <summary>The folder of a campaign's world saves.</summary>
    public static string SaveDir(GameProfile profile, string gameDir, string id) =>
        Path.Combine(gameDir, profile.Campaigns!.Dir, id, profile.Campaigns.SaveFolder);

    /// <summary>The character file the adapter keeps for a campaign.</summary>
    public static string CharacterFile(GameProfile profile, string gameDir, string id) =>
        Path.Combine(gameDir, profile.Campaigns!.Dir, id, profile.Campaigns.CharacterFile);

    /// <summary>The Arisen's level in a character file (its third line, <c>level N job N equip N</c>, the adapter's profile_rule.h), or
    /// null when there is no file or it cannot be read.</summary>
    public static int? CharacterLevel(string characterFile)
    {
        try
        {
            if (!File.Exists(characterFile)) return null;
            var header = File.ReadLines(characterFile).Skip(LevelLine).FirstOrDefault()?.Split(' ');
            return header is [LevelWord, var text, ..] && int.TryParse(text, out var level) ? level : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <summary>The first free "Campaign N" name.</summary>
    public static string NextName(IEnumerable<Campaign> campaigns)
    {
        var taken = campaigns.Select(campaign => campaign.Name).ToHashSet(StringComparer.CurrentCultureIgnoreCase);
        return Enumerable.Range(1, taken.Count + 1).Select(n => $"{DefaultName} {n}").First(name => !taken.Contains(name));
    }

    /// <summary>A folder id from a name: lower case letters, digits and single dashes (what the adapter accepts), at most 48.</summary>
    internal static string Slug(string name)
    {
        var slug = NonIdRun().Replace(name.ToLowerInvariant(), "-").Trim('-');
        if (slug.Length > MaxIdLength) slug = slug[..MaxIdLength].TrimEnd('-');
        return slug.Length == 0 ? FallbackId : slug;
    }

    /// <summary><paramref name="slug"/>, or slug-2, slug-3 ... when it is taken.</summary>
    internal static string UniqueId(string slug, Func<string, bool> taken)
    {
        if (!taken(slug)) return slug;
        return Enumerable.Range(2, int.MaxValue - 2).Select(n => $"{slug}-{n}").First(id => !taken(id));
    }

    [GeneratedRegex("[^a-z0-9]+")]
    private static partial Regex NonIdRun();
}
