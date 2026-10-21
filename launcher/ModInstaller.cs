using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace CoopLauncher;

public enum ModState { NotInstalled, Installed, UpdateAvailable, Modified }

/// <summary>A game's mod as found in its game folder: the state and the package build recorded when it was installed (0 if unknown).</summary>
public readonly record struct ModStatus(ModState State, int Build);

/// <summary>
/// Installs and removes a game's mod: the profile's adapter files in the game folder. A file is ours when its ownership marker
/// (<c>.cfown</c>, holding the installed hash) records the file's own hash, or when it equals the package copy (an install that
/// was cut short before the marker). Any other file in the way is the player's: it is kept as <c>.cfbak</c> (<c>.cfbak2</c>, ...
/// when one exists, never overwritten) and the original <c>.cfbak</c> is restored on uninstall. A file that carries our marker
/// but another hash was replaced by another mod or damaged (<see cref="ModState.Modified"/>); install repairs it. The installed
/// package build is recorded beside the first file (<c>.cfbuild</c>). An install changes all files or none.
/// </summary>
public static class ModInstaller
{
    private const string BackupSuffix = ".cfbak";
    private const string OwnershipSuffix = ".cfown";
    private const string BuildSuffix = ".cfbuild";
    private const string TempSuffix = ".tmp";
    private const string PreviousSuffix = ".prev";
    private const byte NameEnd = 0;

    private static bool _hashFailureLogged;

    private readonly record struct ModFile(string Src, string Dst)
    {
        public string Marker => Dst + OwnershipSuffix;
    }

    /// <summary>Installed when every adapter file carries the marker and matches it; modified when one does not match its marker;
    /// an update is available when a package copy differs from the installed one.</summary>
    public static ModStatus Status(GameProfile profile, string? gameDir)
    {
        if (gameDir == null) return new ModStatus(ModState.NotInstalled, 0);
        var files = Files(profile, gameDir);
        if (files.Count == 0 || !files.All(f => File.Exists(f.Dst) && File.Exists(f.Marker)))
            return new ModStatus(ModState.NotInstalled, 0);
        try
        {
            var modified = false;
            var stale = false;
            foreach (var file in files)
            {
                var installed = Hash(file.Dst);
                modified |= installed != ReadMarker(file);
                stale |= File.Exists(file.Src) && Hash(file.Src) != installed;
            }
            var state = modified ? ModState.Modified : stale ? ModState.UpdateAvailable : ModState.Installed;
            return new ModStatus(state, ReadBuild(files[0].Dst));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {profile.Name} mod: {e.Message}");
            return new ModStatus(ModState.NotInstalled, 0);
        }
    }

    /// <summary>SHA-256 over the profile's adapter files as installed in the game folder (what runs), in profile order: per file the
    /// lowercase destination name with '/' separators, a 0x00 byte, the u64 little-endian length and the bytes. Null when a file is
    /// missing or unreadable (an unreadable one is logged once).</summary>
    public static byte[]? ContentHash(GameProfile profile, string gameDir)
    {
        try
        {
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            foreach (var (adapter, file) in profile.AdapterFiles.Zip(Files(profile, gameDir)))
            {
                if (!File.Exists(file.Dst)) return null;
                var bytes = File.ReadAllBytes(file.Dst);
                var length = new byte[sizeof(ulong)];
                BinaryPrimitives.WriteUInt64LittleEndian(length, (ulong)bytes.Length);
                hash.AppendData(Encoding.UTF8.GetBytes(adapter.Dst.Replace('\\', '/').ToLowerInvariant()));
                hash.AppendData([NameEnd]);
                hash.AppendData(length);
                hash.AppendData(bytes);
            }
            return hash.GetHashAndReset();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            if (_hashFailureLogged) return null;
            _hashFailureLogged = true;
            Log.Info($"Could not read the {profile.Name} mod to compare it: {e.Message}");
            return null;
        }
    }

    /// <summary>Copies the package's adapter files into the game folder (also how an update or a repair re-copies them). All files
    /// are staged beside their targets first; a failure while moving them into place puts every file back.</summary>
    public static bool Install(GameProfile profile, string gameDir)
    {
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before installing its mod");
            return false;
        }
        var files = Files(profile, gameDir);
        var missing = files.Where(f => !File.Exists(f.Src)).Select(f => f.Src).FirstOrDefault();
        if (missing != null)
        {
            Log.Info($"Mod files missing from the package: {missing}");
            return false;
        }
        var placements = new List<Placement>();
        try
        {
            placements.AddRange(files.Select(Plan).OfType<Placement>());
            foreach (var placement in placements) placement.Stage();
            foreach (var placement in placements) placement.Place();
            WriteBuild(files[0]);
            foreach (var placement in placements) placement.Commit();
            Log.Info($"Installed the {profile.Name} mod");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            foreach (var placement in Enumerable.Reverse(placements)) placement.Undo();
            Log.Info($"Could not install the {profile.Name} mod: {e.Message}");
            return false;
        }
    }

    /// <summary>Removes the adapter files the launcher installed, puts back any original it backed up, and deletes the game's
    /// coop folder (adapter ini, logs, session) except its archive of saved sessions; the session's saves are archived first.</summary>
    public static bool Uninstall(GameProfile profile, string gameDir)
    {
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before removing its mod");
            return false;
        }
        try
        {
            var files = Files(profile, gameDir);
            foreach (var file in files) Remove(file);
            File.Delete(files[0].Dst + BuildSuffix);
            RemoveCoopFolder(profile, gameDir);
            Log.Info($"Removed the {profile.Name} mod");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the {profile.Name} mod: {e.Message}");
            return false;
        }
    }

    private static List<ModFile> Files(GameProfile profile, string gameDir) =>
        profile.AdapterFiles.Select(f => new ModFile(Path.Combine(RepoPaths.RepoRoot, f.Src), Path.Combine(gameDir, f.Dst))).ToList();

    /// <summary>What installing this file needs, or null when it is already the package copy with its marker. A file in the way
    /// that is not ours is backed up first.</summary>
    private static Placement? Plan(ModFile file)
    {
        var packageHash = Hash(file.Src);
        if (!File.Exists(file.Dst)) return new Placement(file, packageHash, backupPath: null);
        var installedHash = Hash(file.Dst);
        var markerHash = ReadMarker(file);
        if (installedHash == packageHash && markerHash == packageHash) return null;
        var ours = installedHash == packageHash || installedHash == markerHash;
        return new Placement(file, packageHash, ours ? null : NextBackupPath(file.Dst));
    }

    private static string NextBackupPath(string dst)
    {
        var path = dst + BackupSuffix;
        for (var number = 2; File.Exists(path); number++) path = $"{dst}{BackupSuffix}{number}";
        return path;
    }

    private static void Remove(ModFile file)
    {
        if (File.Exists(file.Dst) && !IsOurs(file))
            Log.Info($"{file.Dst} was replaced by another mod, leaving it in place");
        else
        {
            File.Delete(file.Dst);
            var backup = file.Dst + BackupSuffix;
            if (File.Exists(backup)) File.Move(backup, file.Dst);
        }
        File.Delete(file.Marker);
    }

    private static bool IsOurs(ModFile file)
    {
        var installedHash = Hash(file.Dst);
        return installedHash == ReadMarker(file) || (File.Exists(file.Src) && installedHash == Hash(file.Src));
    }

    /// <summary>Deletes the coop folder's content except the archive. The session's saves are archived first (the same reset as
    /// after a session); a session folder that could not be archived is kept, so no save is ever lost.</summary>
    private static void RemoveCoopFolder(GameProfile profile, string gameDir)
    {
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameDir));
        var coopDir = Path.GetDirectoryName(Path.GetFullPath(Path.Combine(root, profile.AdapterIni)));
        if (coopDir == null || !IsInside(coopDir, root)) return;
        if (!Directory.Exists(coopDir)) return;
        if (profile.SaveSync is { } sync) AdapterSettings.Reset(gameDir, profile, sync);
        var keptSession = profile.SaveSync is { } config && Path.GetFullPath(Path.Combine(root, config.SessionDir)) is var session
                          && Directory.Exists(session) ? session : null;
        foreach (var entry in Directory.EnumerateFileSystemEntries(coopDir).ToList())
        {
            if (Directory.Exists(entry))
            {
                var archive = Path.GetFileName(entry).Equals(AdapterSettings.ArchiveFolder, StringComparison.OrdinalIgnoreCase);
                var holdsSession = keptSession != null && (keptSession.Equals(entry, StringComparison.OrdinalIgnoreCase) || IsInside(keptSession, entry));
                if (!archive && !holdsSession) Directory.Delete(entry, recursive: true);
            }
            else File.Delete(entry);
        }
    }

    private static bool IsInside(string path, string folder) =>
        path.StartsWith(Path.TrimEndingDirectorySeparator(folder) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    private static void WriteBuild(ModFile first)
    {
        var path = first.Dst + BuildSuffix;
        File.WriteAllText(path + TempSuffix, BuildCheck.LocalBuild().ToString());
        File.Move(path + TempSuffix, path, overwrite: true);
    }

    private static string? ReadMarker(ModFile file) => File.Exists(file.Marker) ? File.ReadAllText(file.Marker).Trim() : null;

    private static int ReadBuild(string dst)
    {
        var path = dst + BuildSuffix;
        return File.Exists(path) && int.TryParse(File.ReadAllText(path).Trim(), out var build) ? build : 0;
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    /// <summary>One file of an install: staged beside its target, then moved into place with its marker, the old copy kept
    /// aside until the whole install has succeeded so <see cref="Undo"/> can put everything back.</summary>
    private sealed class Placement(ModFile file, string packageHash, string? backupPath)
    {
        private string? _previousMarker;
        private bool _backedUp;
        private bool _movedAside;
        private bool _placed;

        public void Stage()
        {
            _previousMarker = ReadMarker(file);
            File.Copy(file.Src, file.Dst + TempSuffix, overwrite: true);
            File.WriteAllText(file.Marker + TempSuffix, packageHash);
        }

        public void Place()
        {
            if (backupPath != null)
            {
                File.Copy(file.Dst, backupPath);
                _backedUp = true;
                Log.Info($"Backed up existing {file.Dst} to {backupPath}");
            }
            if (File.Exists(file.Dst))
            {
                File.Move(file.Dst, file.Dst + PreviousSuffix, overwrite: true);
                _movedAside = true;
            }
            _placed = true;
            File.Move(file.Dst + TempSuffix, file.Dst);
            File.Move(file.Marker + TempSuffix, file.Marker, overwrite: true);
        }

        public void Commit()
        {
            if (_movedAside) Quietly(() => File.Delete(file.Dst + PreviousSuffix));
        }

        /// <summary>Puts the file, its marker and any backup made back as they were; never throws, a step that cannot be undone is skipped.</summary>
        public void Undo()
        {
            Quietly(() => File.Delete(file.Dst + TempSuffix));
            Quietly(() => File.Delete(file.Marker + TempSuffix));
            if (_placed) Quietly(() => File.Delete(file.Dst));
            if (_movedAside) Quietly(() => File.Move(file.Dst + PreviousSuffix, file.Dst, overwrite: true));
            if (_backedUp) Quietly(() => File.Delete(backupPath!));
            if (!_placed) return;
            Quietly(() =>
            {
                if (_previousMarker != null) File.WriteAllText(file.Marker, _previousMarker);
                else File.Delete(file.Marker);
            });
        }
    }

    /// <summary>For steps that must not fail the install: the old copy kept aside, and undoing a failed install.</summary>
    private static void Quietly(Action action)
    {
        try
        {
            action();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
