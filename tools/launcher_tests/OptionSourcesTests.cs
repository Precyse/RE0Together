using CoopLauncher;
using CoopLauncher.Gui;
using CoopLauncher.Web;

namespace LauncherTests;

/// <summary>The forced save slot as a game option: the guest's check of its copy against the host's hash, the host's choices from its own
/// save (a profile whose host save folder is a temp folder), what a guest takes from the lobby, and the picker's rules.</summary>
public static class OptionSourcesTests
{
    private const string Slot = OptionSources.SlotKey;
    private const string Hash = OptionSources.HashKey;
    private const string PartnerMode = "partner_mode";

    private static readonly GameOption SlotOption = new(Slot, "Save", [new OptionValue("", "None")], Host: true, Source: OptionSources.SaveSlots);
    private static readonly GameOption HashOption = new(Hash, "Save hash", [new OptionValue("", "None")], Host: true, Source: OptionSources.SaveHash);

    public static void Run()
    {
        var previous = Environment.GetEnvironmentVariable(AppData.DataRootVariable);
        var root = Path.Combine(Path.GetTempPath(), "coop-save-options-" + Guid.NewGuid().ToString("N"));
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, Path.Combine(root, "data"));
        try
        {
            var game = SaveFixture.GameFolder(Path.Combine(root, "game"), SaveFixture.Slots);
            var hostDir = Path.Combine(root, "host");
            Directory.CreateDirectory(hostDir);
            File.Copy(Path.Combine(game, "coop", "session", SaveFixture.Layout.File), Path.Combine(hostDir, SaveFixture.Layout.File));
            var profile = ProfileWith(hostDir);
            var hash = SaveIndex.HashOf(SaveFixture.SlotBytes(SaveFixture.Slots[0]));
            GuestCheck(profile, game, hash);
            HostChoices(profile, game);
            GuestTakesTheHosts(profile);
            Picker(profile, game);
            HostingScreenRow(profile, game);
            GridsSkipRuntimeOptions(profile);
        }
        finally
        {
            Environment.SetEnvironmentVariable(AppData.DataRootVariable, previous);
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    private static GameProfile ProfileWith(string hostSaveDir) => new(
        "save-options-test", "Test", 1, SaveFixture.ExeName, 2, 1, [], "",
        SaveSync: new SaveSyncProfile([SaveFixture.Layout.File], "coop/session", HostSaveDir: hostSaveDir),
        Options: [SlotOption, HashOption, new GameOption(PartnerMode, "Partner mode", [new OptionValue("state", "State")], Host: true)],
        SaveIndex: SaveFixture.Layout);

    private static Dictionary<string, string> Host(string slot, string hash) => new() { [Slot] = slot, [Hash] = hash };

    private static void GuestCheck(GameProfile profile, string game, string hash)
    {
        Check.That(OptionSources.SessionCopyMatches(profile, game, Host("0", hash)), "forced load: the guest's copy holds the slot the host's hash names");
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("0", "00000000")), "forced load: another hash is a different save");
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("0", string.Empty)), "forced load: a host that forces a slot without a hash is not matched");
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("2", hash)), "forced load: a slot that is empty in the guest's copy does not match");
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("x", hash)) && !OptionSources.SessionCopyMatches(profile, game, Host("9", hash)),
            "forced load: a slot that is not a slot of the save does not match");
        Check.That(OptionSources.SessionCopyMatches(profile, game, Host(string.Empty, string.Empty)) && OptionSources.SessionCopyMatches(profile, null, new Dictionary<string, string>()),
            "forced load: a host that forces nothing always matches");
        Check.That(!OptionSources.SessionCopyMatches(profile, Path.Combine(game, "nowhere"), Host("0", hash)), "forced load: no copy of the save at all does not match");
        File.WriteAllBytes(Path.Combine(game, "coop", "session", SaveFixture.Layout.File), SaveFixture.Build(
            [SaveFixture.Slots[0] with { Count = 12 }, SaveFixture.Slots[1]], SaveFixture.Key));
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("0", hash)), "forced load: a newer copy with the slot changed is noticed at once (the cache follows the file)");
    }

    private static void HostChoices(GameProfile profile, string game)
    {
        var offered = GameOptions.Offered(profile, SlotOption, game).ToList();
        Check.That(offered.Select(value => value.Value).SequenceEqual(["", "0", "1"]) && offered[1].Label == "Slot 1 / Normal / 0:02:17 / Area 4",
            $"forced load: the picker offers none, then the used slots of the host's save ({string.Join(" | ", offered.Select(value => value.Label))})");
        Check.That(GameOptions.Current(profile, game)[Slot] == string.Empty && GameOptions.Current(profile, game)[Hash] == string.Empty, "forced load: nothing picked, nothing forced");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, "1"));
        var current = GameOptions.Current(profile, game);
        Check.That(current[Slot] == "1" && current[Hash] == SaveIndex.HashOf(SaveFixture.SlotBytes(SaveFixture.Slots[1])), "forced load: a picked slot carries the hash of its bytes");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, "3"));
        Check.That(GameOptions.Current(profile, game)[Slot] == string.Empty && GameOptions.Current(profile, game)[Hash] == string.Empty, "forced load: a picked slot that is empty now is not forced");
        Check.That(GameOptions.HostValues(profile, game).Keys.Order().SequenceEqual([Hash, Slot, PartnerMode]), "forced load: the slot and its hash travel with the lobby's host options");
        Check.That(GameOptions.Offered(profile, SlotOption, null).Select(value => value.Value).SequenceEqual([""]), "forced load: without a game folder only none is offered");
    }

    private static void GuestTakesTheHosts(GameProfile profile)
    {
        var own = new Dictionary<string, string> { [Slot] = "5", [Hash] = "deadbeef", [PartnerMode] = "state" };
        var fromHost = GameOptions.ForAdapter(profile, own, Host("0", "e40c292c"), isHost: false);
        Check.That(fromHost[Slot] == "0" && fromHost[Hash] == "e40c292c", "forced load: a guest's game gets the host's slot and hash though its own save has other slots");
        var fromOldHost = GameOptions.ForAdapter(profile, own, new Dictionary<string, string>(), isHost: false);
        Check.That(fromOldHost[Slot] == string.Empty && fromOldHost[Hash] == string.Empty, "forced load: a host that carries no slot forces none, whatever the guest picked for itself");
        var hostItself = GameOptions.ForAdapter(profile, own, Host("0", "e40c292c"), isHost: true);
        Check.That(hostItself[Slot] == "5" && hostItself[Hash] == "deadbeef", "forced load: the host's own game gets its own values");
    }

    private static void Picker(GameProfile profile, string game)
    {
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, string.Empty));
        Check.That(OptionsModel.SavePicker(profile, game, SessionStep.Lobby)?.Key == Slot, "forced load: the host picks the slot while it waits");
        Check.That(OptionsModel.SavePicker(profile, game, SessionStep.StartingGame) == null && OptionsModel.SavePicker(profile, game, SessionStep.InGame) == null,
            "forced load: the pick is closed once the game starts");
        Check.That(OptionsModel.SavePicker(profile, null, SessionStep.Lobby) == null, "forced load: no slots to pick, no picker");
        Check.That(OptionsModel.SavePicker(profile with { Options = [] }, game, SessionStep.Lobby) == null, "forced load: a game without the option has no picker");
    }

    private static void HostingScreenRow(GameProfile profile, string game)
    {
        var hosting = HostingState(profile, game, SessionStep.Lobby, isHost: true);
        Check.That(hosting.Save is { Key: Slot, Label: "Save", Value: "", Locked: false } && hosting.Save.Values.Select(value => value.Value).SequenceEqual(["", "0", "1"]),
            "forced load: the hosting screen carries the picker with the host's slots and nothing picked");
        Check.That(HostingState(profile, game, SessionStep.Lobby, isHost: false).Save == null, "forced load: a guest's hosting screen has no picker");
        Check.That(HostingState(profile, game, SessionStep.StartingGame, isHost: true).Save == null, "forced load: the picker goes when the host's game starts");
        Check.That(HostingState(profile with { Options = [] }, game, SessionStep.Lobby, isHost: true).Save == null, "forced load: a game without the option has no picker on its screen");
    }

    private static SessionDto HostingState(GameProfile profile, string game, SessionStep step, bool isHost)
    {
        var host = new PlayerSlot(2, "Precyse", IsHost: true, IsLocal: isHost, Step: step);
        var guest = new PlayerSlot(1, "Amy", IsHost: false, IsLocal: !isHost, Step: step);
        var status = new AppStatus(isHost ? AppState.Hosting : AppState.Joined, 1) { GameId = profile.Id, Step = step, Seats = 2, Bridged = true, Players = [host, guest] };
        var mod = new GameStatus(new ModStatus(ModState.Installed, 412), true, 412, 17178773);
        var input = new SnapshotInput(
            status, [new GameEntry(profile, mod, new ArtDto(null, null, null))], profile.Id, ScreenKind.Hosting, null, Tab.Play, PopupKind.None, 0, LobbyFilter.All, null, null,
            GameCondition.Fine, new UpdateView(null, false, 0, false), [], 412, null, 0, 0, _ => game, _ => null);
        return UiSnapshot.MainPage(input, ScreenKind.Hosting).Session!;
    }

    private static void GridsSkipRuntimeOptions(GameProfile profile) =>
        Check.That(OptionsModel.OptionsOf(profile, settingsOptions: false).Select(option => option.Key).SequenceEqual([PartnerMode]), "forced load: the options popup does not list the slot or its hash");
}
