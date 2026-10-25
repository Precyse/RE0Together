using System.Collections.Concurrent;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CoopLauncher;

/// <summary>A file the game folder gets: Src in the package, Dst in the game folder. With Folder set both name a folder; the profile
/// load replaces it by one entry per file (see <see cref="ModFiles.Expand"/>), each with the folder's Dst in FolderDst. With Generate
/// set (<c>"delta"</c>) Src is a script recipe (<see cref="ScriptDelta"/>) and Dst a folder: the profile load replaces it by one entry
/// per script of the recipe, each with the script's name in Member, and the install builds the script from the player's own files.</summary>
public sealed record AdapterFile(string Src, string Dst, bool Folder = false, string? FolderDst = null, string? Generate = null, string? Member = null);

/// <summary>Save sync settings. Fixed files come from the Steam cloud folder (SteamRemoteFiles). A game that keeps its
/// saves elsewhere names the folders as templates ({documents}, {steamid64}; see SavePaths) and a FilePattern: the host
/// then sends whatever matches, announced first by a manifest.</summary>
public sealed record SaveSyncProfile(
    List<string> SteamRemoteFiles, string SessionDir,
    string? HostSaveDir = null, string? GuestSaveDir = null, string? FilePattern = null);

/// <summary>Where a game keeps its save slots: the save file (in the save folder, and in the session folder on a guest), where the executable
/// holds the file's cipher key (a virtual address as hex text, KeyLength bytes), the name of each slot's entry (<c>{0}</c> is the slot
/// number), how many slots there are and how big each one is. The key is read from the player's own game at run time, never shipped.</summary>
public sealed record SaveIndexProfile(string File, string KeyAddress, int KeyLength, string Entry, int Slots, int SlotSize);

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

/// <summary>Where a picture of an option value is in the game folder: an archive (the path may hold {value}), the texture
/// in it, and for a picture that is not a cutout the part to keep (x, y, width, height) and how many pixels of its edges
/// fade out.</summary>
public sealed record ArtSource(string Arc, string Texture, List<int>? Crop = null, int Feather = 0);

/// <summary>Where the game's own UI art for the in-game overlay is in the game folder: the archive holding the message font's
/// texture and layout (<see cref="Gui.UiArt"/> writes them to coop/ui at launch) and the archive and texture of the dialog panel.</summary>
public sealed record UiArtSource(string FontArc, string FontTexture, string FontGfd, string DialogArc, string DialogTexture);

/// <summary>One value of a game option. A value with a DlcAppId is offered only when the game folder holds that DLC. Art
/// replaces the profile's OutfitArt for this value. A value of the character option may name the character in full (FullName) and
/// the unit it belongs to (Unit), which the player cards show.</summary>
public sealed record OptionValue(string Value, string Label, string? DlcAppId = null, ArtSource? Art = null, string? FullName = null, string? Unit = null);

/// <summary>A per-game choice shown as a dropdown and written to the adapter ini under Key; the first value is the
/// default. A Host option is the host's alone (the guest takes what follows from it). A Settings option is shown in the
/// settings view, not in the session's options grid. An option with a Character is the outfit of that character (a value
/// of the game's character option. An option with a Source takes its values from this machine at run time (<see cref="OptionSources"/>):
/// Values are the ones always offered (the first is the default), the grids do not list it, and a guest takes the host's value as it is.</summary>
public sealed record GameOption(string Key, string Label, List<OptionValue> Values, bool Host = false, bool Settings = false,
    string? Character = null, string? Source = null)
{
    private const string DlcFolder = "DLC";

    public string Default => Values[0].Value;

    /// <summary>The value whose picture shows an outfit choice: the choice itself, except the saved outfit (the empty value, "use
    /// the outfit in the save"), which is shown as the first value without a DLC. Null stays null (not known).</summary>
    public string? PictureOf(string? value) =>
        value == string.Empty ? Values.FirstOrDefault(v => v.Value.Length > 0 && v.DlcAppId == null)?.Value ?? value : value;

    public IEnumerable<OptionValue> AvailableIn(string? gameDir) =>
        Values.Where(v => v.DlcAppId == null || (gameDir != null && Directory.Exists(Path.Combine(gameDir, DlcFolder, v.DlcAppId))));
}

public sealed record GameProfile(
    string Id, string Name, int SteamAppId, string Exe, int MaxPlayers, int Port,
    List<AdapterFile> AdapterFiles, string AdapterIni, SaveSyncProfile? SaveSync = null, string? AdapterLog = null,
    List<string>? Characters = null, List<int>? SupportedBuilds = null, List<GameOption>? Options = null,
    SessionKind SessionKind = SessionKind.Lobby, List<RequiredFile>? Requires = null,
    string? JoinFile = null, string? StatusFile = null, BridgeCondition? BridgeWhen = null,
    List<string>? FreshTypes = null, ArtSource? OutfitArt = null, List<string>? CrashFiles = null, SaveIndexProfile? SaveIndex = null, UiArtSource? UiArt = null)
{
    // UiArt: the game art the in-game overlay draws, taken from the player's own install at launch.
    // CrashFiles: where the game and its adapter leave crash files, relative to the game folder, as folder and pattern ("coop/dumps/*.dmp").
    // SaveIndex: where the save slots are (a game whose save file the launcher can list; the host picks a slot to load at launch).

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly ConcurrentDictionary<string, GameProfile> Loaded = new();
    private static readonly Lazy<IReadOnlyList<string>> Ids = new(ReadIds);

    public bool NativeSession => SessionKind == SessionKind.Native;

    /// <summary>The game lets each player pick a character and an outfit for it (the window's select view).</summary>
    public bool HasCharacterSelect => Options?.Any(option => option.Character != null) == true;

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

    /// <summary>The profile of a game, read once: the window and the loop thread share the instance. A profile that cannot be
    /// read (missing, empty, a bad fresh frame type) throws and is read again on the next call.</summary>
    public static GameProfile Load(string id) => Loaded.GetOrAdd(id, Read);

    public static IReadOnlyList<string> ListIds() => Ids.Value;

    private static GameProfile Read(string id)
    {
        var path = Path.Combine(RepoPaths.GamesDir, id + ".json");
        if (!File.Exists(path)) throw new FileNotFoundException($"No game profile for '{id}'", path);
        return Parse(id, File.ReadAllText(path), RepoPaths.RepoRoot);
    }

    /// <summary>A profile from its JSON, its folder entries expanded against <paramref name="root"/> (the package folder); the fresh
    /// frame types must be valid (the queue would refuse them mid-session).</summary>
    internal static GameProfile Parse(string id, string json, string root)
    {
        var profile = JsonSerializer.Deserialize<GameProfile>(json, JsonOptions) ?? throw new InvalidDataException($"Empty profile {id}");
        try
        {
            FreshQueue.ParseTypes(profile.FreshTypes);
        }
        catch (InvalidDataException e)
        {
            throw new InvalidDataException($"Profile {id}: {e.Message}", e);
        }
        return profile with { AdapterFiles = ModFiles.Expand(profile.AdapterFiles, root) };
    }

    private static IReadOnlyList<string> ReadIds() =>
        Directory.GetFiles(RepoPaths.GamesDir, "*.json").Select(Path.GetFileNameWithoutExtension).ToList()!;
}
