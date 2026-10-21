using CoopLauncher;

namespace LauncherTests;

/// <summary>BUILD_INFO payload and the mismatch reasons: launcher build, then game build, then mod, first found wins.</summary>
public static class BuildInfoTests
{
    private static byte[] Hash(byte seed) => Enumerable.Repeat(seed, BuildInfo.HashBytes).ToArray();

    private static BuildInfo Info(int launcher, int game, byte[]? hash) => new(launcher, game, hash);

    public static void Run()
    {
        Codec();
        Order();
        ModHash();
        DevBuilds();
    }

    private static void Codec()
    {
        var full = Info(7, 100, Hash(3)).Encode();
        Check.That(full.Length == 8 + BuildInfo.HashBytes, "payload: i32, i32, 32 bytes of hash");
        Check.That(BuildInfo.TryDecode(full, out var decoded) && decoded.LauncherBuild == 7 && decoded.GameBuild == 100 &&
                   decoded.ModHash!.SequenceEqual(Hash(3)), "round trip");
        Check.That(BuildInfo.TryDecode(Info(7, 100, null).Encode(), out var zero) && zero.ModHash == null, "all-zero hash = not installed");
        Check.That(BuildInfo.TryDecode(full[..8], out var noHash) && noHash.GameBuild == 100 && noHash.ModHash == null,
            "older payload without a hash: no hash check");
        Check.That(BuildInfo.TryDecode(full[..4], out var launcherOnly) && launcherOnly.LauncherBuild == 7 &&
                   launcherOnly.GameBuild == GameBuilds.Unknown, "older payload without a game build: unknown");
        Check.That(!BuildInfo.TryDecode(full[..3], out _), "too short to read");
    }

    private static void Order()
    {
        var mod = Hash(1);
        Check.That(BuildInfo.Compare(Info(6, 100, mod), Info(6, 100, mod)) == BuildMismatch.None, "same everything: none");
        Check.That(BuildInfo.Compare(Info(5, 100, mod), Info(6, 100, mod)) == BuildMismatch.LauncherBuild, "launcher build differs");
        Check.That(BuildInfo.Compare(Info(5, 100, mod), Info(6, 200, Hash(2))) == BuildMismatch.LauncherBuild,
            "launcher and game and mod differ: the launcher build is reported first");
        Check.That(BuildInfo.Compare(Info(6, 100, mod), Info(6, 200, Hash(2))) == BuildMismatch.GameBuild,
            "game and mod differ: the game build is reported first");
        Check.That(BuildInfo.Compare(Info(6, 100, mod), Info(6, GameBuilds.Unknown, mod)) == BuildMismatch.None,
            "an unknown game build is not compared");
    }

    private static void ModHash()
    {
        Check.That(BuildInfo.Compare(Info(6, 100, null), Info(6, 100, Hash(2))) == BuildMismatch.None,
            "host hash zero: no check");
        Check.That(BuildInfo.Compare(Info(6, 100, Hash(1)), Info(6, 100, null)) == BuildMismatch.ModNotInstalled,
            "host has the mod, this machine does not: mod not installed");
        Check.That(BuildInfo.Compare(Info(6, 100, Hash(1)), Info(6, 100, Hash(2))) == BuildMismatch.ModFiles,
            "other hash: mod files differ");
        Check.That(BuildInfo.Compare(Info(6, 100, Hash(1)), Info(6, 100, Hash(1))) == BuildMismatch.None, "same hash: none");
    }

    private static void DevBuilds()
    {
        Check.That(BuildInfo.Compare(Info(BuildInfo.DevBuild, 100, Hash(1)), Info(6, 100, Hash(1))) == BuildMismatch.None,
            "a dev build has no number to compare");
        Check.That(BuildInfo.Compare(Info(BuildInfo.DevBuild, 100, Hash(1)), Info(6, 100, Hash(2))) == BuildMismatch.ModFiles,
            "a dev build is still guarded by the mod hash");
        Check.That(BuildInfo.Compare(Info(6, 100, Hash(1)), Info(BuildInfo.DevBuild, 100, null)) == BuildMismatch.ModNotInstalled,
            "a dev guest without the mod is refused");
    }
}
