using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace CoopLauncher;

public enum ModState { NotInstalled, Installed, UpdateAvailable, Modified }

/// <summary>A game's mod as found in its game folder: the state and the package build recorded when it was installed (0 if unknown).</summary>
public readonly record struct ModStatus(ModState State, int Build);

/// <summary>
/// Installs and removes a game's mod: the profile's adapter files in the game folder (a folder entry is one file per file in
/// it). A file is ours when its ownership marker (<c>.cfown</c>, holding the installed hash) records the file's own hash, or when
/// it equals the package copy (an install that was cut short before the marker). Any other file in the way is the player's: it
/// is kept as <c>.cfbak</c> (<c>.cfbak2</c>, ... when one exists, never overwritten) and the original <c>.cfbak</c> is restored
/// on uninstall. A file that carries our marker but another hash was replaced by another mod or damaged
/// (<see cref="ModState.Modified"/>); install repairs it. The installed package build is recorded beside the first file
/// (<c>.cfbuild</c>). The files a folder entry installed are listed in its manifest, so an update removes the ones the package
/// dropped. Markers, backups and the manifest of a folder entry sit under <c>.cfmod</c> beside the folder (see
/// <see cref="ModFile"/>). An install, with the removals of an update, changes all files or none.
/// </summary>
public static class ModInstaller
{
    private const byte NameEnd = 0;

    private static bool _hashFailureLogged;

    /// <summary>The state of the whole mod from all its files (<see cref="ModFiles.Decide"/>) and the package build recorded when
    /// it was installed.</summary>
    public static ModStatus Status(GameProfile profile, string? gameDir)
    {
        if (gameDir == null) return new ModStatus(ModState.NotInstalled, 0);
        var files = Files(profile, gameDir);
        try
        {
            var manifests = Manifests(files, gameDir);
            var dropped = manifests.SelectMany(manifest => manifest.Dropped()).Any(file => File.Exists(file.Dst));
            var state = ModFiles.Decide(files.Select(file => Facts(file, manifests)).ToList(), dropped);
            return new ModStatus(state, state == ModState.NotInstalled ? 0 : ReadBuild(files[0]));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {profile.Name} mod: {e.Message}");
            return new ModStatus(ModState.NotInstalled, 0);
        }
    }

    /// <summary>SHA-256 over the profile's adapter files as installed in the game folder (what runs), in profile order (a folder
    /// entry is already expanded to its files): per file the lowercase destination name with '/' separators, a 0x00 byte, the u64
    /// little-endian length and the bytes. Null when a file is missing or unreadable (an unreadable one is logged once).</summary>
    public static byte[]? ContentHash(GameProfile profile, string gameDir)
    {
        try
        {
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            foreach (var file in Files(profile, gameDir))
            {
                if (!File.Exists(file.Dst)) return null;
                var bytes = File.ReadAllBytes(file.Dst);
                var length = new byte[sizeof(ulong)];
                BinaryPrimitives.WriteUInt64LittleEndian(length, (ulong)bytes.Length);
                hash.AppendData(Encoding.UTF8.GetBytes(file.Entry.Dst.Replace('\\', '/').ToLowerInvariant()));
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

    /// <summary>Copies the package's adapter files into the game folder (also how an update or a repair re-copies them) and removes
    /// the files an earlier install put there that the package no longer has. Steps: check the files, back up the player's files in
    /// the way, copy, verify every copy against the package, remove the dropped files; each is reported to
    /// <paramref name="progress"/>. A failure while moving files into place or verifying them puts every file back.</summary>
    public static bool Install(GameProfile profile, string gameDir, IProgress<ModProgress>? progress = null)
    {
        var report = new ProgressReporter(progress);
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before installing its mod");
            report.Fail($"Close {profile.Name} first");
            return false;
        }
        var files = Files(profile, gameDir);
        var missing = files.Where(f => !File.Exists(f.Src)).Select(f => f.Src).FirstOrDefault();
        if (missing != null)
        {
            Log.Info($"Mod files missing from the package: {missing}");
            report.Fail("Mod files missing from the package");
            return false;
        }
        var placements = new List<Placement>();
        var removals = new List<Removal>();
        var manifests = Manifests(files, gameDir);
        try
        {
            report.Begin(ModStep.Checking, files.Sum(f => Weight(f.Src)), files.Count);
            foreach (var file in files)
            {
                if (Plan(file) is { } placement) placements.Add(placement);
                report.Advance(Weight(file.Src));
            }
            removals.AddRange(manifests.SelectMany(manifest => manifest.Dropped()).Select(file => new Removal(file)));
            var backups = placements.Where(placement => placement.BackupWeight > 0).ToList();
            report.Begin(ModStep.BackingUp, backups.Sum(placement => placement.BackupWeight), backups.Count);
            foreach (var placement in backups)
            {
                placement.Backup();
                report.Advance(placement.BackupWeight);
            }
            report.Begin(ModStep.Copying, placements.Sum(placement => placement.Weight), placements.Count);
            foreach (var placement in placements)
            {
                placement.Stage();
                report.Advance(placement.Weight);
            }
            foreach (var placement in placements) placement.Place();
            report.Begin(ModStep.Verifying, placements.Sum(placement => placement.Weight), placements.Count);
            foreach (var placement in placements)
            {
                placement.Verify();
                report.Advance(placement.Weight);
            }
            report.Begin(ModStep.RemovingOld, removals.Count, removals.Count);
            foreach (var removal in removals)
            {
                removal.Place();
                report.Advance(1);
            }
            WriteBuild(files[0]);
            foreach (var manifest in manifests) manifest.Write();
            foreach (var placement in placements) placement.Commit();
            foreach (var removal in removals) removal.Commit();
            Log.Info($"Installed the {profile.Name} mod");
            report.Finish();
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            foreach (var removal in Enumerable.Reverse(removals)) removal.Undo();
            foreach (var placement in Enumerable.Reverse(placements)) placement.Undo();
            foreach (var manifest in manifests) manifest.Restore();
            Log.Info($"Could not install the {profile.Name} mod: {e.Message}");
            report.Fail(e.Message);
            return false;
        }
    }

    /// <summary>Removes the adapter files the launcher installed (also the ones a package dropped), puts back any original it
    /// backed up, and deletes the game's coop folder (adapter ini, logs, session) except its archive of saved sessions and a folder
    /// that still holds the player's own files; the session's saves are archived first. Reported to <paramref name="progress"/>.</summary>
    public static bool Uninstall(GameProfile profile, string gameDir, IProgress<ModProgress>? progress = null)
    {
        var report = new ProgressReporter(progress);
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before removing its mod");
            report.Fail($"Close {profile.Name} first");
            return false;
        }
        try
        {
            var files = Files(profile, gameDir);
            var manifests = Manifests(files, gameDir);
            var owned = files.Concat(manifests.SelectMany(manifest => manifest.Dropped())).ToList();
            report.Begin(ModStep.Removing, owned.Sum(file => Weight(file.Dst)), owned.Count);
            foreach (var file in owned)
            {
                var weight = Weight(file.Dst);
                Remove(file);
                report.Advance(weight);
            }
            File.Delete(files[0].Build);
            foreach (var manifest in manifests) File.Delete(manifest.FilePath);
            RemoveCoopFolder(profile, gameDir, files);
            foreach (var file in files.Where(f => f.Entry.FolderDst != null)) PruneEmptyFolders(file, gameDir);
            Log.Info($"Removed the {profile.Name} mod");
            report.Finish();
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the {profile.Name} mod: {e.Message}");
            report.Fail(e.Message);
            return false;
        }
    }

    /// <summary>A file's share of a step's total: its size in bytes, at least 1 so an empty or missing file still counts.</summary>
    private static long Weight(string path) => File.Exists(path) ? Math.Max(1, new FileInfo(path).Length) : 1;

    private static List<ModFile> Files(GameProfile profile, string gameDir) =>
        profile.AdapterFiles.Select(entry => ToModFile(entry, gameDir, Path.Combine(RepoPaths.RepoRoot, entry.Src))).ToList();

    private static ModFile ToModFile(AdapterFile entry, string gameDir, string src) =>
        new(entry, src, Path.GetFullPath(Path.Combine(gameDir, entry.Dst)), Path.GetFullPath(ModFiles.SidePath(gameDir, entry)));

    /// <summary>One manifest per folder entry of the profile.</summary>
    private static List<FolderManifest> Manifests(List<ModFile> files, string gameDir) =>
        files.Select(f => f.Entry.FolderDst).OfType<string>().Distinct()
            .Select(folder => new FolderManifest(gameDir, folder, files.Where(f => f.Entry.FolderDst == folder).Select(f => f.Entry.Dst).ToList()))
            .ToList();

    private static FileFacts Facts(ModFile file, List<FolderManifest> manifests)
    {
        if (!File.Exists(file.Dst) || !File.Exists(file.Marker))
            return new FileFacts(false, false, false, Added: manifests.Any(manifest => manifest.IsNew(file)));
        var installed = Hash(file.Dst);
        return new FileFacts(true, installed != ReadMarker(file), File.Exists(file.Src) && Hash(file.Src) != installed);
    }

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
        return new Placement(file, packageHash, ours ? null : NextBackupPath(file));
    }

    private static string NextBackupPath(ModFile file)
    {
        var path = file.Backup;
        for (var number = 2; File.Exists(path); number++) path = $"{file.Side}{ModFile.BackupSuffix}{number}";
        return path;
    }

    private static void Remove(ModFile file)
    {
        if (File.Exists(file.Dst) && !IsOurs(file))
            Log.Info($"{file.Dst} was replaced by another mod, leaving it in place");
        else
        {
            File.Delete(file.Dst);
            if (File.Exists(file.Backup)) File.Move(file.Backup, file.Dst);
        }
        File.Delete(file.Marker);
    }

    private static bool IsOurs(ModFile file)
    {
        var installedHash = Hash(file.Dst);
        return installedHash == ReadMarker(file) || (File.Exists(file.Src) && installedHash == Hash(file.Src));
    }

    /// <summary>Deletes the coop folder's content except the archive and any folder that holds a mod file's place (what is left
    /// there is the player's: a restored original or a numbered backup). The session's saves are archived first (the same reset as
    /// after a session); a session folder that could not be archived is kept, so no save is ever lost.</summary>
    private static void RemoveCoopFolder(GameProfile profile, string gameDir, List<ModFile> files)
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
                var holdsModFile = files.Any(f => IsInside(f.Dst, entry) || IsInside(f.Side, entry));
                if (!archive && !holdsSession && !holdsModFile) Directory.Delete(entry, recursive: true);
            }
            else File.Delete(entry);
        }
    }

    private static bool IsInside(string path, string folder) =>
        path.StartsWith(Path.TrimEndingDirectorySeparator(folder) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    /// <summary>Deletes the folders left empty above a folder entry's file and its bookkeeping, up to the folder's parent.</summary>
    private static void PruneEmptyFolders(ModFile file, string gameDir)
    {
        var stop = Path.GetFullPath(Path.Combine(gameDir, Path.GetDirectoryName(file.Entry.FolderDst!) ?? ""));
        foreach (var start in new[] { file.Dst, file.Side })
            for (var dir = Path.GetDirectoryName(start); dir != null && IsInside(dir, stop) && IsEmptyFolder(dir); dir = Path.GetDirectoryName(dir))
                Directory.Delete(dir);
    }

    private static bool IsEmptyFolder(string dir) => Directory.Exists(dir) && !Directory.EnumerateFileSystemEntries(dir).Any();

    private static void WriteBuild(ModFile first)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(first.Build)!);
        File.WriteAllText(first.Build + ".tmp", BuildCheck.LocalBuild().ToString());
        File.Move(first.Build + ".tmp", first.Build, overwrite: true);
    }

    private static string? ReadMarker(ModFile file) => File.Exists(file.Marker) ? File.ReadAllText(file.Marker).Trim() : null;

    private static int ReadBuild(ModFile first) =>
        File.Exists(first.Build) && int.TryParse(File.ReadAllText(first.Build).Trim(), out var build) ? build : 0;

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    /// <summary>One file of an install: backed up if the player's, staged beside its target, moved into place with its marker and
    /// verified, the old copy kept aside until the whole install has succeeded so <see cref="Undo"/> can put everything back.</summary>
    private sealed class Placement(ModFile file, string packageHash, string? backupPath)
    {
        private string? _previousMarker;
        private bool _backedUp;
        private bool _movedAside;
        private bool _placed;

        /// <summary>The package copy's weight in the steps that handle it.</summary>
        public long Weight { get; } = ModInstaller.Weight(file.Src);

        /// <summary>The weight of the player's file that is backed up first, 0 when there is none.</summary>
        public long BackupWeight { get; } = backupPath == null ? 0 : ModInstaller.Weight(file.Dst);

        public void Backup()
        {
            if (backupPath == null) return;
            Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
            File.Copy(file.Dst, backupPath);
            _backedUp = true;
            Log.Info($"Backed up existing {file.Dst} to {backupPath}");
        }

        public void Stage()
        {
            Directory.CreateDirectory(Path.GetDirectoryName(file.Dst)!);
            Directory.CreateDirectory(Path.GetDirectoryName(file.Side)!);
            _previousMarker = ReadMarker(file);
            File.Copy(file.Src, file.Temp, overwrite: true);
            File.WriteAllText(file.Marker + ".tmp", packageHash);
        }

        public void Place()
        {
            if (File.Exists(file.Dst))
            {
                File.Move(file.Dst, file.Previous, overwrite: true);
                _movedAside = true;
            }
            _placed = true;
            File.Move(file.Temp, file.Dst);
            File.Move(file.Marker + ".tmp", file.Marker, overwrite: true);
        }

        /// <summary>The copy in the game folder must be the package copy; a difference fails the install.</summary>
        public void Verify()
        {
            if (Hash(file.Dst) != packageHash) throw new IOException($"{file.Dst} does not match the package copy");
        }

        public void Commit()
        {
            if (_movedAside) Quietly(() => File.Delete(file.Previous));
        }

        /// <summary>Puts the file, its marker and any backup made back as they were; never throws, a step that cannot be undone is skipped.</summary>
        public void Undo()
        {
            Quietly(() => File.Delete(file.Temp));
            Quietly(() => File.Delete(file.Marker + ".tmp"));
            if (_placed) Quietly(() => File.Delete(file.Dst));
            if (_movedAside) Quietly(() => File.Move(file.Previous, file.Dst, overwrite: true));
            if (_backedUp) Quietly(() => File.Delete(backupPath!));
            if (!_placed) return;
            Quietly(() =>
            {
                if (_previousMarker != null) File.WriteAllText(file.Marker, _previousMarker);
                else File.Delete(file.Marker);
            });
        }
    }

    /// <summary>A file an earlier install put there that the package dropped: moved aside (our own file only, the original backup
    /// takes its place), deleted with its marker once the whole install has succeeded.</summary>
    private sealed class Removal(ModFile file)
    {
        private bool _movedAside;
        private bool _restoredBackup;

        public void Place()
        {
            if (!File.Exists(file.Dst)) return;
            if (!IsOurs(file))
            {
                Log.Info($"{file.Dst} was replaced by another mod, leaving it in place");
                return;
            }
            File.Move(file.Dst, file.Previous, overwrite: true);
            _movedAside = true;
            if (!File.Exists(file.Backup)) return;
            File.Move(file.Backup, file.Dst);
            _restoredBackup = true;
        }

        public void Commit()
        {
            if (_movedAside) Quietly(() => File.Delete(file.Previous));
            Quietly(() => File.Delete(file.Marker));
        }

        public void Undo()
        {
            if (_restoredBackup) Quietly(() => File.Move(file.Dst, file.Backup));
            if (_movedAside) Quietly(() => File.Move(file.Previous, file.Dst, overwrite: true));
        }
    }

    /// <summary>The files a folder entry owns in the game folder, as of the last install (read when this is made) and now.</summary>
    private sealed class FolderManifest(string gameDir, string folderDst, List<string> currentDsts)
    {
        private readonly string? _previousText = File.Exists(ModFiles.ManifestPath(gameDir, folderDst))
            ? File.ReadAllText(ModFiles.ManifestPath(gameDir, folderDst)) : null;

        public string FilePath { get; } = ModFiles.ManifestPath(gameDir, folderDst);

        /// <summary>The files the last install put there that the current package no longer has.</summary>
        public IEnumerable<ModFile> Dropped() =>
            ModFiles.Removed(ModFiles.ParseManifest(_previousText), currentDsts, folderDst)
                .Select(dst => ToModFile(new AdapterFile("", dst, FolderDst: folderDst), gameDir, ""));

        /// <summary>A file of this folder that the last install did not list.</summary>
        public bool IsNew(ModFile file) =>
            file.Entry.FolderDst == folderDst && !ModFiles.ParseManifest(_previousText).Contains(file.Entry.Dst, StringComparer.OrdinalIgnoreCase);

        public void Write()
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath + ".tmp", ModFiles.FormatManifest(currentDsts));
            File.Move(FilePath + ".tmp", FilePath, overwrite: true);
        }

        public void Restore() => Quietly(() =>
        {
            if (_previousText != null) File.WriteAllText(FilePath, _previousText);
            else File.Delete(FilePath);
        });
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
