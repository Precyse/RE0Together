using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>ModInstaller.ContentHash: SHA-256 over the installed adapter files in profile order, each as the lowercase dst name with forward slashes, 0x00, u64 LE length, bytes.</summary>
public static class ContentHashTests
{
    private static readonly byte[] FirstBytes = [1, 2, 3, 4, 5];
    private static readonly byte[] SecondBytes = [9, 8, 7];

    private static GameProfile ProfileOf(params string[] dsts) =>
        new("t", "Test", 1, "t.exe", 2, 1, dsts.Select(dst => new AdapterFile("adapters/" + dst, dst)).ToList(), "");

    private static byte[] Expected(params (string Name, byte[] Bytes)[] files)
    {
        var all = new List<byte>();
        foreach (var (name, bytes) in files)
        {
            var length = new byte[sizeof(ulong)];
            BinaryPrimitives.WriteUInt64LittleEndian(length, (ulong)bytes.Length);
            all.AddRange(Encoding.UTF8.GetBytes(name));
            all.Add(0);
            all.AddRange(length);
            all.AddRange(bytes);
        }
        return SHA256.HashData(all.ToArray());
    }

    public static void Run()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_hash_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "Sub"));
        try
        {
            File.WriteAllBytes(Path.Combine(dir, "First.DLL"), FirstBytes);
            File.WriteAllBytes(Path.Combine(dir, "Sub", "Second.ini"), SecondBytes);
            var profile = ProfileOf("First.DLL", "Sub\\Second.ini");

            var hash = ModInstaller.ContentHash(profile, dir);
            Check.That(hash != null && hash.SequenceEqual(Expected(("first.dll", FirstBytes), ("sub/second.ini", SecondBytes))),
                "hash = SHA-256 of lowercase slash name, 0x00, u64 LE length, bytes, per file in profile order");

            var reversed = ModInstaller.ContentHash(ProfileOf("Sub\\Second.ini", "First.DLL"), dir);
            Check.That(reversed != null && hash != null && !reversed.SequenceEqual(hash), "profile order changes the hash");

            File.WriteAllBytes(Path.Combine(dir, "Sub", "Second.ini"), [9, 8, 6]);
            var changed = ModInstaller.ContentHash(profile, dir);
            Check.That(changed != null && hash != null && !changed.SequenceEqual(hash), "one changed byte changes the hash");

            File.Delete(Path.Combine(dir, "First.DLL"));
            Check.That(ModInstaller.ContentHash(profile, dir) == null, "a missing file: null (not installed)");
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
