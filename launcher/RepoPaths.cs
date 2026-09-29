namespace CoopLauncher;

/// <summary>Locates the repo by walking up from the build output to the folder holding launcher/games.</summary>
public static class RepoPaths
{
    private static readonly string Root = FindRoot();

    public static string RepoRoot => Root;
    public static string GamesDir => Path.Combine(Root, "launcher", "games");

    private static string FindRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            if (Directory.Exists(Path.Combine(dir.FullName, "launcher", "games"))) return dir.FullName;
        throw new DirectoryNotFoundException("Repo root (folder containing launcher/games) not found above " + AppContext.BaseDirectory);
    }
}
