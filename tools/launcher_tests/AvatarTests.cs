using System.Drawing.Imaging;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>Avatar pixel conversion, the least-recently-used cache and the store that builds bitmaps.</summary>
public static class AvatarTests
{
    private sealed class Tracked : IDisposable
    {
        public int Disposals { get; private set; }

        public void Dispose() => Disposals++;
    }

    public static void Run()
    {
        Pixels();
        Replacing();
        Eviction();
        Recency();
        Clearing();
        Store();
    }

    private static void Pixels()
    {
        var bgra = AvatarPixels.RgbaToBgra(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, 2, 1);
        Check.That(bgra.SequenceEqual(new byte[] { 3, 2, 1, 4, 7, 6, 5, 8 }), "RGBA becomes BGRA, alpha kept");
        var threw = false;
        try
        {
            AvatarPixels.RgbaToBgra(new byte[7], 2, 1);
        }
        catch (ArgumentException)
        {
            threw = true;
        }
        Check.That(threw, "a wrong length throws");
    }

    private static void Replacing()
    {
        var cache = new AvatarCache<Tracked>(2);
        var first = new Tracked();
        var second = new Tracked();
        cache.Put(1, first);
        cache.Put(1, second);
        Check.That(first.Disposals == 1 && second.Disposals == 0 && cache.Count == 1, "putting an id again disposes the old value");
        cache.Put(1, second);
        Check.That(second.Disposals == 0, "putting the same value again does not dispose it");
    }

    private static void Eviction()
    {
        var cache = new AvatarCache<Tracked>(2);
        var a = new Tracked();
        var b = new Tracked();
        var c = new Tracked();
        cache.Put(1, a);
        cache.Put(2, b);
        cache.Put(3, c);
        Check.That(a.Disposals == 1 && b.Disposals == 0 && c.Disposals == 0 && cache.Count == 2, "the least recently used value is evicted and disposed");
        Check.That(!cache.TryGet(1, out _) && cache.TryGet(2, out var found) && found == b, "the evicted id is gone, the others stay");
    }

    private static void Recency()
    {
        var cache = new AvatarCache<Tracked>(2);
        var a = new Tracked();
        var b = new Tracked();
        var c = new Tracked();
        cache.Put(1, a);
        cache.Put(2, b);
        cache.TryGet(1, out _);
        cache.Put(3, c);
        Check.That(b.Disposals == 1 && a.Disposals == 0, "reading a value keeps it from being the next evicted");
    }

    private static void Clearing()
    {
        var cache = new AvatarCache<Tracked>(4);
        var values = Enumerable.Range(0, 3).Select(_ => new Tracked()).ToList();
        for (var i = 0; i < values.Count; i++) cache.Put((ulong)i, values[i]);
        cache.Clear();
        Check.That(values.All(v => v.Disposals == 1) && cache.Count == 0, "Clear disposes everything");
    }

    private static void Store()
    {
        using var store = new AvatarStore();
        ulong changed = 0;
        store.Changed += id => changed = id;
        store.Put(7, 2, 1, new byte[] { 10, 20, 30, 255, 40, 50, 60, 128 });
        var bitmap = store.Get(7);
        Check.That(changed == 7 && bitmap != null, "Put stores a bitmap and raises Changed");
        if (bitmap == null) return;
        Check.That(bitmap.PixelFormat == PixelFormat.Format32bppArgb, "the bitmap is 32bpp ARGB");
        Check.That(bitmap.GetPixel(0, 0) == Color.FromArgb(255, 10, 20, 30) && bitmap.GetPixel(1, 0) == Color.FromArgb(128, 40, 50, 60), "the pixels keep their colours and alpha");
        changed = 0;
        store.Put(8, 3, 3, new byte[5]);
        Check.That(changed == 0 && store.Get(8) == null, "bytes of the wrong size are ignored");
    }
}
