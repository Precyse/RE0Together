namespace CoopLauncher;

/// <summary>Peer-to-peer frame carrier. Identity is a SteamID (Steam) or a UDP port (local test).</summary>
public interface ITransport : IDisposable
{
    ulong LocalId { get; }

    /// <summary>Raised from Pump with the sender's transport identity; the slot byte is untrusted.</summary>
    event Action<ulong, Frame> FrameReceived;

    /// <summary>Largest FILE_CHUNK data size this carrier can send in one message.</summary>
    int MaxChunkBytes { get; }

    /// <summary>Returns false when the carrier refused the frame (e.g. its send buffer is full); the caller may retry.</summary>
    bool Send(ulong peerId, Frame frame);

    void Pump();
}
