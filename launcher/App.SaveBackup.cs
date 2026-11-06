namespace CoopLauncher;

/// <summary>The session's save backups (<see cref="SaveBackup"/>) for a game whose profile has <c>saveBackup</c>: the "before"
/// copy once per session, just before this machine starts the game, and the "after" copy when the session is dropped. A host that plays
/// a campaign also has that world's folder copied to <c>&lt;archive&gt;\&lt;campaignId&gt;_&lt;stamp&gt;</c> beside the save backups.</summary>
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

    /// <summary>The campaign folder the host's game plays from, copied whole; a failure is logged and never stops the session.</summary>
    private void BackupMountedWorld(SaveBackupProfile config)
    {
        if (_session?.IsHost != true || OptionSources.CampaignSaveDir(_profile!, _gameDir!) is not { } world || !Directory.Exists(world)) return;
        var target = Path.Combine(_gameDir!, Path.GetDirectoryName(config.Into) ?? string.Empty, $"{new DirectoryInfo(world).Parent!.Name}_{DateTime.Now.ToString(AppData.StampFormat)}");
        try
        {
            foreach (var file in Directory.EnumerateFiles(world, "*", SearchOption.AllDirectories))
            {
                var copy = Path.Combine(target, Path.GetRelativePath(world, file));
                Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
                File.Copy(file, copy, overwrite: true);
            }
            Log.Info($"The campaign world backed up to {target}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not back up the campaign world {world}: {e.Message}");
        }
    }

    /// <summary>True when a copy was written; a failure is logged and never stops the session.</summary>
    private bool BackupSaves(string tag)
    {
        if (_profile?.SaveBackup is not { } config || _gameDir == null) return false;
        BackupMountedWorld(config);
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
