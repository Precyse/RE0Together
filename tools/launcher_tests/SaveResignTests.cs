using System.Security.Cryptography;
using CoopLauncher;
using CoopLauncher.SaveResign;

namespace LauncherTests;

/// <summary>The Mandarin save re-sign: the pieces equal MandarinJuice's (vectors taken from its code), a file written for one Steam
/// account is readable by another after re-signing and no longer by the first, bad files are refused, and the profile key is validated.</summary>
public static class SaveResignTests
{
    private const ulong DragonsDogma2Seed = 6829223510001954331;
    private const uint DragonsDogma2Variant = 1;
    private const ulong HostSteamId = 76561198071592961;
    private const ulong GuestSteamId = 76561198150990292;
    private const string KindName = "mandarin";
    private static readonly ResignProfile Dd2Profile = new(KindName, DragonsDogma2Seed.ToString(), DragonsDogma2Variant);

    private static readonly (int Length, ulong Checksum)[] ChecksumVectors =
    [
        (0, 0x9AE16A3B2F90404F), (1, 0xBEC09CC447469D8B), (3, 0x40B4412E33EEFD64), (4, 0xF3C7EE05197A7B92), (7, 0x4FEF0B39C12AA9A4),
        (8, 0x0C97ECBC1E9D2626), (16, 0x7EDA0ED8C837A168), (17, 0xC9518FE63EBF6127), (32, 0xA53F806F23A314E8), (33, 0x0D861240F8A6C64B),
        (64, 0x1CCEFEA99389AE75), (65, 0xACF6A69F1F078342), (200, 0x6099C264B552B496),
    ];

    public static void Run()
    {
        ChecksumEqualsMandarinJuice();
        CipherEqualsMandarinJuice();
        UserIdVariants();
        ResignOpensForTheNewOwnerOnly();
        BadFilesAreRefused();
        ResignFileLeavesAFailedFileAlone();
        ProfileKeyIsValidated();
    }

    private static byte[] Pattern(int length, int salt)
    {
        var data = new byte[length];
        for (var i = 0; i < length; i++) data[i] = (byte)(i * 7 + 3 + salt);
        return data;
    }

    private static ulong UserId(ulong steamId64) => MandarinUserId.FromSteamId64(steamId64, DragonsDogma2Variant);

    private static void ChecksumEqualsMandarinJuice()
    {
        foreach (var (length, expected) in ChecksumVectors)
            Check.That(MandarinSliceChecksum.Of(Pattern(length, 0)) == expected, $"slice checksum of {length} bytes equals MandarinJuice's");
    }

    private static void CipherEqualsMandarinJuice()
    {
        const string expectedHash = "3F32C0C2E3402E6704F477E7FBB108802DAFFD8BFEF85C3BD958841C03DC9DBF";
        var encrypted = new MandarinCipher(DragonsDogma2Seed).Encrypt(Pattern(300, 9), UserId(HostSteamId));
        Check.That(Convert.ToHexString(SHA256.HashData(encrypted)) == expectedHash, "the cipher's output for a fixed input equals MandarinJuice's");
    }

    private static void UserIdVariants()
    {
        Check.That(UserId(HostSteamId) == 0xFFFFFFFFF95D47FE, "variant 1: the complement of the account id under all ones");
        Check.That(MandarinUserId.FromSteamId64(HostSteamId, 0) == HostSteamId, "variant 0: the SteamID64");
        Check.That(MandarinUserId.FromSteamId64(HostSteamId, 2) == ~HostSteamId, "variant 2: the complement of the SteamID64");
        var shuffled = HostSteamId ^ 0x1A3B5C7DD0C2B4A8;
        var expected = ~(((shuffled >> 32) & 0xFFFFFFFF) | ((shuffled & 0xFFFFFFFF) << 32));
        Check.That(MandarinUserId.FromSteamId64(HostSteamId, 3) == expected, "variant 3: the complement of the SteamID64 with its halves swapped");
        Check.That(MandarinUserId.IsSupported(3) && !MandarinUserId.IsSupported(4), "variants 0 to 3 are supported");
    }

    private static void ResignOpensForTheNewOwnerOnly()
    {
        var cipher = new MandarinCipher(DragonsDogma2Seed);
        foreach (var flavor in new[] { MandarinFlavor.Default, MandarinFlavor.Compressible })
        {
            foreach (var length in new[] { 1, 100, 20000, 150000 })
            {
                var plain = Pattern(length, 5);
                var original = MandarinSaveFile.Encrypt(plain, flavor, cipher, UserId(HostSteamId));
                var resigned = SaveResigner.Resign(original, Dd2Profile, HostSteamId, GuestSteamId);
                var label = $"{flavor}, {length} bytes";
                Check.That(MandarinSaveFile.Decrypt(original, cipher, UserId(HostSteamId)).AsSpan().SequenceEqual(plain), $"{label}: the original opens for the host");
                Check.That(MandarinSaveFile.FlavorOf(resigned) == flavor, $"{label}: the flavor is kept");
                Check.That(MandarinSaveFile.Decrypt(resigned, cipher, UserId(GuestSteamId)).AsSpan().SequenceEqual(plain), $"{label}: the re-signed file opens for the guest");
                Check.That(Refused(() => MandarinSaveFile.Decrypt(resigned, cipher, UserId(HostSteamId))), $"{label}: the re-signed file no longer opens for the host");
                Check.That(Refused(() => SaveResigner.Resign(resigned, Dd2Profile, HostSteamId, GuestSteamId)), $"{label}: re-signing from the wrong account is refused");
            }
        }
    }

    private static void BadFilesAreRefused()
    {
        var cipher = new MandarinCipher(DragonsDogma2Seed);
        var good = MandarinSaveFile.Encrypt(Pattern(5000, 1), MandarinFlavor.Default, cipher, UserId(HostSteamId));

        Check.That(Refused(() => SaveResigner.Resign(Pattern(1000, 0), Dd2Profile, HostSteamId, GuestSteamId)), "a file that is not a Mandarin save is refused");
        Check.That(Refused(() => SaveResigner.Resign(good[..100], Dd2Profile, HostSteamId, GuestSteamId)), "a truncated file is refused");
        var flipped = (byte[])good.Clone();
        flipped[good.Length / 2] ^= 1;
        Check.That(Refused(() => SaveResigner.Resign(flipped, Dd2Profile, HostSteamId, GuestSteamId)), "a file with a changed byte is refused (its signature)");
        var otherFlavor = (byte[])good.Clone();
        otherFlavor[8] = 0x11;
        Check.That(Refused(() => SaveResigner.Resign(otherFlavor, Dd2Profile, HostSteamId, GuestSteamId)), "a file with an unknown flavor is refused");
        var otherSeed = new ResignProfile(KindName, "1", DragonsDogma2Variant);
        Check.That(Refused(() => SaveResigner.Resign(good, otherSeed, HostSteamId, GuestSteamId)), "a file read with another game's seed is refused");
    }

    private static void ResignFileLeavesAFailedFileAlone()
    {
        var folder = Path.Combine(Path.GetTempPath(), "cf_resign_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        try
        {
            var cipher = new MandarinCipher(DragonsDogma2Seed);
            var plain = Pattern(3000, 2);
            var path = Path.Combine(folder, "data000.bin");
            var original = MandarinSaveFile.Encrypt(plain, MandarinFlavor.Default, cipher, UserId(HostSteamId));
            File.WriteAllBytes(path, original);

            SaveResigner.ResignFile(path, Dd2Profile, HostSteamId, GuestSteamId);
            Check.That(MandarinSaveFile.Decrypt(File.ReadAllBytes(path), cipher, UserId(GuestSteamId)).AsSpan().SequenceEqual(plain), "ResignFile rewrites the file for the guest");

            var refused = Refused(() => SaveResigner.ResignFile(path, Dd2Profile, HostSteamId, GuestSteamId));
            Check.That(refused && MandarinSaveFile.Decrypt(File.ReadAllBytes(path), cipher, UserId(GuestSteamId)).AsSpan().SequenceEqual(plain),
                "ResignFile on a file of another account throws and leaves the file as it was");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static void ProfileKeyIsValidated()
    {
        var dd2 = GameProfile.Load("dd2").SaveSync!.Resign;
        Check.That(dd2 is { Kind: KindName, Variant: DragonsDogma2Variant } && dd2.Seed == DragonsDogma2Seed.ToString(), "the dd2 profile names the mandarin re-sign with its seed and variant");
        Check.That(SaveResigner.Problem(dd2!) == null, "the dd2 resign key is usable");
        Check.That(SaveResigner.Problem(new ResignProfile(KindName, "0x" + DragonsDogma2Seed.ToString("X"), DragonsDogma2Variant)) == null, "a 0x-hex seed is usable");
        Check.That(SaveResigner.Problem(new ResignProfile("other", "1", 0)) != null, "an unknown kind is a problem");
        Check.That(SaveResigner.Problem(new ResignProfile(KindName, "seed", 0)) != null, "a seed that is not a number is a problem");
        Check.That(SaveResigner.Problem(new ResignProfile(KindName, "18446744073709551616", 0)) != null, "a seed above 64 bits is a problem");
        Check.That(SaveResigner.Problem(new ResignProfile(KindName, "1", 9)) != null, "an unknown variant is a problem");

        var hexSeed = new ResignProfile(KindName, "0x" + DragonsDogma2Seed.ToString("X"), DragonsDogma2Variant);
        var cipher = new MandarinCipher(DragonsDogma2Seed);
        var file = MandarinSaveFile.Encrypt(Pattern(500, 4), MandarinFlavor.Default, cipher, UserId(HostSteamId));
        Check.That(MandarinSaveFile.Decrypt(SaveResigner.Resign(file, hexSeed, HostSteamId, GuestSteamId), cipher, UserId(GuestSteamId)).Length == 500,
            "the hex seed re-signs like the decimal one");

        Check.That(ParseThrows("""{ "kind": "other", "seed": "1", "variant": 0 }"""), "a profile with an unknown resign kind throws at load");
        Check.That(ParseThrows("""{ "kind": "mandarin", "seed": "x", "variant": 0 }"""), "a profile with a bad resign seed throws at load");
        Check.That(ParseThrows("""{ "kind": "mandarin", "seed": "1", "variant": 7 }"""), "a profile with a bad resign variant throws at load");
    }

    private static bool ParseThrows(string resignJson)
    {
        var json = """{ "id": "r", "name": "R", "steamAppId": 1, "exe": "r.exe", "maxPlayers": 2, "port": 1, "adapterFiles": [], "adapterIni": "", "saveSync": { "steamRemoteFiles": ["a.bin"], "sessionDir": "s", "resign": """
                   + resignJson + " } }";
        try
        {
            GameProfile.Parse("r", json, RepoPaths.RepoRoot);
            return false;
        }
        catch (InvalidDataException e)
        {
            return e.Message.Contains("saveSync.resign");
        }
    }

    private static bool Refused(Action action)
    {
        try
        {
            action();
            return false;
        }
        catch (InvalidDataException)
        {
            return true;
        }
    }
}
