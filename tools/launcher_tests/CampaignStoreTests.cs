using CoopLauncher;

namespace LauncherTests;

/// <summary>CampaignStore, Dd2getherImport and the campaign and character option sources: ids the adapter accepts, a new campaign
/// empty or copied from a save, the list by name, DD2gether's newest checkpoint imported once, characters only where one was captured.</summary>
public static class CampaignStoreTests
{
    private static readonly GameOption CampaignOption =
        new("campaign", "Campaign", [new OptionValue("", "Own save")], Source: OptionSources.Campaigns);
    private static readonly GameOption CharacterOption =
        new("arisen", "Character", [new OptionValue("", "Own save")], Source: OptionSources.Characters);

    public static void Run()
    {
        Check.That(CampaignStore.Slug("Chaos") == "chaos" && CampaignStore.Slug("  My World #2! ") == "my-world-2" && CampaignStore.Slug("??") == "campaign",
            "a campaign id is lower case letters, digits and single dashes");
        Check.That(CampaignStore.Slug(new string('a', 80)).Length == 48, "a long name gives an id of at most 48");
        Check.That(CampaignStore.UniqueId("chaos", id => id is "chaos" or "chaos-2") == "chaos-3", "a taken id gets the next number");
        Check.That(CampaignStore.NextName([new CampaignStore.Campaign("x", "Campaign 1", DateTime.UtcNow)]) == "Campaign 2", "the first free Campaign N");

        WithFolder(root =>
        {
            var gameDir = Path.Combine(root, "game");
            var ownSave = Path.Combine(root, "own");
            Directory.CreateDirectory(ownSave);
            File.WriteAllText(Path.Combine(ownSave, "data000.bin"), "world");
            var game = Profile();

            var empty = CampaignStore.Create(game, gameDir, "Fresh Start");
            var copied = CampaignStore.Create(game, gameDir, "Chaos", ownSave);
            Check.That(empty.Id == "fresh-start" && Directory.GetFiles(CampaignStore.SaveDir(game, gameDir, empty.Id)).Length == 0,
                "a new campaign's world starts empty");
            Check.That(File.ReadAllText(Path.Combine(CampaignStore.SaveDir(game, gameDir, copied.Id), "data000.bin")) == "world",
                "a campaign from a save starts as a copy of it");
            Check.That(CampaignStore.List(game, gameDir).Select(c => c.Name).SequenceEqual(["Chaos", "Fresh Start"]), "the campaigns list by name");

            var offered = OptionSources.Values(game, CampaignOption, gameDir).Select(v => v.Value).ToList();
            Check.That(offered.SequenceEqual(["chaos", "fresh-start", OptionSources.NewCampaign, OptionSources.CopyCampaign]),
                "the campaign option offers each campaign, then the two ways to make one");
            Check.That(OptionSources.Values(game, CharacterOption, gameDir).Select(v => v.Label).SequenceEqual(["Chaos (save once)", "Fresh Start (save once)"]),
                "every campaign's character is listed, marked until it was captured");
            File.WriteAllText(CampaignStore.CharacterFile(game, gameDir, copied.Id), "dd2p 1\ntdb 0A1B2C3D\nlevel 12 job 3 equip 0\nend\n");
            Check.That(OptionSources.Values(game, CharacterOption, gameDir).First().Label == "Chaos / Lv 12",
                "a captured character shows its level");

            var made = OptionSources.Choose(game, gameDir, CampaignOption, OptionSources.NewCampaign);
            Check.That(made == "campaign-1" && CampaignStore.List(game, gameDir).Any(c => c.Id == made), "picking New campaign makes one and chooses it");
            Check.That(OptionSources.Choose(game, gameDir, CampaignOption, "chaos") == "chaos", "picking a campaign keeps it");
        });

        WithFolder(root =>
        {
            var gameDir = Path.Combine(root, "game");
            var data = Path.Combine(gameDir, "coop", "archive", "dd2gether_1", "reframework", "data", "dd2gether");
            Directory.CreateDirectory(Path.Combine(data, "owned_campaigns", "abc"));
            Directory.CreateDirectory(Path.Combine(data, "profiles", "p1", "save"));
            File.WriteAllText(Path.Combine(data, "profiles", "p1", "profile.json"), """{ "Name": "Maliketh" }""");
            File.WriteAllText(Path.Combine(data, "profiles", "p1", "save", "data000.bin"), "character");
            File.WriteAllText(Path.Combine(data, "owned_campaigns", "abc", "campaign.json"), """{ "Name": "Chaos" }""");
            foreach (var (n, text) in new[] { (5, "older"), (6, "newest") })
            {
                var save = Path.Combine(data, "internal", "checkpoints", "campaigns", "abc", n.ToString(), "save");
                Directory.CreateDirectory(save);
                File.WriteAllText(Path.Combine(save, "data000.bin"), text);
            }
            var game = Profile(importDd2gether: true);
            Check.That(Dd2getherImport.Available(game, gameDir) && !Dd2getherImport.Available(game, root), "DD2gether's data is found in the archive");
            Check.That(Dd2getherImport.Run(game, gameDir) == 2 && Dd2getherImport.Run(game, gameDir) == 0, "a DD2gether campaign and profile are imported once");
            var chaos = CampaignStore.List(game, gameDir).Single(c => c.Name == "Chaos");
            var maliketh = CampaignStore.List(game, gameDir).Single(c => c.Name == "Maliketh");
            Check.That(maliketh.Source == Dd2getherImport.ProfileSourcePrefix + "p1" &&
                       File.ReadAllText(Path.Combine(CampaignStore.SaveDir(game, gameDir, maliketh.Id), "data000.bin")) == "character",
                "a DD2gether character profile comes in as a campaign with its save");
            Check.That(chaos.Name == "Chaos" && chaos.Source == Dd2getherImport.SourcePrefix + "abc" &&
                       File.ReadAllText(Path.Combine(CampaignStore.SaveDir(game, gameDir, chaos.Id), "data000.bin")) == "newest",
                "the import takes the name and the newest checkpoint");
        });
    }

    private static GameProfile Profile(bool importDd2gether = false) =>
        new("test", "Test", 1, "test.exe", 2, 1, [], "coop/adapter.ini", Options: [CampaignOption, CharacterOption],
            Campaigns: new CampaignsProfile("coop/campaigns", "win64_save", "profile.dd2p", importDd2gether));

    private static void WithFolder(Action<string> body)
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_campaigns_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            body(root);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
