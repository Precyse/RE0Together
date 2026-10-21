using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>The home hero: the selected game's Steam art 296 px tall, shaded from the left and faded into the window
/// background at the bottom, its logo at the top left, and the action row on the art: HOST (LAUNCH for a game that matches its
/// players itself), the mod button when the mod needs something, the Lobby code field with Join, the options cog and, for a game
/// with a character select, the Characters button. A notice (a refusal or an invite) sits under the row and the hero grows by it.
/// It reports what was clicked.</summary>
internal sealed class HeroBanner : Control
{
    private const string HostLabel = "HOST";
    private const string LaunchLabel = "LAUNCH";
    private const string CodeLabel = "Lobby code";
    private const string OptionsGlyph = "";
    private const string CharactersGlyph = "";
    private const int ShadeLeftAlpha = 230;
    private const int ShadeMidAlpha = 128;
    private const float ShadeMid = 0.45f;
    private const float ShadeEnd = 0.75f;
    private const float FadeStart = 0.35f;
    private const int FadeMidAlpha = 191;
    private const float FadeMid = 0.70f;

    private static readonly int ArtHeight = Theme.Scale(296);
    private static readonly int Pad = Theme.Scale(24);
    private static readonly int RowBottom = Theme.Scale(20);
    private static readonly int RowGap = Theme.Scale(12);
    private static readonly int RowHeight = Theme.Scale(44);
    private static readonly int HostWidth = Theme.Scale(140);
    private static readonly int JoinWidth = Theme.Scale(96);
    private static readonly int IconSize = Theme.Scale(44);
    private static readonly int CodeLabelGap = Theme.Scale(8);
    private static readonly Rectangle LogoBox = new(Theme.Scale(24), Theme.Scale(40), Theme.Scale(240), Theme.Scale(110));
    private static readonly Font CodeFont = new("Consolas", 11.25f, FontStyle.Bold);

    private readonly FlatButton _host = new(HostLabel, ButtonKind.Primary) { Height = RowHeight };
    private readonly FlatButton _mod = new("Install", ButtonKind.Normal) { Height = RowHeight, Width = Theme.ToolWidth };
    private readonly FieldBox _code = new(RowHeight, CodeFont);
    private readonly FlatButton _join = new("Join", ButtonKind.Ghost) { Size = new Size(JoinWidth, RowHeight) };
    private readonly FlatButton _options = new(OptionsGlyph, ButtonKind.Icon) { Size = new Size(IconSize, IconSize), AccessibleName = "Options" };
    private readonly FlatButton _characters = new(CharactersGlyph, ButtonKind.Icon) { Size = new Size(IconSize, IconSize), AccessibleName = "Characters" };
    private readonly NoticeRow _notice = new();
    private SteamArt? _art;
    private string _name = string.Empty;
    private bool _showsMod;
    private bool _showsCode;
    private bool _showsOptions;
    private bool _showsCharacters;
    private bool _noticeSpace;
    private Rectangle _codeLabel;

    public HeroBanner()
    {
        Dock = DockStyle.Top;
        Height = ArtHeight;
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
        Controls.AddRange([_host, _mod, _code, _join, _options, _characters, _notice]);
        _host.Click += (_, _) => HostClicked?.Invoke();
        _mod.Click += (_, _) => ModClicked?.Invoke();
        _join.Click += (_, _) => JoinRequested?.Invoke(_code.Input.Text);
        _options.Click += (_, _) => OptionsClicked?.Invoke();
        _characters.Click += (_, _) => CharactersClicked?.Invoke();
        _code.Input.KeyDown += (_, e) =>
        {
            if (e.KeyCode != Keys.Enter) return;
            e.SuppressKeyPress = true;
            JoinRequested?.Invoke(_code.Input.Text);
        };
        _notice.ActionClicked += () => NoticeActionClicked?.Invoke();
        _notice.ReportClicked += () => NoticeReportClicked?.Invoke();
        _notice.Collapsed += () =>
        {
            _noticeSpace = false;
            Fit();
        };
    }

    public event Action? HostClicked;
    public event Action? ModClicked;

    /// <summary>Join was asked for with this text in the Lobby code field.</summary>
    public event Action<string>? JoinRequested;
    public event Action? OptionsClicked;
    public event Action? CharactersClicked;
    public event Action? NoticeActionClicked;
    public event Action? NoticeReportClicked;

    /// <summary>The options cog reads at full ink while its popup is open.</summary>
    public bool OptionsOpen
    {
        set => _options.Active = value;
    }

    /// <summary>The typed lobby code was refused: the field shakes and turns amber.</summary>
    public void RefuseCode() => _code.Refuse();

    /// <summary>Shows the game's art and what the view allows. The cog is there when the game has options to set.</summary>
    public void Show(HomeView view, GameProfile game, SteamArt art, bool hasOptions)
    {
        _art = art;
        _name = game.Name;
        _host.Text = view.Native ? LaunchLabel : HostLabel;
        _host.Enabled = view.HostEnabled;
        _showsMod = view.Mod != null;
        _mod.Visible = _showsMod;
        _mod.Text = view.Mod?.Label ?? string.Empty;
        _mod.Enabled = view.ModEnabled;
        _showsCode = !view.Native;
        _code.Visible = _join.Visible = _showsCode;
        _join.Enabled = view.JoinEnabled;
        _code.Enabled = view.CodeEnabled;
        _showsOptions = _options.Visible = hasOptions;
        _showsCharacters = _characters.Visible = game.HasCharacterSelect;
        _noticeSpace |= view.Notice != null;
        Fit();
        _notice.Show(view.Notice);
        Invalidate();
    }

    protected override void OnResize(EventArgs e)
    {
        Arrange();
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        PaintArt(g);
        if (_art?.Logo is { } logo) Draw.FitBottomLeft(g, logo, LogoBox);
        else Draw.Text(g, _name.ToUpperInvariant(), Theme.Brand, Theme.Text, LogoBox);
        if (_showsCode) Draw.Text(g, CodeLabel, Theme.Body, Theme.Muted, _codeLabel);
    }

    /// <summary>The art over the top 296 px, shaded from the left and faded into the background at the bottom.</summary>
    private void PaintArt(Graphics g)
    {
        if (_art?.Hero is not { } hero || Width <= 0) return;
        var area = new Rectangle(0, 0, Width, ArtHeight);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        Draw.Cover(g, hero, area);
        PaintGradient(g, area, LinearGradientMode.Horizontal,
            [Color.FromArgb(ShadeLeftAlpha, Theme.Bg), Color.FromArgb(ShadeMidAlpha, Theme.Bg), Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg)],
            [0f, ShadeMid, ShadeEnd, 1f]);
        PaintGradient(g, area, LinearGradientMode.Vertical,
            [Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg), Color.FromArgb(FadeMidAlpha, Theme.Bg), Theme.Bg],
            [0f, FadeStart, FadeMid, 1f]);
    }

    private static void PaintGradient(Graphics g, Rectangle area, LinearGradientMode mode, Color[] colors, float[] positions)
    {
        using var brush = new LinearGradientBrush(area, Color.Black, Color.Black, mode);
        brush.InterpolationColors = new ColorBlend { Colors = colors, Positions = positions };
        g.FillRectangle(brush, area);
    }

    /// <summary>The hero is 296 px, taller by the notice row while one is shown.</summary>
    private void Fit()
    {
        var height = ArtHeight + (_noticeSpace ? Theme.ControlHeight : 0);
        if (Height != height) Height = height;
        Arrange();
    }

    /// <summary>The action row from the left (HOST, the mod button, the code label, the field, Join) and from the right (the
    /// icon buttons), the field taking what is left; the notice under it.</summary>
    private void Arrange()
    {
        var y = ArtHeight - RowBottom - RowHeight;
        var x = Pad;
        _host.SetBounds(x, y, HostWidth, RowHeight);
        x += HostWidth + RowGap;
        if (_showsMod)
        {
            _mod.SetBounds(x, y, _mod.Width, RowHeight);
            x += _mod.Width + RowGap;
        }
        var right = Width - Pad;
        foreach (var (icon, shown) in new[] { (_characters, _showsCharacters), (_options, _showsOptions) })
        {
            if (!shown) continue;
            right -= IconSize;
            icon.Location = new Point(right, y);
            right -= RowGap;
        }
        if (_showsCode)
        {
            x += CodeLabelGap;
            var labelWidth = Draw.Width(CodeLabel, Theme.Body);
            _codeLabel = new Rectangle(x, y, labelWidth, RowHeight);
            x += labelWidth + RowGap;
            _join.Location = new Point(right - JoinWidth, y);
            _code.SetBounds(x, y, Math.Max(0, _join.Left - RowGap - x), RowHeight);
        }
        _notice.SetBounds(Pad, y + RowHeight + RowGap, Math.Max(0, Width - 2 * Pad), Theme.ControlHeight);
    }
}
