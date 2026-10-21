using CoopLauncher.Gui;
using System.Drawing.Imaging;

namespace CoopLauncher.Web;

/// <summary>The Steam avatars the page may show: each is encoded to PNG once when it arrives from Steam, kept in a small cache, and
/// served by the image host. <see cref="Revision"/> is what the snapshot carries in the avatar URL: it is null until the avatar
/// arrived and changes when a new picture for the same id arrives.</summary>
internal sealed class AvatarImages
{
    public const int Capacity = 128;

    private readonly Lru<ulong, byte[]> _png = new(Capacity);
    private readonly Dictionary<ulong, int> _revisions = new();   // UI thread only

    /// <summary>Stores the avatar from Steam's RGBA bytes; bytes that do not match the size are logged and ignored. UI thread.</summary>
    public void Put(ulong id, int width, int height, byte[] rgba)
    {
        try
        {
            using var bitmap = AvatarPixels.ToBitmap(AvatarPixels.RgbaToBgra(rgba, width, height), width, height);
            using var stream = new MemoryStream();
            bitmap.Save(stream, ImageFormat.Png);
            _png.Put(id, stream.ToArray());
        }
        catch (ArgumentException e)
        {
            Log.Info($"Avatar {id} ignored: {e.Message}");
            return;
        }
        _revisions[id] = _revisions.GetValueOrDefault(id) + 1;
    }

    /// <summary>The revision of an avatar that arrived and is still cached, else null. UI thread.</summary>
    public int? Revision(ulong id) => _png.Contains(id) && _revisions.TryGetValue(id, out var revision) ? revision : null;

    /// <summary>The PNG bytes of an avatar, or null. Any thread.</summary>
    public byte[]? Png(ulong id) => _png.TryGet(id, out var bytes) ? bytes : null;
}
