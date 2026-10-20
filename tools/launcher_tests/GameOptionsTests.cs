using CoopLauncher;

namespace LauncherTests;

/// <summary>GameOptions.ForAdapter: a host keeps its own choices; a guest takes the host's value of every host option the
/// lobby carries and keeps its own for the rest; a value the profile does not offer is ignored.</summary>
public static class GameOptionsTests
{
    private const string PartnerMode = "partner_mode";
    private const string Character = "character";
    private const string Outfit = "outfit_billy";

    private static readonly GameProfile Profile = new(
        "test", "Test", 1, "test.exe", 2, 1, [],
        Options:
        [
            new GameOption(PartnerMode, "Partner mode", [new OptionValue("input", "Input"), new OptionValue("state", "State")], Host: true),
            new GameOption(Character, "Character", [new OptionValue("rebecca", "Rebecca"), new OptionValue("billy", "Billy")], Host: true),
            new GameOption(Outfit, "Billy outfit", [new OptionValue("", "Saved"), new OptionValue("05", "Prisoner")]),
        ]);

    private static Dictionary<string, string> Own() =>
        new() { [PartnerMode] = "input", [Character] = "billy", [Outfit] = "05" };

    public static void Run()
    {
        HostKeepsItsOwn();
        GuestTakesHostOptions();
        GuestKeepsItsOwnOtherOptions();
        UnofferedOrMissingValuesAreIgnored();
    }

    private static void HostKeepsItsOwn()
    {
        var host = new Dictionary<string, string> { [PartnerMode] = "state", [Character] = "rebecca" };
        var values = GameOptions.ForAdapter(Profile, Own(), host, isHost: true);
        Check.That(values[PartnerMode] == "input" && values[Character] == "billy", "the host's ini keeps the host's own choices");
    }

    private static void GuestTakesHostOptions()
    {
        var host = new Dictionary<string, string> { [PartnerMode] = "state", [Character] = "rebecca" };
        var values = GameOptions.ForAdapter(Profile, Own(), host, isHost: false);
        Check.That(values[PartnerMode] == "state", "a guest runs the host's partner mode, whatever it chose itself");
        Check.That(values[Character] == "rebecca", "a guest takes the host's character option too");
    }

    private static void GuestKeepsItsOwnOtherOptions()
    {
        var host = new Dictionary<string, string> { [PartnerMode] = "state", [Outfit] = "" };
        var values = GameOptions.ForAdapter(Profile, Own(), host, isHost: false);
        Check.That(values[Outfit] == "05", "an option that is not the host's stays the guest's own, even if the lobby names it");
        Check.That(values[Character] == "billy", "a host option the lobby does not carry stays the guest's own");
    }

    private static void UnofferedOrMissingValuesAreIgnored()
    {
        var host = new Dictionary<string, string> { [PartnerMode] = "turbo" };
        var own = Own();
        var values = GameOptions.ForAdapter(Profile, own, host, isHost: false);
        Check.That(values[PartnerMode] == "input", "a host value the profile does not offer is ignored");
        Check.That(GameOptions.ForAdapter(Profile, own, new Dictionary<string, string>(), isHost: false)[PartnerMode] == "input",
            "an empty lobby changes nothing");
        Check.That(own[PartnerMode] == "input", "the caller's dictionary is not modified");
    }
}
