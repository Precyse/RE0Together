using System.Diagnostics.CodeAnalysis;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Pixel format conversion for Steam avatars and the pictures taken from a game.</summary>
public static class AvatarPixels
{
    private const int BytesPerPixel = 4;

    /// <summary>Steam's RGBA bytes as the memory order of <see cref="PixelFormat.Format32bppArgb"/> (blue, green, red,
    /// alpha); alpha is kept.</summary>
    public static byte[] RgbaToBgra(ReadOnlySpan<byte> rgba, int width, int height)
    {
        if (width < 0 || height < 0 || rgba.Length != width * height * BytesPerPixel)
            throw new ArgumentException($"{rgba.Length} bytes are not a {width}x{height} RGBA image", nameof(rgba));
        var bgra = new byte[rgba.Length];
        for (var i = 0; i < rgba.Length; i += BytesPerPixel)
        {
            bgra[i] = rgba[i + 2];
            bgra[i + 1] = rgba[i + 1];
            bgra[i + 2] = rgba[i];
            bgra[i + 3] = rgba[i + 3];
        }
        return bgra;
    }

    /// <summary>A bitmap holding BGRA bytes (<paramref name="width"/> x <paramref name="height"/> pixels, rows without padding).</summary>
    public static Bitmap ToBitmap(byte[] bgra, int width, int height)
    {
        var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        try
        {
            var row = width * BytesPerPixel;
            for (var y = 0; y < height; y++) Marshal.Copy(bgra, y * row, data.Scan0 + y * data.Stride, row);
        }
        finally
        {
            bitmap.UnlockBits(data);
        }
        return bitmap;
    }
}

/// <summary>A least-recently-used cache of disposable values by id. It owns what it holds: a value that is replaced,
/// evicted or cleared is disposed.</summary>
public sealed class AvatarCache<T> where T : class, IDisposable
{
    private readonly int _capacity;
    private readonly Dictionary<ulong, LinkedListNode<KeyValuePair<ulong, T>>> _nodes = new();
    private readonly LinkedList<KeyValuePair<ulong, T>> _recent = new();   // most recently used first

    public AvatarCache(int capacity)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(capacity, 1);
        _capacity = capacity;
    }

    public int Count => _nodes.Count;

    /// <summary>Stores the value as the most recent; the value it replaces and anything beyond the capacity are disposed.</summary>
    public void Put(ulong id, T value)
    {
        if (_nodes.TryGetValue(id, out var existing))
        {
            _recent.Remove(existing);
            if (!ReferenceEquals(existing.Value.Value, value)) existing.Value.Value.Dispose();
        }
        _nodes[id] = _recent.AddFirst(new KeyValuePair<ulong, T>(id, value));
        while (_nodes.Count > _capacity)
        {
            var oldest = _recent.Last!;
            _recent.RemoveLast();
            _nodes.Remove(oldest.Value.Key);
            oldest.Value.Value.Dispose();
        }
    }

    /// <summary>The value for the id, which becomes the most recent.</summary>
    public bool TryGet(ulong id, [NotNullWhen(true)] out T? value)
    {
        if (!_nodes.TryGetValue(id, out var node))
        {
            value = null;
            return false;
        }
        _recent.Remove(node);
        _recent.AddFirst(node);
        value = node.Value.Value;
        return true;
    }

    public void Clear()
    {
        foreach (var entry in _recent) entry.Value.Dispose();
        _recent.Clear();
        _nodes.Clear();
    }
}

/// <summary>Steam avatars as bitmaps by Steam id, shared by every control that shows a player. UI thread only. A player
/// without an avatar (yet) is drawn as a placeholder: the card fill with the first letter of the name.</summary>
internal sealed class AvatarStore : IDisposable
{
    public const int Capacity = 64;

    private readonly AvatarCache<Bitmap> _cache = new(Capacity);

    /// <summary>Raised on the UI thread after an avatar was stored for the id.</summary>
    public event Action<ulong>? Changed;

    /// <summary>Stores the avatar from Steam's RGBA bytes; bytes that do not match the size are logged and ignored.</summary>
    public void Put(ulong id, int width, int height, byte[] rgba)
    {
        Bitmap bitmap;
        try
        {
            bitmap = AvatarPixels.ToBitmap(AvatarPixels.RgbaToBgra(rgba, width, height), width, height);
        }
        catch (ArgumentException e)
        {
            Log.Info($"Avatar {id} ignored: {e.Message}");
            return;
        }
        _cache.Put(id, bitmap);
        Changed?.Invoke(id);
    }

    public Bitmap? Get(ulong id) => _cache.TryGet(id, out var bitmap) ? bitmap : null;

    /// <summary>Draws the avatar square in the box, or the placeholder when there is none.</summary>
    public void Paint(Graphics g, Rectangle box, ulong id, string name)
    {
        if (Get(id) is not { } bitmap)
        {
            PaintPlaceholder(g, box, name);
            return;
        }
        var state = g.Save();
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        g.DrawImage(bitmap, box);
        g.Restore(state);
    }

    /// <summary>The card fill with the name's first letter in capitals centred in the box (no letter for an empty name).</summary>
    public static void PaintPlaceholder(Graphics g, Rectangle box, string name)
    {
        Draw.Fill(g, Theme.Card, box);
        var initial = Initials.Of(name);
        if (initial.Length > 0) Draw.Centered(g, initial, Theme.Strong, Theme.Muted, box);
    }

    public void Dispose() => _cache.Clear();
}

/// <summary>Shows one player's avatar in a box on a control: when the avatar arrives after the box was first drawn, it
/// cross-fades from the placeholder (skeleton-reveal); an avatar that was already there is drawn without motion.</summary>
internal sealed class AvatarFade : IDisposable
{
    private readonly Control _owner;
    private readonly AvatarStore _store;
    private readonly string _channel;
    private ulong _id;
    private bool _present;
    private double? _progress;

    public AvatarFade(Control owner, AvatarStore store, string channel)
    {
        _owner = owner;
        _store = store;
        _channel = channel;
        _store.Changed += OnChanged;
    }

    /// <summary>Points the box at a player; a change of player ends any fade.</summary>
    public void Show(ulong id)
    {
        if (id == _id) return;
        _id = id;
        _present = _store.Get(id) != null;
        _progress = null;
        Motion.Cancel(_owner, _channel);
    }

    public void Paint(Graphics g, Rectangle box, string name, Color background)
    {
        if (_progress is not { } progress)
        {
            _store.Paint(g, box, _id, name);
            return;
        }
        MotionLayer.Paint(g, box, Recipes.RevealOut(progress), background, (target, rect) => AvatarStore.PaintPlaceholder(target, rect, name));
        MotionLayer.Paint(g, box, Recipes.RevealIn(progress), background, (target, rect) => _store.Paint(target, rect, _id, name));
    }

    public void Dispose()
    {
        _store.Changed -= OnChanged;
        Motion.Cancel(_owner, _channel);
    }

    private void OnChanged(ulong id)
    {
        if (id != _id || _present) return;
        _present = true;
        Motion.Animate(_owner, _channel, 0, 1, Durations.Reveal, Easing.InOut, progress =>
        {
            _progress = progress;
            _owner.Invalidate();
        }, () =>
        {
            _progress = null;
            _owner.Invalidate();
        });
    }
}
