namespace CoopLauncher.Gui;

/// <summary>The joining screen: the game's art under a 420 px column in the middle of the window, the logo, the host (avatar,
/// <c>Joining &lt;host&gt;</c> and the seat under it), the steps that get the player into the game, and the buttons: Cancel,
/// the fix a refusal offers, Start again. Everything on it moves through the motion engine: steps draw their check or shake
/// their cross, numbers roll, bars tween, texts swap. Shown with a <see cref="JoiningModel"/>; reports the three buttons.</summary>
internal sealed class JoiningScreen : Control
{
    private const float BackdropOpacity = 0.18f;

    private static readonly int ColumnWidth = Theme.Scale(420);
    private static readonly Size LogoBox = new(Theme.Scale(200), Theme.Scale(80));
    private static readonly int Gap = Theme.Scale(24);
    private static readonly int ButtonGap = Theme.Scale(12);
    private static readonly int AvatarSize = Theme.Scale(40);
    private static readonly int AvatarGap = Theme.Scale(12);
    private static readonly int StepsRowGap = Theme.Scale(14);
    private static readonly Size CancelSize = new(Theme.Scale(120), Theme.Scale(40));
    private static readonly Size CancelFixSize = new(Theme.Scale(120), Theme.Scale(44));
    private static readonly Size FixSize = new(Theme.Scale(160), Theme.Scale(44));
    private static readonly Size AgainSize = new(Theme.Scale(140), Theme.Scale(40));

    private readonly AvatarStore _avatars;
    private readonly BackdropLayer _backdrop = new();
    private readonly StepList _steps = new(StepSize.Large) { RowGap = StepsRowGap, Background = Theme.Bg };
    private readonly StatusLine _title;
    private readonly StatusLine _sub;
    private readonly FlatButton _cancel = new("Cancel", ButtonKind.Ghost);
    private readonly FlatButton _fix = new(string.Empty, ButtonKind.Primary);
    private readonly FlatButton _again = new("Start again", ButtonKind.Ghost);
    private SteamArt? _art;
    private JoiningModel? _model;
    private Rectangle _logo;
    private Rectangle _avatar;

    public JoiningScreen(AvatarStore avatars)
    {
        _avatars = avatars;
        _title = new StatusLine(this, "title");
        _sub = new StatusLine(this, "sub");
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        Controls.AddRange([_steps, _fix, _again, _cancel]);
        _cancel.Click += (_, _) => CancelClicked?.Invoke();
        _fix.Click += (_, _) => FixClicked?.Invoke();
        _again.Click += (_, _) => StartAgainClicked?.Invoke();
        avatars.Changed += OnAvatarArrived;
    }

    public event Action? CancelClicked;
    public event Action? FixClicked;
    public event Action? StartAgainClicked;

    public void Show(JoiningModel model, SteamArt art)
    {
        _model = model;
        _art = art;
        _title.Set(model.Title);
        _sub.Set(model.Sub);
        _steps.Show(model.Steps);
        _fix.Visible = model.Fix != NoticeFix.None;
        _fix.Text = model.FixLabel;
        _fix.Enabled = model.FixEnabled;
        _again.Visible = model.StartAgain;
        Arrange();
        Invalidate();
    }

    protected override void OnResize(EventArgs e)
    {
        Arrange();
        base.OnResize(e);
    }

    protected override void OnPaintBackground(PaintEventArgs e) => _backdrop.Paint(e.Graphics, ClientSize, _art?.Hero, BackdropOpacity);

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_model == null) return;
        var g = e.Graphics;
        SessionArt.PaintLogo(g, _art?.Logo, _logo);
        SessionArt.PaintAvatar(g, _avatars, _model.HostId, _model.HostName, _avatar, Theme.Hero);
        var textX = _avatar.Right + AvatarGap;
        var textWidth = Math.Max(0, _logo.X + ColumnWidth - textX);
        var hasSub = _sub.Text.Length > 0 || _model.Sub.Length > 0;
        var height = Theme.Title.Height + (hasSub ? Theme.StepSmall.Height : 0);
        var top = _avatar.Y + (_avatar.Height - height) / 2;
        _title.Paint(g, new Rectangle(textX, top, textWidth, Theme.Title.Height), Theme.Title, Theme.Text, Theme.Bg);
        if (hasSub) _sub.Paint(g, new Rectangle(textX, top + Theme.Title.Height, textWidth, Theme.StepSmall.Height), Theme.StepSmall, Theme.Muted, Theme.Bg);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _avatars.Changed -= OnAvatarArrived;
            _backdrop.Dispose();
        }
        base.Dispose(disposing);
    }

    private void OnAvatarArrived(ulong id)
    {
        if (_model?.HostId == id) Invalidate(_avatar);
    }

    /// <summary>Stacks the column (logo, header, steps, buttons) and centres it in the window both ways.</summary>
    private void Arrange()
    {
        if (_model == null) return;
        var width = Math.Min(ColumnWidth, Width);
        _steps.Width = width;
        var buttons = _fix.Visible ? CancelFixSize.Height : CancelSize.Height;
        var total = LogoBox.Height + Gap + AvatarSize + Gap + _steps.Height + Gap + buttons;
        var x = (Width - width) / 2;
        var y = Math.Max(0, (Height - total) / 2);
        _logo = new Rectangle(x, y, LogoBox.Width, LogoBox.Height);
        y += LogoBox.Height + Gap;
        _avatar = new Rectangle(x, y, AvatarSize, AvatarSize);
        y += AvatarSize + Gap;
        _steps.Location = new Point(x, y);
        y += _steps.Height + Gap;
        ArrangeButtons(x, y);
    }

    /// <summary>The fix first, then Start again, then Cancel, on one row.</summary>
    private void ArrangeButtons(int x, int y)
    {
        if (_fix.Visible)
        {
            _fix.SetBounds(x, y, FixSize.Width, FixSize.Height);
            x += FixSize.Width + ButtonGap;
        }
        if (_again.Visible)
        {
            _again.SetBounds(x, y, AgainSize.Width, AgainSize.Height);
            x += AgainSize.Width + ButtonGap;
        }
        var cancel = _fix.Visible ? CancelFixSize : CancelSize;
        _cancel.SetBounds(x, y, cancel.Width, cancel.Height);
    }
}
