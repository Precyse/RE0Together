namespace CoopLauncher.Gui;

/// <summary>The mod button the Session row offers: what it does and whether it can be used now.</summary>
internal readonly record struct ModAction(string Label, bool Enabled);

/// <summary>What the rail, the mod button and the settings view say about a game's mod. Every game ships in one package,
/// so an update's build is the package build. A game build the profile does not list, or a required file the game folder
/// lacks (named by MissingRequirement), is shown instead of the mod state.</summary>
internal sealed record GameStatus(
    ModStatus Mod, bool GameFound, int PackageBuild, int GameBuild = GameBuilds.Unknown, bool BuildSupported = true,
    string? MissingRequirement = null, bool GameRunning = false)
{
    public static readonly GameStatus Unknown = new(new ModStatus(ModState.NotInstalled, 0), false, 0);

    public bool ModInstalled => Mod.State != ModState.NotInstalled;

    public bool UpdateAvailable => Mod.State == ModState.UpdateAvailable;

    /// <summary>The row's line is drawn in the amber state colour.</summary>
    public bool NeedsAttention => HasProblem || GameFound && Mod.State == ModState.UpdateAvailable;

    /// <summary>Something stops or endangers play: a modified mod, an unsupported game build or a missing required file.</summary>
    public bool HasProblem => GameFound && (Mod.State == ModState.Modified || !BuildSupported || MissingRequirement != null);

    /// <summary>Hosting needs the game, its mod (an update or a repair is applied by the start itself), a supported game build
    /// and every required file.</summary>
    public bool CanHost => GameFound && ModInstalled && BuildSupported && MissingRequirement == null;

    /// <summary>The rail's line under the game's name.</summary>
    public string Line => !GameFound ? "Game not found"
        : !BuildSupported ? $"Game build {GameBuild} unsupported"
        : MissingRequirement != null ? $"{MissingRequirement} not found"
        : Mod.State switch
        {
            ModState.NotInstalled => "Not installed",
            ModState.UpdateAvailable => PackageBuild > 0 ? $"Mod update {PackageBuild}" : "Mod update",
            ModState.Modified => "Mod modified",
            _ => ModLabel,
        };

    /// <summary>The settings view's line under a game's folder: the mod and the game's build.</summary>
    public string SettingsLine => $"{SettingsMod} / {SettingsGame}";

    /// <summary>The one mod button the Session row offers, or none when the mod needs nothing. Never usable while the game runs.</summary>
    public ModAction? Action => Mod.State switch
    {
        ModState.NotInstalled => new ModAction("Install", GameFound && !GameRunning),
        ModState.UpdateAvailable => new ModAction("Update", GameFound && !GameRunning),
        ModState.Modified => new ModAction("Repair", GameFound && !GameRunning),
        _ => null,
    };

    private string ModLabel => Mod.Build > 0 ? $"Mod {Mod.Build}" : "Installed";

    private string SettingsMod => Mod.State switch
    {
        ModState.NotInstalled => "Mod not installed",
        ModState.Modified => "Mod modified",
        _ => ModLabel,
    };

    private string SettingsGame => !GameFound ? "Game not found"
        : GameBuild == GameBuilds.Unknown ? "Game found"
        : BuildSupported ? $"Game {GameBuild}"
        : $"Game {GameBuild} unsupported";

    /// <summary>Reads a game's mod state: the folder, the installed game build and the mod's files (it hashes them, so not on
    /// the UI thread), and whether the game runs now.</summary>
    public static GameStatus Read(GameProfile game)
    {
        var gameDir = GameFolders.Find(game);
        var gameBuild = GameBuilds.Installed(game, gameDir);
        return new GameStatus(
            ModInstaller.Status(game, gameDir), gameDir != null, BuildCheck.LocalBuild(), gameBuild, GameBuilds.IsSupported(game, gameBuild),
            gameDir == null ? null : game.MissingRequirement(gameDir), GameLauncher.IsRunning(game));
    }
}
