namespace CoopLauncher.Gui;

/// <summary>The bottom of the window: the footer strip with the newest log line, and above it the full log, shown by
/// growing the drawer. The log box is never hidden, so its place in the window's docking order never changes.</summary>
internal sealed class LogDrawer : Panel
{
    private const int LogMaxChars = 60_000;
    private const int LogKeepChars = 40_000;
    private static readonly int LogHeight = Theme.Scale(180);

    private readonly LogFooter _footer = new();
    private readonly TextBox _log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill,
        BorderStyle = BorderStyle.None, BackColor = Theme.Bg2, ForeColor = Theme.Muted, Font = Theme.Mono,
    };

    public LogDrawer()
    {
        Dock = DockStyle.Bottom;
        BackColor = Theme.Bg2;
        Height = _footer.Height;
        NativeTheme.DarkScrollbars(_log);
        Controls.Add(_log);
        Controls.Add(_footer);
        _footer.Toggled += expanded => Height = _footer.Height + (expanded ? LogHeight : 0);
    }

    public void ShowLine(string line)
    {
        _log.AppendText(line + Environment.NewLine);
        if (_log.TextLength > LogMaxChars)
        {
            _log.Text = _log.Text[^LogKeepChars..];
            _log.SelectionStart = _log.TextLength;
            _log.ScrollToCaret();
        }
        _footer.ShowLine(line);
    }
}
