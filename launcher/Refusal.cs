using System.Globalization;

namespace CoopLauncher;

/// <summary>What differs between the host's and this machine's setup, in the order it is checked.</summary>
public enum BuildMismatch { None, LauncherBuild, GameBuild, ModFiles, ModNotInstalled }

/// <summary>Why a start (host, join, rejoin) did not go on; the window turns it into words and, where there is one, a fix.</summary>
public enum RefusalKind
{
    LauncherBuild, GameBuild, ModFiles, ModNotInstalled, GameUnsupported, GameNotFound, RequirementMissing,
    UnknownGame, LobbyFull, LobbyClosed, LobbyNotAllowed, LobbyFailed, NotCoopLobby, SteamOffline, SaveTimedOut,
    SaveFolderMissing, SaveDiffers, HostLeft, RejoinGaveUp, PortInUse, LaunchFailed,
}

/// <summary>A refused start: the kind, a detail for the words that carry one (build number, file name, port), the game it
/// concerns and the lobby it was tried on (0 when none).</summary>
public sealed record Refusal(RefusalKind Kind, string Detail = "", string? GameId = null, ulong LobbyId = 0);

/// <summary>A lobby that cannot be used: the refusal kind it maps to, the diagnostic line for the log and the refusal's
/// detail where its words carry one (the host's build for LauncherBuild).</summary>
public sealed record LobbyFailure(RefusalKind Kind, string Message, string Detail = "");

/// <summary>The detail of a build refusal (LauncherBuild, GameBuild, ModFiles): the host's number and the local one,
/// <c>"&lt;host&gt;/&lt;you&gt;"</c>.</summary>
public static class RefusalVersions
{
    private const char Separator = '/';

    public static string Format(int host, int you) => string.Create(CultureInfo.InvariantCulture, $"{host}{Separator}{you}");

    /// <summary>False for text that is not two whole numbers around the separator (a refusal of another kind, an empty detail).</summary>
    public static bool TryParse(string detail, out int host, out int you)
    {
        host = you = 0;
        var parts = detail.Split(Separator);
        return parts.Length == 2
            && int.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out host)
            && int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out you);
    }
}

/// <summary>Pure maps from what Steam and the build check report to a <see cref="RefusalKind"/>. Codes are the Steamworks
/// enum values as numbers so this file needs no Steamworks types.</summary>
public static class Refusals
{
    private const uint EnterDoesntExist = 2;
    private const uint EnterNotAllowed = 3;
    private const uint EnterFull = 4;
    private const uint EnterFirstBlocked = 6;
    private const uint EnterLastBlocked = 11;

    private const int ResultFail = 2;
    private const int ResultNoConnection = 3;
    private const int ResultTimeout = 16;
    private const int ResultServiceUnavailable = 20;

    public static RefusalKind FromBuildMismatch(BuildMismatch mismatch) => mismatch switch
    {
        BuildMismatch.LauncherBuild => RefusalKind.LauncherBuild,
        BuildMismatch.GameBuild => RefusalKind.GameBuild,
        BuildMismatch.ModFiles => RefusalKind.ModFiles,
        BuildMismatch.ModNotInstalled => RefusalKind.ModNotInstalled,
        _ => throw new ArgumentOutOfRangeException(nameof(mismatch), mismatch, "No mismatch to refuse"),
    };

    /// <summary>EChatRoomEnterResponse of a lobby join that Steam answered.</summary>
    public static RefusalKind FromEnterResponse(uint response) => response switch
    {
        EnterDoesntExist => RefusalKind.LobbyClosed,
        EnterFull => RefusalKind.LobbyFull,
        EnterNotAllowed => RefusalKind.LobbyNotAllowed,
        >= EnterFirstBlocked and <= EnterLastBlocked => RefusalKind.LobbyNotAllowed,
        _ => RefusalKind.LobbyFailed,
    };

    /// <summary>EResult of a lobby creation that Steam answered.</summary>
    public static RefusalKind FromCreateResult(int eResult) => eResult switch
    {
        ResultNoConnection or ResultTimeout or ResultServiceUnavailable or ResultFail => RefusalKind.SteamOffline,
        _ => RefusalKind.LobbyFailed,
    };
}
