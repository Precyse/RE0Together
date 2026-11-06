using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Keys of the adapter ini that the adapter reads only when the game starts (the profile's <c>bootKeys</c>): a game that
/// already runs can be given a session only when it started with the values this session would write. The ini on disk is what the game
/// started with as long as it was not written since the process started; a file written later, or a start time that cannot be read,
/// is not known, so the game must be restarted.</summary>
public static class BootSettings
{
    /// <summary>True when the game runs and may have started with other values of the profile's boot keys than the session
    /// <paramref name="options"/> (<see cref="GameOptions.ForAdapter"/>) writes.</summary>
    public static bool Blocks(GameProfile profile, string? gameDir, IReadOnlyDictionary<string, string> options)
    {
        if (profile.BootKeys is not { Count: > 0 } || !GameLauncher.IsRunning(profile)) return false;
        if (gameDir == null) return true;
        var written = new Dictionary<string, string>(options) { [AdapterSettings.CoopKey] = "1" };
        return Differ(profile.BootKeys, written, AdapterSettings.ReadKeys(gameDir, profile), AdapterSettings.IniWrittenUtc(gameDir, profile), GameLauncher.StartedAtUtc(profile));
    }

    /// <param name="written">The keys the session writes; a boot key it does not write keeps the value it has.</param>
    /// <param name="onDisk">The ini's keys now, null when there is no ini.</param>
    public static bool Differ(IReadOnlyList<string> bootKeys, IReadOnlyDictionary<string, string> written, IReadOnlyDictionary<string, string>? onDisk,
        DateTime? iniWrittenUtc, DateTime? gameStartedUtc)
    {
        var compared = bootKeys.Where(written.ContainsKey).ToList();
        if (compared.Count == 0) return false;
        if (onDisk == null || iniWrittenUtc == null || gameStartedUtc == null || iniWrittenUtc > gameStartedUtc) return true;
        return compared.Any(key => onDisk.GetValueOrDefault(key, string.Empty) != written[key]);
    }
}
