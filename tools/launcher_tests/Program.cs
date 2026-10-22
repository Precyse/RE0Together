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
    /// <summary>Setting this variable to <c>web</c> runs only the web window's suites (UiCommands, UiSnapshot, UiContract); to
    /// <c>mod</c> only the mod install suites.</summary>
    private const string OnlyVariable = "LAUNCHER_TESTS_ONLY";
    private const string WebSuites = "web";
    private const string ModSuites = "mod";

    [STAThread]
    public static int Main()
    {
        var only = Environment.GetEnvironmentVariable(OnlyVariable);
        if (only == WebSuites) return RunWebSuites();
        if (only == ModSuites) return RunModSuites();
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
        LobbyListingTests.Run();
        LobbyDirectoryTests.Run();
        RetryScheduleTests.Run();
        PeerStatsTests.Run();
        StatusTextTests.Run();
        GameWatchTests.Run();
        CliOptionsTests.Run();
        LogRotationTests.Run();
        GameProfileTests.Run();
        AvatarTests.Run();
        FramingTests.Run();
        InstanceGuardTests.Run();
        UpdaterTests.Run();
        RunModInstallSuites();
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
        CardStateTests.Run();
        SessionScreensTests.Run();
        ScreensTests.Run();
        LobbyRowsTests.Run();
        SelectModelTests.Run();
        UiCommandsTests.Run();
        UiSnapshotTests.Run();
        UiContractTests.Run();
        if (Check.Failures == 0) Console.WriteLine("launcher_tests: all checks passed");
        return Check.Failures == 0 ? 0 : 1;
    }

    private static void RunModInstallSuites()
    {
        ModFilesTests.Run();
        ModInstallerTests.Run();
        ModFolderTests.Run();
        ModProgressTests.Run();
    }

    private static int RunModSuites()
    {
        GameProfileTests.Run();
        ContentHashTests.Run();
        RunModInstallSuites();
        if (Check.Failures == 0) Console.WriteLine("launcher_tests (mod): all checks passed");
        return Check.Failures == 0 ? 0 : 1;
    }

    private static int RunWebSuites()
    {
        UiCommandsTests.Run();
        UiSnapshotTests.Run();
        UiContractTests.Run();
        if (Check.Failures == 0) Console.WriteLine("launcher_tests (web): all checks passed");
        return Check.Failures == 0 ? 0 : 1;
    }
}
