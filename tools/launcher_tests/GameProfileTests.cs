using CoopLauncher;

namespace LauncherTests;

/// <summary>GameProfile.Load is cached, validates the fresh frame types and knows the profiles' options.</summary>
public static class GameProfileTests
{
    public static void Run()
    {
        var first = GameProfile.Load("sr2");
        Check.That(ReferenceEquals(first, GameProfile.Load("sr2")), "the same profile instance is shared");
        Check.That(ReferenceEquals(GameProfile.ListIds(), GameProfile.ListIds()) && GameProfile.ListIds().Contains("re0"), "the profile list is read once");
        Check.That(first.Options!.Single(o => o.Key == GameOptions.SeatsKey) is { Host: true, Settings: false }, "sr2 players is a host option");
        Check.That(first.Options!.Single(o => o.Key == "trace") is { Settings: true }, "sr2 trace is a settings option");
        var worldPause = GameProfile.Load("re0").Options!.Single(o => o.Key == "world_pause");
        Check.That(worldPause is { Host: true, Default: "never" } && worldPause.Values.Select(v => v.Value).SequenceEqual(["never", "menus"]),
            "re0 world pause is a host option, never by default, menus the vanilla pause");
        foreach (var id in GameProfile.ListIds()) GameProfile.Load(id);

        var threw = false;
        try
        {
            GameProfile.Parse("broken", """{ "id": "broken", "name": "Broken", "steamAppId": 1, "exe": "b.exe", "maxPlayers": 2, "port": 1, "adapterFiles": [], "adapterIni": "", "freshTypes": ["0x0001"] }""", RepoPaths.RepoRoot);
        }
        catch (InvalidDataException e)
        {
            threw = e.Message.Contains("broken");
        }
        Check.That(threw, "a bad fresh type throws at load, naming the profile");

        var fine = GameProfile.Parse("fine", """{ "id": "fine", "name": "Fine", "steamAppId": 1, "exe": "f.exe", "maxPlayers": 2, "port": 1, "adapterFiles": [], "adapterIni": "", "freshTypes": ["0x0140"] }""", RepoPaths.RepoRoot);
        Check.That(fine.FreshTypes!.Count == 1, "a valid fresh type loads");
    }
}
