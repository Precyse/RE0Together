using System.Globalization;

namespace CoopLauncher;

/// <summary>Other co-op mods that must not run beside ours (profile key <c>conflicts</c>: a name, its paths relative to the game
/// folder, and the archive folder). When any path is there and the game is closed, every one of them (scripts and the mod's data,
/// its saves included) is moved, never deleted, into <c>&lt;into&gt;/&lt;name&gt;_&lt;yyyyMMdd_HHmmss&gt;/</c> under the same relative
/// paths, so it can be put back by hand. Done at launcher start, before a mod install and before a session starts.</summary>
public static class ConflictArchive
{
    private const string StampFormat = "yyyyMMdd_HHmmss";

    /// <summary>The paths of a conflict that are in the game folder.</summary>
    public static IReadOnlyList<string> Present(ConflictProfile conflict, string gameDir) =>
        conflict.Paths.Where(path => File.Exists(Path.Combine(gameDir, path)) || Directory.Exists(Path.Combine(gameDir, path))).ToList();

    /// <summary>Moves every conflict found into its archive; returns the archive folders made (none when nothing was found or the
    /// game runs). A path that cannot be moved is logged and the rest go on.</summary>
    public static IReadOnlyList<string> Run(GameProfile profile, string gameDir, bool gameRunning, DateTime now)
    {
        var made = new List<string>();
        if (gameRunning) return made;
        foreach (var conflict in profile.Conflicts ?? [])
        {
            var present = Present(conflict, gameDir);
            if (present.Count == 0) continue;
            var archive = FolderFor(gameDir, conflict.Into, conflict.Name, now);
            foreach (var path in present)
            {
                try
                {
                    MoveInto(Path.Combine(gameDir, path), Path.Combine(archive, path));
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                {
                    Log.Info($"{conflict.Name}: {path} not moved: {e.Message}");
                }
            }
            Log.Event($"Moved {conflict.Name} to {Path.GetRelativePath(gameDir, archive)}");
            made.Add(archive);
        }
        return made;
    }

    /// <summary>The archive folder <c>&lt;gameDir&gt;/&lt;into&gt;/&lt;name&gt;_&lt;stamp&gt;</c> of one archiving (not created).</summary>
    public static string FolderFor(string gameDir, string into, string name, DateTime now) =>
        Path.GetFullPath(Path.Combine(gameDir, into, $"{CampaignStore.Slug(name)}_{now.ToString(StampFormat, CultureInfo.InvariantCulture)}"));

    /// <summary>Moves a file or folder to <paramref name="destination"/>, making its folder; throws what a move throws.</summary>
    public static void MoveInto(string from, string destination)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if (Directory.Exists(from)) Directory.Move(from, destination);
        else File.Move(from, destination);
    }
}
