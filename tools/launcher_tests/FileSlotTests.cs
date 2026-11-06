using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The forced save slot of a game whose slots are separate files (Dragon's Dogma 2): the host's list read from its mounted
/// folder (its own save or a campaign's), the newest as the default, what a guest takes from the lobby and its copy's check.</summary>
public static class FileSlotTests
{
    private const string Slot = OptionSources.SlotKey;
    private const string Campaign = "campaign";
    private static readonly string[] SentFiles = ["data000.bin", "data00-1.bin", "data001Slot.bin", "data004Slot.bin"];
    private static readonly GameOption SlotOption = new(Slot, "Save", [new OptionValue("", "Newest")], Host: true, Source: OptionSources.SaveFileSlots);
    private static readonly GameOption CampaignOption = new(Campaign, "Campaign", [new OptionValue("", "Your save")], Source: OptionSources.Campaigns);

    public static void Run()
    {
        var previous = Environment.GetEnvironmentVariable(AppData.DataRootVariable);
        var root = Path.Combine(Path.GetTempPath(), "coop-file-slots-" + Guid.NewGuid().ToString("N"));
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, Path.Combine(root, "data"));
        try
        {
            var game = Path.Combine(root, "game");
            var hostDir = Path.Combine(root, "host");
            Directory.CreateDirectory(hostDir);
            Write(hostDir, "data000.bin", At(19, 31));
            Write(hostDir, "data001Slot.bin", At(18, 46));
            Write(hostDir, "data004Slot.bin", At(17, 5));
            Write(hostDir, "data002Slot.bin", At(23, 0));  // newest, but the save sync does not send it
            Write(hostDir, "data00-1.bin", At(23, 30));  // the system file
            var profile = new GameProfile("file-slot-test", "Test", 1, "x.exe", 2, 1, [], "",
                SaveSync: new SaveSyncProfile(SentFiles.ToList(), "coop/session", HostSaveDir: hostDir),
                Options: [CampaignOption, SlotOption],
                Campaigns: new CampaignsProfile("coop/campaigns", "win64_save", "profile.dd2p"));
            Names();
            HostList(profile, game);
            Campaigns(profile, game);
            GuestSide(profile, game);
        }
        finally
        {
            Environment.SetEnvironmentVariable(AppData.DataRootVariable, previous);
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    private static DateTime At(int hour, int minute) => new DateTime(2026, 11, 12, hour, minute, 0, DateTimeKind.Local).ToUniversalTime();

    private static void Write(string folder, string name, DateTime writtenUtc)
    {
        File.WriteAllText(Path.Combine(folder, name), name);
        File.SetLastWriteTimeUtc(Path.Combine(folder, name), writtenUtc);
    }

    private static void Names()
    {
        Check.That(SlotFiles.NumberOf("data000.bin") == 0 && SlotFiles.NumberOf("data001Slot.bin") == 1 && SlotFiles.NumberOf("data004Slot.bin") == 4,
            "file slots: data000.bin is the autosave, data00NSlot.bin slot N");
        Check.That(SlotFiles.NumberOf("data00-1.bin") == null && SlotFiles.NumberOf("notes.txt") == null, "file slots: the system file and other files are no slot");
        Check.That(new SlotFiles.Entry(0, At(19, 31)).Label == "Autosave / 12 Nov 19:31" && new SlotFiles.Entry(1, At(18, 46)).Label == "Slot 1 / 12 Nov 18:46",
            "file slots: the label names the slot and the time of its file");
    }

    private static void HostList(GameProfile profile, string game)
    {
        var offered = GameOptions.Offered(profile, SlotOption, game).ToList();
        Check.That(offered.Select(value => value.Value).SequenceEqual(["0", "1", "4"]) && offered[0].Label == "Autosave / 12 Nov 19:31",
            $"file slots: the host's own save lists the slots the sync sends, the newest first ({string.Join(" | ", offered.Select(value => value.Label))})");
        Check.That(GameOptions.Current(profile, game)[Slot] == "0", "file slots: nothing picked forces the newest slot");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, "4"));
        Check.That(GameOptions.Current(profile, game)[Slot] == "4", "file slots: a picked slot is forced");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, "2"));
        Check.That(GameOptions.Current(profile, game)[Slot] == "0", "file slots: a pick that is not offered falls back to the newest");
        Check.That(GameOptions.HostValues(profile, game)[Slot] == "0", "file slots: the slot travels with the lobby's host options");
        Check.That(GameOptions.Current(profile, null)[Slot] == string.Empty, "file slots: without a game folder nothing is forced");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Slot, string.Empty));
        Check.That(OptionsModel.SessionPickers(profile, game, SessionStep.Lobby, isHost: true).Select(option => option.Key).SequenceEqual([Campaign, Slot]) &&
                   OptionsModel.SessionPickers(profile, game, SessionStep.Lobby, isHost: true, worldCommitted: true).Count == 0 &&
                   OptionsModel.SessionPickers(profile, game, SessionStep.StartingGame, isHost: true).Count == 0,
            "file slots: the host picks the slot beside the campaign, locked once a guest reads or the game starts");
        Check.That(OptionsModel.SessionPickers(profile, null, SessionStep.Lobby, isHost: true).Select(option => option.Key).SequenceEqual([Campaign]),
            "file slots: no slot to pick, no slot picker");
    }

    private static void Campaigns(GameProfile profile, string game)
    {
        var made = CampaignStore.Create(profile, game, "Chaos");
        var saveDir = CampaignStore.SaveDir(profile, game, made.Id);
        Write(saveDir, "data001Slot.bin", At(20, 15));
        AppSettings.Update(settings => settings.WithOption(profile.Id, Campaign, made.Id));
        var offered = GameOptions.Offered(profile, SlotOption, game).ToList();
        Check.That(offered.Select(value => value.Value).SequenceEqual(["1"]) && GameOptions.Current(profile, game)[Slot] == "1",
            "file slots: a campaign host lists the campaign's slots, not its own save's");
        File.Delete(Path.Combine(saveDir, "data001Slot.bin"));
        Check.That(GameOptions.Current(profile, game)[Slot] == string.Empty &&
                   OptionsModel.SessionPickers(profile, game, SessionStep.Lobby, isHost: true).Select(option => option.Key).SequenceEqual([Campaign]),
            "file slots: a new campaign without a save forces nothing");
        AppSettings.Update(settings => settings.WithOption(profile.Id, Campaign, string.Empty));
    }

    private static void GuestSide(GameProfile profile, string game)
    {
        var session = Path.Combine(game, "coop", "session");
        Directory.CreateDirectory(session);
        Write(session, "data000.bin", At(19, 31));
        Write(session, "data004Slot.bin", At(17, 5));
        static Dictionary<string, string> Host(string slot) => new() { [Slot] = slot };
        Check.That(OptionSources.SessionCopyMatches(profile, game, Host("0")) && OptionSources.SessionCopyMatches(profile, game, Host("4")),
            "file slots: the guest's copy holds the slots the host forces");
        Check.That(!OptionSources.SessionCopyMatches(profile, game, Host("1")) && !OptionSources.SessionCopyMatches(profile, game, Host("x")),
            "file slots: a slot the guest's copy lacks does not match");
        Check.That(OptionSources.SessionCopyMatches(profile, game, Host(string.Empty)) && OptionSources.SessionCopyMatches(profile, game, new Dictionary<string, string>()),
            "file slots: a host that forces nothing always matches");
        var own = new Dictionary<string, string> { [Slot] = "4" };
        Check.That(GameOptions.ForAdapter(profile, own, Host("1"), isHost: false)[Slot] == "1" &&
                   GameOptions.ForAdapter(profile, own, new Dictionary<string, string>(), isHost: false)[Slot] == string.Empty &&
                   GameOptions.ForAdapter(profile, own, Host("1"), isHost: true)[Slot] == "4",
            "file slots: a guest's adapter ini gets the host's slot (none when the lobby carries none), a host its own");
    }
}
