using CoopLauncher;

namespace LauncherTests;

/// <summary>SaveSyncCoordinator.CopyOwnFiles: a guest's own files go into the session folder; a missing one or a path is skipped.</summary>
public static class SaveOwnFilesTests
{
    public static void Run()
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_own_" + Guid.NewGuid().ToString("N"));
        var own = Path.Combine(root, "own");
        var session = Path.Combine(root, "session", "win64_save");
        try
        {
            Directory.CreateDirectory(own);
            File.WriteAllText(Path.Combine(own, "data00-1.bin"), "options");
            File.WriteAllText(Path.Combine(root, "outside.bin"), "not ours");
            var copied = SaveSyncCoordinator.CopyOwnFiles(own, session, ["data00-1.bin", "missing.bin", "../outside.bin"]);
            Check.That(copied == 1 && File.ReadAllText(Path.Combine(session, "data00-1.bin")) == "options",
                "the own file is copied into a new session folder");
            Check.That(!File.Exists(Path.Combine(session, "missing.bin")) && !File.Exists(Path.Combine(root, "session", "outside.bin")),
                "a missing file and a path that leaves the folder are skipped");
            File.WriteAllText(Path.Combine(own, "data00-1.bin"), "new options");
            SaveSyncCoordinator.CopyOwnFiles(own, session, ["data00-1.bin"]);
            Check.That(File.ReadAllText(Path.Combine(session, "data00-1.bin")) == "new options", "a copy replaces the last session's");
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }
}
