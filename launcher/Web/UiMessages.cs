namespace CoopLauncher.Web;

/// <summary>Which of the two pages a surface shows.</summary>
internal enum PageKind { Main, Mini }

/// <summary>The messages the page is sent besides the snapshot (SPEC 3.1), as JSON text. Data only: the host never sends script.</summary>
internal static class UiMessages
{
    public const string ConfirmClose = "confirmClose";
    public const string Shown = "shown";
    public const string MiniClose = "miniClose";
    public const string CodeRefused = "codeRefused";
    public const string Copied = "copied";

    public static string Hello(PageKind page, bool debug, bool reducedMotion) =>
        UiJson.Write(new { type = "hello", page = page == PageKind.Main ? "main" : "mini", debug, reducedMotion });

    public static string LogLines(IEnumerable<string> lines) => UiJson.Write(new { type = "log", lines });

    public static string Event(string name) => UiJson.Write(new { type = "event", name });

    /// <summary>A state message: the snapshot's JSON object (as <see cref="UiJson"/> writes it, starting with a brace and holding at
    /// least one property) with <c>type</c> and <c>seq</c> put in front.</summary>
    public static string State(long seq, string snapshotJson)
    {
        if (snapshotJson.Length < 3 || snapshotJson[0] != '{') throw new ArgumentException("a snapshot is a JSON object", nameof(snapshotJson));
        return $"{{\"type\":\"state\",\"seq\":{seq},{snapshotJson[1..]}";
    }
}
