using Steamworks;

namespace CoopLauncher;

/// <summary>A Steam avatar as the window needs it: RGBA bytes, row by row from the top.</summary>
public sealed record AvatarImage(ulong Id, int Width, int Height, byte[] Rgba);

/// <summary>
/// Reads the large Steam avatars of the users the window wants (the local player, lobby members, friends hosting): an
/// avatar Steam has not loaded yet is waited for (AvatarImageLoaded_t, or PersonaStateChange_t for a user whose information
/// was requested), and an avatar that changes is delivered again. Loop thread only: every call and every callback runs
/// inside the Steam pump.
/// </summary>
internal sealed class SteamAvatars : IDisposable
{
    private const int BytesPerPixel = 4;
    private const int NoAvatar = 0;
    private const int Loading = -1;

    private readonly Callback<AvatarImageLoaded_t> _imageLoaded;
    private readonly Callback<PersonaStateChange_t> _personaChanged;
    private readonly HashSet<ulong> _wanted = new();
    private readonly HashSet<ulong> _informationRequested = new();
    private readonly Dictionary<ulong, AvatarImage> _delivered = new();
    private readonly Queue<ulong> _attempts = new();

    public SteamAvatars()
    {
        _imageLoaded = Callback<AvatarImageLoaded_t>.Create(loaded => Retry(loaded.m_steamID.m_SteamID));
        _personaChanged = Callback<PersonaStateChange_t>.Create(changed =>
        {
            if ((changed.m_nChangeFlags & EPersonaChange.k_EPersonaChangeAvatar) != 0) Retry(changed.m_ulSteamID);
        });
    }

    /// <summary>Raised from <see cref="Pump"/> with each avatar as it becomes available.</summary>
    public event Action<AvatarImage>? Loaded;

    /// <summary>The users whose avatars are wanted from now on; one that is not delivered yet is looked up on the next pump.</summary>
    public void Want(IEnumerable<ulong> ids)
    {
        var next = ids.ToHashSet();
        foreach (var gone in _wanted.Except(next).ToList()) _delivered.Remove(gone);
        foreach (var added in next.Except(_wanted)) _attempts.Enqueue(added);
        _wanted.Clear();
        _wanted.UnionWith(next);
    }

    /// <summary>Looks up the avatars that are due.</summary>
    public void Pump()
    {
        while (_attempts.TryDequeue(out var id))
            if (_wanted.Contains(id)) TryDeliver(id);
    }

    /// <summary>Raises <see cref="Loaded"/> again for every avatar delivered so far (a window that subscribed late).</summary>
    public void ResendAll()
    {
        foreach (var image in _delivered.Values) Loaded?.Invoke(image);
    }

    public void Dispose()
    {
        _imageLoaded.Dispose();
        _personaChanged.Dispose();
    }

    private void Retry(ulong id)
    {
        if (_wanted.Contains(id)) _attempts.Enqueue(id);
    }

    private void TryDeliver(ulong id)
    {
        var user = new CSteamID(id);
        var handle = SteamFriends.GetLargeFriendAvatar(user);
        if (handle == Loading) return;
        if (handle == NoAvatar)
        {
            if (_informationRequested.Add(id)) SteamFriends.RequestUserInformation(user, false);
            return;
        }
        if (!SteamUtils.GetImageSize(handle, out var width, out var height)) return;
        var rgba = new byte[BytesPerPixel * (int)width * (int)height];
        if (!SteamUtils.GetImageRGBA(handle, rgba, rgba.Length)) return;
        var image = new AvatarImage(id, (int)width, (int)height, rgba);
        _delivered[id] = image;
        Loaded?.Invoke(image);
    }
}
