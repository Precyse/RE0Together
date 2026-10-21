// Unit tests for the launcher's pure rules (no Steam, no sockets): the mod content hash and the BUILD_INFO comparison, the
// member grace, the token buckets, the fresh-frame queue, the adapter option values. Usage: dotnet run --project tools/launcher_tests. Exit 0 when every check passes.
namespace LauncherTests;

public static class Check
{
    public static int Failures { get; private set; }

    public static void That(bool ok, string what)
    {
        if (ok) return;
        Console.WriteLine($"FAIL: {what}");
        Failures++;
    }
}

public static class Program
{
    [STAThread]
    public static int Main()
    {
        ContentHashTests.Run();
        BuildInfoTests.Run();
        MemberGraceTests.Run();
        PeerBudgetTests.Run();
        FreshQueueTests.Run();
        GameOptionsTests.Run();
        LobbyCodeTests.Run();
        SessionProgressTests.Run();
        RefusalTests.Run();
        FriendLobbiesTests.Run();
        RetryScheduleTests.Run();
        PeerStatsTests.Run();
        StatusTextTests.Run();
        GameWatchTests.Run();
        CliOptionsTests.Run();
        LogRotationTests.Run();
        GameProfileTests.Run();
        MotionTests.Run();
        BoxBlurTests.Run();
        AvatarTests.Run();
        TextChangeTests.Run();
        FramingTests.Run();
        InstanceGuardTests.Run();
        UpdaterTests.Run();
        ModInstallerTests.Run();
        SaveSenderTests.Run();
        SaveReceiverTests.Run();
        SaveSyncTimeoutTests.Run();
        SavePathsTests.Run();
        AdapterSettingsTests.Run();
        LogForwarderTests.Run();
        AppSettingsTests.Run();
        SteamLibraryTests.Run();
        ReportBundleTests.Run();
        LoopbackBridgeTests.Run();
        GameStatusTests.Run();
        SessionCardsTests.Run();
        if (Check.Failures == 0) Console.WriteLine("launcher_tests: all checks passed");
        return Check.Failures == 0 ? 0 : 1;
    }
}
