namespace CoopLauncher;

/// <summary>Reads a game's save slots from this machine: the host's own save (where the save sync reads it) or the copy a guest received.
/// The key comes from the installed game's executable, read only. A result is kept until one of the files changes, so asking again
/// (the window asks on every update) costs a file check. A save that is not there yet is no slots and no failure; a save that cannot be
/// decoded is no slots and one log line saying why.</summary>
public static class SaveIndexFiles
{
    private const string HexPrefix = "0x";
    private const byte FirstPrintable = 0x20;
    private const byte LastPrintable = 0x7E;

    private static readonly object CacheLock = new();
    private static readonly Dictionary<string, (string Stamp, SaveIndex Index)> Cache = [];

    /// <summary>The slots of the save this machine's player hosts with.</summary>
    public static SaveIndex ReadHost(GameProfile profile, string? gameDir)
    {
        if (profile.SaveIndex is not { } layout || profile.SaveSync is not { } config) return SaveIndex.Empty;
        var folder = SaveSyncCoordinator.HostSaveDir(profile, config, SavePaths.SteamAccountId());
        return folder == null ? SaveIndex.Empty : Read(profile, layout, gameDir, Path.Combine(folder, layout.File));
    }

    /// <summary>The slots of the copy of the host's save that sits in the session folder of this guest.</summary>
    public static SaveIndex ReadSession(GameProfile profile, string? gameDir)
    {
        if (profile.SaveIndex is not { } layout || profile.SaveSync is not { } config || gameDir == null) return SaveIndex.Empty;
        return Read(profile, layout, gameDir, Path.Combine(gameDir, config.SessionDir, layout.File));
    }

    private static SaveIndex Read(GameProfile profile, SaveIndexProfile layout, string? gameDir, string savePath)
    {
        var exePath = gameDir == null ? null : Path.Combine(gameDir, profile.Exe);
        if (exePath == null || !File.Exists(exePath) || !File.Exists(savePath)) return SaveIndex.Empty;
        var stamp = $"{Stamp(savePath)}|{Stamp(exePath)}";
        lock (CacheLock)
        {
            if (Cache.TryGetValue(savePath, out var known) && known.Stamp == stamp) return known.Index;
            byte[] file;
            try
            {
                file = File.ReadAllBytes(savePath);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Log.Info($"Save slots of {profile.Name} not listed: {savePath} could not be read ({e.Message})");
                return SaveIndex.Empty;
            }
            var index = Decode(layout, file, exePath);
            if (index.Failure != null) Log.Info($"Save slots of {profile.Name} not listed: {index.Failure}");
            Cache[savePath] = (stamp, index);
            return index;
        }
    }

    private static SaveIndex Decode(SaveIndexProfile layout, byte[] file, string exePath)
    {
        if (ReadKey(layout, exePath) is not { } key) return SaveIndex.Failed($"the cipher key is not in {Path.GetFileName(exePath)} where this profile expects it");
        return key.All(IsPrintable)
            ? SaveIndex.Parse(file, key, layout)
            : SaveIndex.Failed($"the cipher key read from {Path.GetFileName(exePath)} is not text: another game build");
    }

    private static byte[]? ReadKey(SaveIndexProfile layout, string exePath)
    {
        var text = layout.KeyAddress.StartsWith(HexPrefix, StringComparison.OrdinalIgnoreCase) ? layout.KeyAddress[HexPrefix.Length..] : layout.KeyAddress;
        return uint.TryParse(text, System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out var address)
            ? PeImage.ReadAt(exePath, address, layout.KeyLength)
            : null;
    }

    private static bool IsPrintable(byte value) => value is >= FirstPrintable and <= LastPrintable;

    private static string Stamp(string path)
    {
        var info = new FileInfo(path);
        return $"{info.Length}/{info.LastWriteTimeUtc.Ticks}";
    }
}
