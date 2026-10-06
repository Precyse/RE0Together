namespace CoopLauncher.Gui;

/// <summary>What the rail and the mod button say about a game's mod. Every game ships in one package, so an update's
/// build is the package build.</summary>
internal sealed record GameStatus(ModStatus Mod, bool GameFound, int PackageBuild)
{
    public static readonly GameStatus Unknown = new(new ModStatus(ModState.NotInstalled, 0), false, 0);

    public bool ModInstalled => Mod.State != ModState.NotInstalled;

    public bool UpdateAvailable => Mod.State == ModState.UpdateAvailable;

    public string Line => !GameFound ? "Game not found" : Mod.State switch
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
