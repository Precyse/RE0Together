namespace CoopLauncher;

/// <summary>The session's save backups (<see cref="SaveBackup"/>) for a game whose profile has <c>saveBackup</c>: the "before"
/// copy once per session, just before this machine starts the game, and the "after" copy when the session is dropped.</summary>
public sealed partial class App
{
    private bool _beforeBackupTaken;

    private void BackupSavesBeforeLaunch()
    {
        if (_beforeBackupTaken) return;
        _beforeBackupTaken = BackupSaves(SaveBackup.Before);
    }

    private void BackupSavesAfterSession()
    {
        if (_beforeBackupTaken) BackupSaves(SaveBackup.After);
        _beforeBackupTaken = false;
    }

    /// <summary>True when a copy was written; a failure is logged and never stops the session.</summary>
    private bool BackupSaves(string tag)
    {
        if (_profile?.SaveBackup is not { } config || _gameDir == null) return false;
        if (SavePaths.Expand(config.Dir) is not { } saveDir)
        {
            Log.Info($"No save folder to back up for {_profile.Name} (the folder template needs a signed-in Steam user)");
            return false;
        }
        try
        {
            var copy = SaveBackup.Take(saveDir, Path.Combine(_gameDir, config.Into), DateTime.Now, tag, config.Keep);
            Log.Info(copy == null ? $"No save folder at {saveDir}, nothing backed up" : $"Saves backed up ({tag}) to {copy}");
            return copy != null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not back up {_profile.Name}'s saves: {e.Message}");
            return false;
        }
    }
}
