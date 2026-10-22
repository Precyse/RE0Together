using System.Buffers.Binary;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>
/// One machine's build, as BUILD_INFO carries it: launcher build (the release build in installed.json; 0 = dev build), installed game build (Steam
/// buildid; 0 = unknown), and the hash of the installed mod files (<see cref="ModInstaller.ContentHash"/>; null = not
/// installed or unknown, sent as all zero). A payload from an older launcher is shorter: the missing fields skip their check.
/// </summary>
public sealed record BuildInfo(int LauncherBuild, int GameBuild, byte[]? ModHash)
{
    public const int DevBuild = 0;
    public const int HashBytes = SHA256.HashSizeInBytes;

    private const int IntBytes = sizeof(int);
    private const int GameBuildEnd = IntBytes + IntBytes;

    public byte[] Encode()
    {
        var payload = new byte[GameBuildEnd + HashBytes];
        BinaryPrimitives.WriteInt32LittleEndian(payload, LauncherBuild);
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(IntBytes), GameBuild);
        ModHash?.CopyTo(payload, GameBuildEnd);
        return payload;
    }

    public static bool TryDecode(byte[] payload, out BuildInfo info)
    {
        info = new BuildInfo(DevBuild, GameBuilds.Unknown, null);
        if (payload.Length < IntBytes) return false;
        var game = payload.Length >= GameBuildEnd ? BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(IntBytes)) : GameBuilds.Unknown;
        var hash = payload.Length >= GameBuildEnd + HashBytes ? payload[GameBuildEnd..(GameBuildEnd + HashBytes)] : null;
        info = new BuildInfo(BinaryPrimitives.ReadInt32LittleEndian(payload), game, hash is null || hash.All(b => b == 0) ? null : hash);
        return true;
    }

    /// <summary>The first difference found, checked in the order launcher build, game build, mod. A dev build has no number to
    /// compare, so only the mod hash guards it.</summary>
    public static BuildMismatch Compare(BuildInfo host, BuildInfo local)
    {
        var bothNumbered = host.LauncherBuild != DevBuild && local.LauncherBuild != DevBuild;
        if (bothNumbered && host.LauncherBuild != local.LauncherBuild) return BuildMismatch.LauncherBuild;
        var bothKnown = host.GameBuild != GameBuilds.Unknown && local.GameBuild != GameBuilds.Unknown;
        if (bothKnown && host.GameBuild != local.GameBuild) return BuildMismatch.GameBuild;
        if (host.ModHash is null) return BuildMismatch.None;
        if (local.ModHash is null) return BuildMismatch.ModNotInstalled;
        return host.ModHash.AsSpan().SequenceEqual(local.ModHash) ? BuildMismatch.None : BuildMismatch.ModFiles;
    }

    /// <summary>The refusal detail of a mismatch, <see cref="RefusalVersions"/> of what differs: launcher builds, game builds, or
    /// the installed mod builds (<paramref name="hostModBuild"/> is the host's <c>cf_mod</c>; the mod hash carries no number).
    /// Empty for a mismatch with no numbers.</summary>
    public static string Detail(BuildMismatch mismatch, BuildInfo host, BuildInfo local, int hostModBuild, int localModBuild) => mismatch switch
    {
        BuildMismatch.LauncherBuild => RefusalVersions.Format(host.LauncherBuild, local.LauncherBuild),
        BuildMismatch.GameBuild => RefusalVersions.Format(host.GameBuild, local.GameBuild),
        BuildMismatch.ModFiles => RefusalVersions.Format(hostModBuild, localModBuild),
        _ => string.Empty,
    };
}

/// <summary>
/// Players must run the same build. The host sends its BUILD_INFO to every peer as it joins, ahead of the save transfer; a
/// guest compares it with its own and refuses to start the game on a mismatch (see <see cref="BuildInfo.Compare"/>).
/// </summary>
public sealed class BuildCheck
{
    private const long WaitMs = 10_000;

    private readonly GameProfile _profile;
    private readonly string? _gameDir;
    private readonly ITransport _transport;
    private readonly Session _session;
    private readonly bool _isHost;
    private readonly long _startedMs = Environment.TickCount64;
    private bool _heard;

    private BuildCheck(GameProfile profile, string? gameDir, ITransport transport, Session session)
    {
        _profile = profile;
        _gameDir = gameDir;
        _transport = transport;
        _session = session;
        _isHost = session.IsHost;
    }

    /// <summary>This package's release build (<see cref="InstalledRelease"/>); 0 for a dev build.</summary>
    public static int LocalBuild() => InstalledRelease.ReadBuild(InstalledRelease.PathFor(AppContext.BaseDirectory));

    /// <summary>Subscribe before the save sync, so BUILD_INFO goes out ahead of the save files.</summary>
    public static BuildCheck Create(GameProfile profile, string? gameDir, Session session, ITransport transport)
    {
        var check = new BuildCheck(profile, gameDir, transport, session);
        session.PeerJoined += check.SendTo;
        session.BuildInfoReceived += check.OnBuildInfo;
        return check;
    }

    /// <summary>The game may start: on the host always; on a guest once the host's build matched (or did not arrive in time).</summary>
    public bool Ready => _isHost || _heard || Environment.TickCount64 - _startedMs > WaitMs;

    /// <summary>Set when the builds differ; the session must not go on.</summary>
    public string? Mismatch { get; private set; }

    /// <summary>What differs; <see cref="BuildMismatch.None"/> while <see cref="Mismatch"/> is null.</summary>
    public BuildMismatch Reason { get; private set; }

    /// <summary>The host's and this machine's build as compared (null before the host's BUILD_INFO arrived).</summary>
    public (BuildInfo Host, BuildInfo Local)? Compared { get; private set; }

    private BuildInfo LocalInfo() => new(LocalBuild(), GameBuilds.Installed(_profile, _gameDir),
        _gameDir is null ? null : ModInstaller.ContentHash(_profile, _gameDir));

    private void SendTo(ulong peer)
    {
        if (!_isHost) return;
        _transport.Send(peer, new Frame(Msg.BuildInfo, Framing.FlagReliable, 0, LocalInfo().Encode()));
    }

    private void OnBuildInfo(ulong sender, Frame frame)
    {
        if (_isHost || sender != _session.HostId || !BuildInfo.TryDecode(frame.Payload, out var host)) return;
        _heard = true;
        var local = LocalInfo();
        if (host.LauncherBuild != local.LauncherBuild && (host.LauncherBuild == BuildInfo.DevBuild || local.LauncherBuild == BuildInfo.DevBuild))
            Log.Info($"Build check: host build {host.LauncherBuild}, this build {local.LauncherBuild} (dev build, continuing)");
        Compared = (host, local);
        Reason = BuildInfo.Compare(host, local);
        if (Reason == BuildMismatch.None) return;
        Mismatch = Describe(Reason, host, local);
        Log.Info(Mismatch);
    }

    private string Describe(BuildMismatch reason, BuildInfo host, BuildInfo local) => reason switch
    {
        BuildMismatch.LauncherBuild =>
            $"Build mismatch: host is on build {host.LauncherBuild}, you are on build {local.LauncherBuild}. {(host.LauncherBuild < local.LauncherBuild ? "The host" : "You")} should restart the launcher to update.",
        BuildMismatch.GameBuild =>
            $"Game build mismatch: host has {_profile.Name} build {host.GameBuild}, you have build {local.GameBuild}. Both players need the same game version.",
        BuildMismatch.ModNotInstalled =>
            $"Mod not installed: the host runs the {_profile.Name} mod and you do not have it installed. Install it first.",
        _ => $"Mod files differ: your {_profile.Name} mod is not the host's. Both players need the same mod files; update or reinstall it.",
    };
}
