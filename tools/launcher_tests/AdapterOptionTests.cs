using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>OPTION_SET: the payload parser and the rule that accepts or refuses a change asked for by the game's own co-op page.</summary>
public static class AdapterOptionTests
{
    private const string PartnerMode = "partner_mode";
    private const string Outfit = "outfit_rebecca";
    private const string DlcOutfit = "71";
    private const string DlcAppId = "381713";

    private static readonly GameProfile Profile = new(
        "adapter-option-test", "Test", 1, SaveFixture.ExeName, 2, 1, [], "",
        Options:
        [
            new GameOption(PartnerMode, "Partner mode", [new OptionValue("input", "Input"), new OptionValue("state", "State")], Host: true),
            new GameOption(OptionSources.SlotKey, "Save", [new OptionValue("", "None")], Host: true, Source: OptionSources.SaveSlots),
            new GameOption(OptionSources.HashKey, "Save hash", [new OptionValue("", "None")], Host: true, Source: OptionSources.SaveHash),
            new GameOption(Outfit, "Rebecca outfit", [new OptionValue("", "Saved"), new OptionValue("02", "S.T.A.R.S. 2002"), new OptionValue(DlcOutfit, "S.T.A.R.S. 1996", DlcAppId)],
                Character: "rebecca"),
        ]);

    public static void Run()
    {
        ParsesGoodPayloads();
        RefusesBadPayloads();
        RefusesWhatTheProfileDoesNotAllow();
        AcceptsWhatThePlayerMaySet();
        OffersTheSavedSlotsOfTheHostsSave();
    }

    private static bool Parse(string text, out string key, out string value) =>
        ControlMessages.TryParseOptionSet(Encoding.ASCII.GetBytes(text), out key, out value);

    private static void ParsesGoodPayloads()
    {
        Check.That(Parse("outfit_rebecca=7d", out var key, out var value) && key == "outfit_rebecca" && value == "7d", "OPTION_SET: key=value parses");
        Check.That(Parse("load_slot=", out key, out value) && key == "load_slot" && value == "", "OPTION_SET: an empty value parses");
        Check.That(Parse("a=Some value ~", out _, out value) && value == "Some value ~", "OPTION_SET: spaces and printable punctuation are allowed in a value");
        Check.That(Parse(new string('k', 32) + "=" + new string('v', 32), out _, out _), "OPTION_SET: 32 characters each is the limit");
    }

    private static void RefusesBadPayloads()
    {
        Check.That(!Parse("Outfit=1", out _, out _) && !Parse("out-fit=1", out _, out _) && !Parse("out fit=1", out _, out _), "OPTION_SET: a key holds only a-z, 0-9 and _");
        Check.That(!Parse("=1", out _, out _), "OPTION_SET: an empty key is refused");
        Check.That(!Parse(new string('k', 33) + "=1", out _, out _) && !Parse("k=" + new string('v', 33), out _, out _), "OPTION_SET: a key or value over 32 characters is refused");
        Check.That(!Parse("novalue", out _, out _) && !Parse("", out _, out _), "OPTION_SET: a payload without = is refused");
        Check.That(!Parse("k=a;b", out _, out _) && !Parse("k=a=b", out _, out _), "OPTION_SET: a value holds no ; and no =");
        Check.That(!Parse("k=a\tb", out _, out _) && !Parse("k=a\nb", out _, out _), "OPTION_SET: a value holds printable characters only");
        Check.That(!ControlMessages.TryParseOptionSet([.. Encoding.ASCII.GetBytes("k="), 0xC3, 0xA9], out _, out _), "OPTION_SET: a byte above ASCII is refused");
    }

    private static void RefusesWhatTheProfileDoesNotAllow()
    {
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, "volume", "1") != null, "OPTION_SET: an option the profile does not have is refused");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, OptionSources.HashKey, "") != null, "OPTION_SET: the derived save hash is refused, even for the host");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, false, PartnerMode, "state") != null, "OPTION_SET: a host option from a guest is refused");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, PartnerMode, "turbo") != null, "OPTION_SET: a value the option does not offer is refused");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, false, Outfit, "99") != null, "OPTION_SET: an outfit value the option does not have is refused");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, OptionSources.SlotKey, "0") != null, "OPTION_SET: a save slot is refused when no save has slots");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, false, Outfit, DlcOutfit) != null, "OPTION_SET: a DLC outfit is refused without the DLC folder");
    }

    private static void AcceptsWhatThePlayerMaySet()
    {
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, PartnerMode, "state") == null, "OPTION_SET: the host may set a host option to an offered value");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, false, Outfit, "02") == null, "OPTION_SET: a guest may set its own outfit");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, false, Outfit, "") == null, "OPTION_SET: the saved outfit (empty value) is offered");
        Check.That(AdapterOptionRequest.WhyRefused(Profile, null, true, OptionSources.SlotKey, "") == null, "OPTION_SET: no save slot (empty value) is always offered");
        var gameDir = Path.Combine(Path.GetTempPath(), "coop-adapter-option-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(gameDir, "DLC", DlcAppId));
            Check.That(AdapterOptionRequest.WhyRefused(Profile, gameDir, false, Outfit, DlcOutfit) == null, "OPTION_SET: a DLC outfit is accepted when the game folder holds the DLC");
        }
        finally
        {
            Directory.Delete(gameDir, recursive: true);
        }
    }

    private static void OffersTheSavedSlotsOfTheHostsSave()
    {
        var root = Path.Combine(Path.GetTempPath(), "coop-adapter-option-save-" + Guid.NewGuid().ToString("N"));
        try
        {
            var game = SaveFixture.GameFolder(Path.Combine(root, "game"), SaveFixture.Slots);
            var hostDir = Path.Combine(root, "host");
            Directory.CreateDirectory(hostDir);
            File.Copy(Path.Combine(game, "coop", "session", SaveFixture.Layout.File), Path.Combine(hostDir, SaveFixture.Layout.File));
            var profile = Profile with { SaveSync = new SaveSyncProfile([SaveFixture.Layout.File], "coop/session", HostSaveDir: hostDir), SaveIndex = SaveFixture.Layout };
            Check.That(AdapterOptionRequest.WhyRefused(profile, game, true, OptionSources.SlotKey, "1") == null, "OPTION_SET: a used slot of the host's save is accepted");
            Check.That(AdapterOptionRequest.WhyRefused(profile, game, true, OptionSources.SlotKey, "3") != null, "OPTION_SET: a slot the save does not hold is refused");
            Check.That(AdapterOptionRequest.WhyRefused(profile, game, false, OptionSources.SlotKey, "1") != null, "OPTION_SET: a guest cannot pick the save slot");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
