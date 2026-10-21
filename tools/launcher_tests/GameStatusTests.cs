using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The rail line, the settings line, the mod button and CanHost of a game's mod state.</summary>
public static class GameStatusTests
{
    private const int GameBuild = 17178773;
    private const int ModBuild = 412;
    private const int OldModBuild = 401;
    private const int UnsupportedBuild = 99999;

    private static GameStatus Make(ModState state, int modBuild = ModBuild, bool found = true, int gameBuild = GameBuild,
        bool supported = true, string? missing = null, bool running = false) =>
        new(new ModStatus(state, modBuild), found, ModBuild, gameBuild, supported, missing, running);

    public static void Run()
    {
        Lines();
        SettingsLines();
        Buttons();
        Hosting();
    }

    private static void Lines()
    {
        Check.That(GameStatus.Unknown.Line == "Game not found", "line: no game");
        Check.That(Make(ModState.Installed, found: false).Line == "Game not found", "line: game not found beats the mod");
        Check.That(Make(ModState.Installed, gameBuild: UnsupportedBuild, supported: false).Line == "Game build 99999 unsupported", "line: unsupported build");
        Check.That(Make(ModState.NotInstalled, missing: "Juiced").Line == "Juiced not found", "line: missing required file");
        Check.That(Make(ModState.NotInstalled).Line == "Not installed", "line: not installed");
        Check.That(Make(ModState.Installed).Line == "Mod 412", "line: installed build");
        Check.That(Make(ModState.Installed, modBuild: 0).Line == "Installed", "line: installed, build unknown");
        Check.That(Make(ModState.UpdateAvailable, OldModBuild).Line == "Mod update 412", "line: update names the package build");
        Check.That(Make(ModState.Modified).Line == "Mod modified", "line: modified");
        Check.That(Make(ModState.UpdateAvailable).NeedsAttention && Make(ModState.Modified).NeedsAttention, "attention: update and modified");
        Check.That(!Make(ModState.Installed).NeedsAttention && !Make(ModState.NotInstalled).NeedsAttention, "attention: installed and not installed are quiet");
        Check.That(Make(ModState.Installed, supported: false).NeedsAttention && Make(ModState.Installed, missing: "Juiced").NeedsAttention, "attention: unsupported build and missing file");
        Check.That(!Make(ModState.Modified, found: false).NeedsAttention, "attention: nothing to fix without the game");
    }

    private static void SettingsLines()
    {
        Check.That(Make(ModState.Installed).SettingsLine == "Mod 412 / Game 17178773", "settings line: installed");
        Check.That(Make(ModState.NotInstalled, found: false).SettingsLine == "Mod not installed / Game not found", "settings line: nothing");
        Check.That(Make(ModState.Installed, gameBuild: UnsupportedBuild, supported: false).SettingsLine == "Mod 412 / Game 99999 unsupported", "settings line: unsupported");
        Check.That(Make(ModState.Modified).SettingsLine == "Mod modified / Game 17178773", "settings line: modified");
        Check.That(Make(ModState.UpdateAvailable, OldModBuild).SettingsLine == "Mod 401 / Game 17178773", "settings line: an update shows the installed build");
        Check.That(Make(ModState.Installed, gameBuild: GameBuilds.Unknown).SettingsLine == "Mod 412 / Game found", "settings line: build unknown");
    }

    private static void Buttons()
    {
        Check.That(Make(ModState.NotInstalled).Action == new ModAction("Install", true), "button: install");
        Check.That(Make(ModState.UpdateAvailable).Action == new ModAction("Update", true), "button: update");
        Check.That(Make(ModState.Modified).Action == new ModAction("Repair", true), "button: repair");
        Check.That(Make(ModState.Installed).Action == null, "button: none when the mod needs nothing");
        Check.That(Make(ModState.NotInstalled, found: false).Action is { Enabled: false }, "button: install needs the game");
        Check.That(Make(ModState.UpdateAvailable, running: true).Action is { Enabled: false }, "button: not while the game runs");
        Check.That(Make(ModState.Modified, running: true).Action is { Label: "Repair", Enabled: false }, "button: repair waits for the game to close");
    }

    private static void Hosting()
    {
        Check.That(Make(ModState.Installed).CanHost, "host: installed");
        Check.That(Make(ModState.UpdateAvailable).CanHost && Make(ModState.Modified).CanHost, "host: an update or a repair is applied by the start");
        Check.That(!Make(ModState.NotInstalled).CanHost, "host: needs the mod");
        Check.That(!Make(ModState.Installed, supported: false).CanHost, "host: needs a supported build");
        Check.That(!Make(ModState.Installed, missing: "Juiced").CanHost, "host: needs the required file");
        Check.That(!Make(ModState.Installed, found: false).CanHost, "host: needs the game");
    }
}
