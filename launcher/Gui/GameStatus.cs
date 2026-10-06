namespace CoopLauncher.Gui;

/// <summary>What the rail and the mod button say about a game's mod. Every game ships in one package, so an update's
/// build is the package build. A game build the profile does not list is shown instead of the mod state.</summary>
internal sealed record GameStatus(ModStatus Mod, bool GameFound, int PackageBuild, int GameBuild = GameBuilds.Unknown, bool BuildSupported = true)
{
    public static readonly GameStatus Unknown = new(new ModStatus(ModState.NotInstalled, 0), false, 0);

    public bool ModInstalled => Mod.State != ModState.NotInstalled;

    public bool UpdateAvailable => Mod.State == ModState.UpdateAvailable;

    /// <summary>The row's line is drawn in the amber state colour.</summary>
    public bool NeedsAttention => GameFound && (UpdateAvailable || !BuildSupported);

    /// <summary>Hosting needs the mod installed and a supported game build.</summary>
    public bool CanHost => ModInstalled && BuildSupported;

    public string Line => !GameFound ? "Game not found"
        : !BuildSupported ? $"Game build {GameBuild} unsupported"
        : Mod.State switch
        {
            ModState.NotInstalled => "Not installed",
            ModState.UpdateAvailable => PackageBuild > 0 ? $"Update available (Build {PackageBuild})" : "Update available",
            _ => Mod.Build > 0 ? $"Installed (Build {Mod.Build})" : "Installed",
        };

    /// <summary>The label of the one mod button, and whether it can be used now.</summary>
    public (string Label, bool Enabled) Action => Mod.State switch
    {
        ModState.NotInstalled => ("Install", GameFound),
        ModState.UpdateAvailable => ("Update", GameFound),
        _ => ("Uninstall", GameFound),
    };
}
