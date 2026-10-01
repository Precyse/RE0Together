using System.Text.Json;

namespace CoopLauncher;

public sealed record AdapterFile(string Src, string Dst);

/// <summary>Save sync settings. Fixed files come from the Steam cloud folder (SteamRemoteFiles). A game that keeps its
/// saves elsewhere names the folders as templates ({documents}, {steamid64}; see SavePaths) and a FilePattern: the host
/// then sends whatever matches, announced first by a manifest.</summary>
public sealed record SaveSyncProfile(
    List<string> SteamRemoteFiles, string SessionDir, string AdapterIni,
    string? HostSaveDir = null, string? GuestSaveDir = null, string? FilePattern = null);

public sealed record GameProfile(
    string Id, string Name, int SteamAppId, string Exe, int MaxPlayers, int Port,
    List<AdapterFile> AdapterFiles, SaveSyncProfile? SaveSync = null, string? AdapterLog = null)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    public static GameProfile Load(string id)
    {
        var path = Path.Combine(RepoPaths.GamesDir, id + ".json");
        if (!File.Exists(path)) throw new FileNotFoundException($"No game profile for '{id}'", path);
        return JsonSerializer.Deserialize<GameProfile>(File.ReadAllText(path), JsonOptions)
               ?? throw new InvalidDataException($"Empty profile {path}");
    }

    public static IReadOnlyList<string> ListIds() =>
        Directory.GetFiles(RepoPaths.GamesDir, "*.json").Select(Path.GetFileNameWithoutExtension).ToList()!;
}
