using CoopLauncher;

namespace LauncherTests;

/// <summary>Updater.Replace rolls back a failed copy; Updater.Recover rolls back an interrupted replace and clears a finished one.</summary>
public static class UpdaterTests
{
    private const string Marker = "update.pending";

    public static void Run()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_update_" + Guid.NewGuid().ToString("N"));
        try
        {
            FailedCopyRestoresEveryOriginal(Path.Combine(dir, "failed"));
            InterruptedReplaceIsRolledBack(Path.Combine(dir, "interrupted"));
            FinishedReplaceDeletesBackups(Path.Combine(dir, "finished"));
            SuccessfulReplaceKeepsBackupsUntilRecover(Path.Combine(dir, "success"));
            UpdateLockIsHeldByOneProcessAtATime(Path.Combine(dir, "locked"));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    private static void Write(string root, string relative, string text)
    {
        var path = Path.Combine(root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
    }

    private static string Read(string root, string relative) => File.ReadAllText(Path.Combine(root, relative));

    private static bool Exists(string root, string relative) => File.Exists(Path.Combine(root, relative));

    private static void FailedCopyRestoresEveryOriginal(string work)
    {
        var source = Path.Combine(work, "source");
        var root = Path.Combine(work, "root");
        Write(root, "a.txt", "old a");
        Write(root, "sub/b.txt", "old b");
        Write(source, "a.txt", "new a");
        Write(source, "new.txt", "brand new");
        Write(source, "sub/b.txt", "new b");
        Write(source, "z_locked.txt", "new z");
        Write(root, "z_locked.txt", "old z");

        var threw = false;
        using (new FileStream(Path.Combine(source, "z_locked.txt"), FileMode.Open, FileAccess.Read, FileShare.None))
        {
            try
            {
                Updater.Replace(source, root);
            }
            catch (IOException)
            {
                threw = true;
            }
        }
        Check.That(threw, "a copy that fails mid-way rethrows to the caller");
        Check.That(Read(root, "a.txt") == "old a" && Read(root, "sub/b.txt") == "old b" && Read(root, "z_locked.txt") == "old z",
            "a failed replace restores every original");
        Check.That(!Exists(root, "new.txt"), "a failed replace deletes the new copies it made");
        Check.That(Directory.GetFiles(root, "*.old", SearchOption.AllDirectories).Length == 0, "a failed replace leaves no .old files");
    }

    private static void InterruptedReplaceIsRolledBack(string root)
    {
        Write(root, "a.txt", "half new a");
        Write(root, "a.txt.old", "old a");
        Write(root, "sub/b.txt.old", "old b");
        Write(root, Marker, "413");

        Updater.Recover(root);

        Check.That(Read(root, "a.txt") == "old a" && Read(root, "sub/b.txt") == "old b", "an interrupted replace is rolled back from the .old files");
        Check.That(!Exists(root, Marker) && !Exists(root, "a.txt.old"), "the marker and the .old files are gone after the rollback");
    }

    private static void FinishedReplaceDeletesBackups(string root)
    {
        Write(root, "a.txt", "new a");
        Write(root, "a.txt.old", "old a");

        Updater.Recover(root);

        Check.That(Read(root, "a.txt") == "new a" && !Exists(root, "a.txt.old"), "a finished replace keeps the new files and deletes the .old files");
    }

    private static void SuccessfulReplaceKeepsBackupsUntilRecover(string work)
    {
        var source = Path.Combine(work, "source");
        var root = Path.Combine(work, "root");
        Write(root, "a.txt", "old a");
        Write(source, "a.txt", "new a");
        Write(source, "sub/c.txt", "new c");

        Updater.Replace(source, root);

        Check.That(Read(root, "a.txt") == "new a" && Read(root, "sub/c.txt") == "new c", "replace installs the package");
        Check.That(Read(root, "a.txt.old") == "old a", "replace keeps the original as .old for the running copy");
    }

    private static bool LockFromAnotherThread(string root) =>
        Task.Run(() =>
        {
            using var handle = Updater.TryAcquireLock(root);
            return handle != null;
        }).Result;

    private static void UpdateLockIsHeldByOneProcessAtATime(string root)
    {
        using (var held = Updater.TryAcquireLock(root))
        {
            Check.That(held != null, "the update lock is free at first");
            Check.That(!LockFromAnotherThread(root), "a second update waits out the one under way");
        }
        Check.That(LockFromAnotherThread(root), "the update lock is free again once released");
    }
}
