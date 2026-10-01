using System.Buffers.Binary;

namespace CoopLauncher;

/// <summary>
/// Players must run the same build. The host sends its build number (BUILD_INFO) to every peer as it joins, ahead
/// of the save transfer; a guest compares it with its own and refuses to start the game on a mismatch. A dev build
/// (no version.txt, build 0) only warns.
/// </summary>
public sealed class BuildCheck
{
    public const string VersionFile = "version.txt";
    private const long WaitMs = 10_000;
    private const int DevBuild = 0;

    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly bool _isHost;
    private readonly long _startedMs = Environment.TickCount64;
    private bool _heard;

    private BuildCheck(ITransport transport, Func<ulong> hostId, bool isHost)
    {
        _transport = transport;
        _hostId = hostId;
        _isHost = isHost;
    }

    /// <summary>This package's build number from version.txt next to the exe; 0 for a dev build.</summary>
    public static int LocalBuild()
    {
        var path = Path.Combine(AppContext.BaseDirectory, VersionFile);
        return File.Exists(path) && int.TryParse(File.ReadAllText(path).Trim(), out var build) ? build : DevBuild;
    }

    /// <summary>Subscribe before the save sync, so BUILD_INFO goes out ahead of the save files.</summary>
    public static BuildCheck Create(Session session, ITransport transport, ILobby lobby)
    {
        var check = new BuildCheck(transport, () => lobby.OwnerId, lobby.OwnerId == transport.LocalId);
        session.PeerJoined += check.SendTo;
        session.BuildInfoReceived += check.OnBuildInfo;
        return check;
    }

    /// <summary>The game may start: on the host always; on a guest once the host's build matched (or did not arrive in time).</summary>
    public bool Ready => _isHost || _heard || Environment.TickCount64 - _startedMs > WaitMs;

    /// <summary>Set when the builds differ; the session must not go on.</summary>
    public string? Mismatch { get; private set; }

    private void SendTo(ulong peer)
    {
        if (!_isHost) return;
        var payload = new byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(payload, LocalBuild());
        _transport.Send(peer, new Frame(Msg.BuildInfo, Framing.FlagReliable, 0, payload));
    }

    private void OnBuildInfo(ulong sender, Frame frame)
    {
        if (_isHost || sender != _hostId() || frame.Payload.Length != sizeof(int)) return;
        _heard = true;
        var host = BinaryPrimitives.ReadInt32LittleEndian(frame.Payload);
        var local = LocalBuild();
        if (host == local) return;
        if (host == DevBuild || local == DevBuild)
        {
            Log.Info($"Build check: host build {host}, this build {local} (dev build, continuing)");
            return;
        }
        var older = host < local ? "The host" : "You";
        Mismatch = $"Build mismatch: host is on build {host}, you are on build {local}. {older} should restart the launcher to update.";
        Log.Info(Mismatch);
    }
}
