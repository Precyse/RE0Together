namespace CoopLauncher;

/// <summary>Where the files of a release come from: the release's assets by name. The updater only asks this, so tests give it a
/// folder instead of the network.</summary>
public interface IAssetSource
{
    /// <summary>Writes the asset to <paramref name="destination"/>, telling <paramref name="onBytes"/> how many bytes each chunk
    /// carried. Throws when the asset cannot be had.</summary>
    void Download(string asset, Stream destination, Action<int> onBytes, CancellationToken cancel);
}
