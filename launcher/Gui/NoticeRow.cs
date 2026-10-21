namespace CoopLauncher.Gui;

/// <summary>One line the Session section has to say: the key tells updates of the same notice from a different one. The
/// caller supplies every word.</summary>
public sealed record Notice(string Key, string Words, string? Action, bool Report);

/// <summary>The notice row of the Session section: an amber lamp, the words, an optional fix button and, for a refusal, a
/// Report button. A new notice rises in like a toast and a cleared one sinks out; the same notice updated in place changes
/// without motion. The row hides itself once gone and says so (<see cref="Collapsed"/>) so the section can resize.</summary>
internal sealed class NoticeRow : Panel
{
    private const string ReportWord = "Report";

    private static readonly int LampGap = Theme.Scale(12);

    private readonly FlatButton _action = new(string.Empty, ButtonKind.Normal) { Width = Theme.ToolWidth, Visible = false };
    private readonly FlatButton _report = new(ReportWord, ButtonKind.Ghost) { Width = Theme.ToolWidth, Visible = false };
    private Notice? _notice;
    private bool _shown;
    private bool _leaving;

    public NoticeRow()
    {
        BackColor = Theme.Bg;
        Height = Theme.ControlHeight;
        Visible = false;
        _action.Click += (_, _) => ActionClicked?.Invoke();
        _report.Click += (_, _) => ReportClicked?.Invoke();
        Controls.Add(_action);
        Controls.Add(_report);
        SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
    }

    public event Action? ActionClicked;
    public event Action? ReportClicked;

    /// <summary>The row finished going away and is hidden.</summary>
    public event Action? Collapsed;

    /// <summary>Shows the notice, or (null) clears the row.</summary>
    public void Show(Notice? notice)
    {
        if (notice == null)
        {
            Clear();
            return;
        }

        var isNew = _notice?.Key != notice.Key || !_shown || _leaving;
        _leaving = false;
        _notice = notice;
        _action.Text = notice.Action ?? string.Empty;
        _action.Visible = notice.Action != null;
        _report.Visible = notice.Report;
        Place();
        Invalidate();
        if (!isNew) return;
        _shown = true;
        Visible = true;
        PerformLayout();
        MotionOverlay.Play(this, Durations.ToastOpen, Easing.Out, Recipes.ToastIn, Theme.Bg);
    }

    protected override void OnResize(EventArgs e)
    {
        Place();
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (_notice == null) return;
        var g = e.Graphics;
        Draw.Lamp(g, Theme.Armed, 0, Height / 2);
        var textX = Theme.LampSize + LampGap;
        var right = (_notice.Action != null ? _action.Left : _notice.Report ? _report.Left : Width) - Theme.Gap;
        Draw.Text(g, _notice.Words, Theme.Strong, Theme.Text, new Rectangle(textX, 0, Math.Max(0, right - textX), Height));
    }

    private void Clear()
    {
        if (!_shown || _leaving) return;
        _leaving = true;
        MotionOverlay.Play(this, Durations.ToastClose, Easing.Out, Recipes.ToastOut, Theme.Bg, done: () =>
        {
            _leaving = false;
            _shown = false;
            _notice = null;
            Visible = false;
            Collapsed?.Invoke();
        });
    }

    /// <summary>Buttons at the right edge: the fix button left of Report.</summary>
    private void Place()
    {
        var right = Width;
        if (_notice is { Report: true })
        {
            right -= _report.Width;
            _report.Location = new Point(right, 0);
            right -= Theme.Gap;
        }
        if (_notice is { Action: not null }) _action.Location = new Point(right - _action.Width, 0);
    }
}
