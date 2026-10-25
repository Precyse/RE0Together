using CoopLauncher;

namespace LauncherTests;

/// <summary>Blowfish against the standard test vectors (big-endian halves, as published) and in the little-endian ECB form the saves use.</summary>
public static class BlowfishTests
{
    private static readonly (string Key, string Plain, string Cipher)[] Vectors =
    [
        ("0000000000000000", "0000000000000000", "4EF997456198DD78"),
        ("FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF", "51866FD5B85ECB8A"),
        ("3000000000000000", "1000000000000001", "7D856F9A613063F2"),
        ("1111111111111111", "1111111111111111", "2466DD878B963C9D"),
        ("0123456789ABCDEF", "1111111111111111", "61F9C3802281B096"),
    ];

    public static void Run()
    {
        StandardVectors();
        LittleEndianBlocks();
        RoundTrip();
        BadInput();
    }

    private static void StandardVectors()
    {
        foreach (var (key, plain, cipher) in Vectors)
        {
            var cipherInstance = new Blowfish(Convert.FromHexString(key));
            var (left, right) = Halves(plain);
            cipherInstance.EncryptHalves(ref left, ref right);
            Check.That($"{left:X8}{right:X8}" == cipher, $"blowfish: key {key} encrypts {plain} to {cipher} (got {left:X8}{right:X8})");
            cipherInstance.DecryptHalves(ref left, ref right);
            Check.That($"{left:X8}{right:X8}" == plain, $"blowfish: key {key} decrypts back to {plain}");
        }
    }

    private static void LittleEndianBlocks()
    {
        var blowfish = new Blowfish(new byte[8]);
        var encrypted = blowfish.EncryptEcb(new byte[8]);
        Check.That(Convert.ToHexString(encrypted) == "4597F94E78DD9861", "blowfish: the zero block under the zero key is the standard cipher with each half written little-endian");
        Check.That(blowfish.DecryptEcb(encrypted).All(b => b == 0), "blowfish: and decrypts back");
    }

    private static void RoundTrip()
    {
        var data = Enumerable.Range(0, 64).Select(i => (byte)(i * 37 + 5)).ToArray();
        var blowfish = new Blowfish(SaveFixture.Key);
        var encrypted = blowfish.EncryptEcb(data);
        Check.That(!encrypted.SequenceEqual(data) && blowfish.DecryptEcb(encrypted).SequenceEqual(data), "blowfish: a 32-byte key round-trips 64 bytes");
        Check.That(encrypted.AsSpan(0, 8).SequenceEqual(new Blowfish(SaveFixture.Key).EncryptEcb(data.AsSpan(0, 8))), "blowfish: ECB blocks are independent and a key gives the same result every time");
    }

    private static void BadInput()
    {
        var blowfish = new Blowfish(SaveFixture.Key);
        Check.That(Throws(() => blowfish.DecryptEcb(new byte[12])), "blowfish: a length that is not whole blocks is refused");
        Check.That(Throws(() => new Blowfish([])), "blowfish: an empty key is refused");
    }

    private static (uint Left, uint Right) Halves(string hex) => (Convert.ToUInt32(hex[..8], 16), Convert.ToUInt32(hex[8..], 16));

    private static bool Throws(Action action)
    {
        try
        {
            action();
            return false;
        }
        catch (ArgumentException)
        {
            return true;
        }
    }
}
