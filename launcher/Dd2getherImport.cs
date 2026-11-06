using System.Globalization;
using System.Text.Json;

namespace CoopLauncher;

/// <summary>Brings DD2gether's saves into the launcher's campaigns (<see cref="CampaignStore"/>), once each, for a profile with
/// <c>campaigns.importDd2gether</c>: its data folder (<see cref="DataFolder"/>) where DD2gether runs from, or any copy of it under
/// the game's coop/archive (where it is moved when it is disabled). A co-op campaign (<c>owned_campaigns/&lt;id&gt;/campaign.json</c>)
/// comes with its newest checkpoint's save (<c>internal/checkpoints/campaigns/&lt;id&gt;/&lt;n&gt;/save</c>), for hosting; a character
/// profile (<c>profiles/&lt;id&gt;/profile.json</c>) with its own save, a campaign whose character a guest can bring once it was
/// played. Nothing of DD2gether's is changed.</summary>
public static class Dd2getherImport
{
    public const string SourcePrefix = "dd2gether:";
    public const string ProfileSourcePrefix = "dd2gether-profile:";
    private const string DataFolder = "reframework/data/dd2gether";
    private const string ArchiveFolder = "coop/archive";
    private const string OwnedFolder = "owned_campaigns";
    private const string OwnedInfo = "campaign.json";
    private const string CheckpointsFolder = "internal/checkpoints/campaigns";
    private const string CheckpointSave = "save";
    private const string ProfilesFolder = "profiles";
    private const string ProfileInfo = "profile.json";
    private const string ProfileSave = "save";

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    private sealed record Named(string? Name);

    /// <summary>Whether this game folder holds DD2gether's data, live or archived.</summary>
    public static bool Available(GameProfile profile, string? gameDir) =>
        profile.Campaigns is { ImportDd2gether: true } && gameDir != null && DataFolders(gameDir).Any();

    /// <summary>Imports every DD2gether campaign and character profile not imported yet; returns how many were.</summary>
    public static int Run(GameProfile profile, string gameDir)
    {
        if (profile.Campaigns is not { ImportDd2gether: true }) return 0;
        var imported = CampaignStore.List(profile, gameDir).Select(campaign => campaign.Source).OfType<string>().ToHashSet();
        var count = 0;
        foreach (var data in DataFolders(gameDir))
        {
            count += ImportEach(profile, gameDir, imported, Path.Combine(data, OwnedFolder), OwnedInfo, SourcePrefix, id => NewestCheckpoint(data, id));
            count += ImportEach(profile, gameDir, imported, Path.Combine(data, ProfilesFolder), ProfileInfo, ProfileSourcePrefix,
                id => Path.Combine(data, ProfilesFolder, id, ProfileSave) is var save && Directory.Exists(save) ? save : null);
        }
        return count;
    }

    /// <summary>Each folder of <paramref name="parent"/> whose source is new and that has a save becomes a campaign named from its
    /// info file.</summary>
    private static int ImportEach(GameProfile profile, string gameDir, HashSet<string> imported, string parent, string infoFile, string prefix,
        Func<string, string?> saveOf)
    {
        if (!Directory.Exists(parent)) return 0;
        var count = 0;
        foreach (var folder in Directory.GetDirectories(parent))
        {
            var id = Path.GetFileName(folder);
            if (imported.Contains(prefix + id) || saveOf(id) is not { } save) continue;
            try
            {
                var name = JsonSerializer.Deserialize<Named>(File.ReadAllText(Path.Combine(folder, infoFile)), JsonOptions)?.Name ?? id;
                CampaignStore.Create(profile, gameDir, name, save, prefix + id);
                imported.Add(prefix + id);
                ++count;
            }
            catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
            {
                Log.Info($"DD2gether save {id} not imported: {e.Message}");
            }
        }
        return count;
    }

    /// <summary>The live data folder and every archived copy of it.</summary>
    private static IEnumerable<string> DataFolders(string gameDir)
    {
        var live = Path.Combine(gameDir, DataFolder);
        if (Directory.Exists(live)) yield return live;
        var archive = Path.Combine(gameDir, ArchiveFolder);
        if (!Directory.Exists(archive)) yield break;
        foreach (var copy in Directory.GetDirectories(archive).Select(folder => Path.Combine(folder, DataFolder)).Where(Directory.Exists))
            yield return copy;
    }

    /// <summary>The save folder of the highest numbered checkpoint of a campaign, or null when it has none.</summary>
    private static string? NewestCheckpoint(string data, string id)
    {
        var checkpoints = Path.Combine(data, CheckpointsFolder, id);
        if (!Directory.Exists(checkpoints)) return null;
        return Directory.GetDirectories(checkpoints)
            .Select(folder => (Folder: folder, Number: int.TryParse(Path.GetFileName(folder), NumberStyles.None, CultureInfo.InvariantCulture, out var n) ? n : -1))
            .Where(pair => pair.Number >= 0 && Directory.Exists(Path.Combine(pair.Folder, CheckpointSave)))
            .OrderByDescending(pair => pair.Number)
            .Select(pair => Path.Combine(pair.Folder, CheckpointSave))
            .FirstOrDefault();
    }
}
