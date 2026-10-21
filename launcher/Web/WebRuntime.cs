using Microsoft.Web.WebView2.Core;
using System.Diagnostics;

namespace CoopLauncher.Web;

/// <summary>The Microsoft Edge WebView2 Runtime the window needs: whether it is installed and new enough, and the dialog that
/// explains when it is not.</summary>
internal static class WebRuntime
{
    /// <summary>The oldest runtime the page's features are relied on in.</summary>
    public const string MinRuntime = "130.0.0.0";

    private const string DownloadUrl = "https://go.microsoft.com/fwlink/p/?LinkId=2124703";
    private const string MissingWord = "missing";
    private const string DialogHeading = "Microsoft Edge WebView2 Runtime is required";
    private const string DialogLink = "Download the WebView2 Runtime";
    private const string DialogTitle = "Co-op Launcher";
    private const string CloseLabel = "Close";

    /// <summary>The installed runtime's version, or null when there is none (or it cannot be read).</summary>
    public static string? Version()
    {
        try
        {
            return CoreWebView2Environment.GetAvailableBrowserVersionString();
        }
        catch (Exception e) when (e is WebView2RuntimeNotFoundException or BadImageFormatException or DllNotFoundException)
        {
            return null;
        }
    }

    /// <summary>Whether a version is at least <see cref="MinRuntime"/>.</summary>
    public static bool IsNewEnough(string version) => CoreWebView2Environment.CompareBrowserVersions(version, MinRuntime) >= 0;

    /// <summary>The usable runtime's version, or null when it is missing or older than the minimum.</summary>
    public static string? UsableVersion() => Version() is { } version && IsNewEnough(version) ? version : null;

    /// <summary><c>--webview-check</c>: prints the runtime version (or <c>missing</c>) and returns the exit code, without opening a
    /// window. 0 when the runtime is usable.</summary>
    public static int Check()
    {
        var version = Version();
        Console.WriteLine(version ?? MissingWord);
        return version != null && IsNewEnough(version) ? 0 : 1;
    }

    /// <summary>True when the runtime is usable; otherwise explains in a native dialog (with the download link) and returns false.</summary>
    public static bool EnsureOrExplain()
    {
        if (UsableVersion() != null) return true;
        Explain();
        return false;
    }

    /// <summary>The dialog for a runtime that is missing (or that stopped working): the heading, the download link, Close.</summary>
    public static void Explain()
    {
        var page = new TaskDialogPage
        {
            Caption = DialogTitle,
            Heading = DialogHeading,
            Text = $"<a href=\"{DownloadUrl}\">{DialogLink}</a>",
            Icon = TaskDialogIcon.Error,
            EnableLinks = true,
            AllowCancel = true,
            Buttons = { new TaskDialogButton(CloseLabel) },
        };
        page.LinkClicked += (_, e) => OpenLink(e.LinkHref);
        TaskDialog.ShowDialog(page);
    }

    private static void OpenLink(string address)
    {
        if (address != DownloadUrl) return;
        Process.Start(new ProcessStartInfo(address) { UseShellExecute = true });
    }
}
