using System.Text.Json;
using System.Text.Json.Serialization;

namespace CoopLauncher;

public sealed record AdapterFile(string Src, string Dst);

/// <summary>Save sync settings. Fixed files come from the Steam cloud folder (SteamRemoteFiles). A game that keeps its
/// saves elsewhere names the folders as templates ({documents}, {steamid64}; see SavePaths) and a FilePattern: the host
/// then sends whatever matches, announced first by a manifest.</summary>
public sealed record SaveSyncProfile(
    List<string> SteamRemoteFiles, string SessionDir,
    string? HostSaveDir = null, string? GuestSaveDir = null, string? FilePattern = null);

/// <summary>Where a game's co-op session lives. A Lobby game uses the launcher's Steam lobby, transport and save sync; a
/// Native game matches players itself, so the launcher only installs the mod, writes the options and starts the game; a
/// Rendezvous game is Native with a Steam lobby that says who hosts: the mod connects itself, so there is no bridge, no
/// frames between launchers and no save sync, unless the profile's BridgeWhen condition holds for the host's options.</summary>
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum SessionKind { Lobby, Native, Rendezvous }

/// <summary>A file the game folder must hold for co-op to work, and the name the status line gives it when it is missing.</summary>
public sealed record RequiredFile(string File, string Name);

/// <summary>When a Rendezvous game also builds the bridge: the host's value of option Key equals Value (SR2: transport=steam).</summary>
public sealed record BridgeCondition(string Key, string Value);

/// <summary>One value of a game option. A value with a DlcAppId is offered only when the game folder holds that DLC.</summary>
public sealed record OptionValue(string Value, string Label, string? DlcAppId = null);

/// <summary>A per-game choice shown as a dropdown and written to the adapter ini under Key; the first value is the
/// default. A Host option is the host's alone (the guest takes what follows from it).</summary>
public sealed record GameOption(string Key, string Label, List<OptionValue> Values, bool Host = false)
{
    private const string DlcFolder = "DLC";

    public string Default => Values[0].Value;

    public IEnumerable<OptionValue> AvailableIn(string? gameDir) =>
        Values.Where(v => v.DlcAppId == null || (gameDir != null && Directory.Exists(Path.Combine(gameDir, DlcFolder, v.DlcAppId))));
}

public sealed record GameProfile(
    string Id, string Name, int SteamAppId, string Exe, int MaxPlayers, int Port,
    List<AdapterFile> AdapterFiles, string AdapterIni, SaveSyncProfile? SaveSync = null, string? AdapterLog = null,
    List<string>? Characters = null, List<int>? SupportedBuilds = null, List<GameOption>? Options = null,
    SessionKind SessionKind = SessionKind.Lobby, List<RequiredFile>? Requires = null,
    string? JoinFile = null, string? StatusFile = null, BridgeCondition? BridgeWhen = null)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    public bool NativeSession => SessionKind == SessionKind.Native;

    /// <summary>A Steam lobby carries who hosts (JoinFile, written by NativeJoin) and the mod reports back (StatusFile).</summary>
    public bool RendezvousSession => SessionKind == SessionKind.Rendezvous;

    /// <summary>Whether the session has the bridge, frames between launchers and the adapter link: always for a Lobby game,
    /// for a Rendezvous game when the host's option values meet BridgeWhen.</summary>
    public bool Bridged(IReadOnlyDictionary<string, string> hostValues) =>
        SessionKind == SessionKind.Lobby ||
        (RendezvousSession && BridgeWhen is { } condition && hostValues.TryGetValue(condition.Key, out var value) && value == condition.Value);

    /// <summary>The name of the first required file missing from the game folder, or null when all are there.</summary>
    public string? MissingRequirement(string gameDir) =>
        Requires?.FirstOrDefault(required => !File.Exists(Path.Combine(gameDir, required.File)))?.Name;

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
