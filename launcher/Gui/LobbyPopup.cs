using System.Globalization;

namespace CoopLauncher.Gui;

/// <summary>The panel of the lobby popup: the game's art with its logo and a close button, then two columns. Left, PLAYERS n/max
/// over one row per roster entry tinted by its step (the host first; a member the roster does not name is a plain row, open seats
/// are dashed); right, Ping, Mod, Game, Network and Playing,
/// and the lobby's button (JOIN, Full, Update or a disabled Join). Shown by the <see cref="ModalLayer"/>; it reports the button
/// and the close button.</summary>
internal sealed class LobbyPopup : Control
{
    private const string HostWord = "Host";
    private const string OpenWord = "Open";
    private const string BuildFormat = "Build {0}";
    private const string PingLabel = "Ping";
    private const string ModLabel = "Mod";
    private const string GameLabel = "Game";
    private const string NetworkLabel = "Network";
    private const string PlayingLabel = "Playing";
    private const double LeftFraction = 1.2;
    private const double RightFraction = 1.0;
    private const int GridRows = 5;

    private static readonly int PanelWidth = Theme.Scale(520);
    private static readonly int PadX = Theme.Scale(20);
    private static readonly int PadTop = Theme.Scale(8);
    private static readonly int PadBottom = Theme.Scale(20);
    private static readonly int ColumnGap = Theme.Scale(20);
    private static readonly int RosterRow = Theme.Scale(32);
    private static readonly int RosterGap = Theme.Scale(8);
    private static readonly int RosterText = Theme.Scale(10);
    private static readonly int CaptionGap = Theme.Scale(12);
    private static readonly int GridPitch = Theme.Scale(23);
    private static readonly int GridLabelGap = Theme.Scale(14);
    private static readonly int GlyphSize = Theme.Scale(12);
    private static readonly int GlyphGap = Theme.Scale(8);
    private static readonly int ButtonHeight = Theme.Scale(44);
    private static readonly int ButtonGap = Theme.Scale(16);
    private static readonly string[] GridLabels = [PingLabel, ModLabel, GameLabel, NetworkLabel, PlayingLabel];

    private readonly PopupHeader _header = new();
    private readonly FlatButton _action = new("Join", ButtonKind.Primary) { Height = ButtonHeight };
    private readonly ReelText _counter;
    private readonly StatusLine _playing;
    private LobbyRowModel _model;
    private GameStatus _mod = GameStatus.Unknown;
    private int _seatCount;
    private Rectangle _left;
    private Rectangle _right;

    public LobbyPopup(LobbyRowModel model, GameStatus mod, SteamArt art, long unixNow)
    {
        _model = model;
        _counter = new ReelText(this, "players");
        _playing = new StatusLine(this, "playing");
        BackColor = Theme.Bg2;
        Controls.AddRange([_action, _header]);
        _header.Show(art);
        _header.CloseClicked += () => CloseClicked?.Invoke();
        _action.Click += (_, _) => ActionClicked?.Invoke(_model);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
        Show(model, mod, unixNow);
    }

    /// <summary>The close button was clicked.</summary>
    public event Action? CloseClicked;

    /// <summary>The button was clicked: join, or update.</summary>
    public event Action<LobbyRowModel>? ActionClicked;

    /// <summary>The lobby the popup is for.</summary>
    public LobbyListing Listing => _model.Listing;

    /// <summary>Shows this lobby (a search brought newer data of the lobby already on show) and the playing time at this clock.</summary>
    public void Show(LobbyRowModel model, GameStatus mod, long unixNow)
    {
        _model = model;
        _mod = mod;
        var listing = model.Listing;
        _seatCount = LobbyPopupModel.SeatCount(listing);
        _counter.Set(LobbyPopupModel.Caption(listing));
        LobbyRows.Configure(_action, model.Action);
        Tick(unixNow);
        Size = new Size(PanelWidth, PreferredHeight());
        Arrange();
        Invalidate();
    }

    /// <summary>The Playing timer at this clock (Unix seconds); it rolls its digits.</summary>
    public void Tick(long unixNow) => _playing.Set(LobbyPopupModel.PlayingText(_model.Listing.StartedUnix, unixNow));

    protected override void OnResize(EventArgs e)
    {
        Arrange();
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        Draw.Border(g, ClientRectangle, Theme.Line);
        PaintRoster(g);
        PaintGrid(g);
    }

    private int LeftHeight => Theme.Label.Height + CaptionGap + _seatCount * RosterRow + (_seatCount - 1) * RosterGap;

    private int RightHeight => GridRows * GridPitch + ButtonGap + ButtonHeight;

    private int PreferredHeight() => _header.Height + PadTop + Math.Max(LeftHeight, RightHeight) + PadBottom;

    /// <summary>Two columns 1.2fr / 1fr under the header; the button at the foot of the right one.</summary>
    private void Arrange()
    {
        var inner = Width - 2 * PadX - ColumnGap;
        var leftWidth = (int)(inner * LeftFraction / (LeftFraction + RightFraction));
        var top = _header.Height + PadTop;
        _left = new Rectangle(PadX, top, leftWidth, Height - top - PadBottom);
        _right = new Rectangle(_left.Right + ColumnGap, top, inner - leftWidth, _left.Height);
        _action.SetBounds(_right.X, _right.Bottom - ButtonHeight, _right.Width, ButtonHeight);
    }

    private void PaintRoster(Graphics g)
    {
        var listing = _model.Listing;
        _counter.Paint(g, new Rectangle(_left.X, _left.Y, _left.Width, Theme.Label.Height), Theme.Label, Theme.Muted, BackColor);
        var y = _left.Y + Theme.Label.Height + CaptionGap;
        foreach (var seat in LobbyPopupModel.Seats(listing))
        {
            var row = new Rectangle(_left.X, y, _left.Width, RosterRow);
            var avatar = new Rectangle(row.X, row.Y, RosterRow, RosterRow);
            y += RosterRow + RosterGap;
            if (seat.Kind == PopupSeatKind.Unnamed)
            {
                Draw.Fill(g, Theme.Card, row);
                Draw.Fill(g, Theme.Selection, avatar);
                continue;
            }
            if (seat.Kind == PopupSeatKind.Open)
            {
                Draw.Dashed(g, avatar, Theme.Raised);
                Draw.Text(g, OpenWord, Theme.Body, Theme.Muted, new Rectangle(avatar.Right + RosterText, row.Y, row.Width, RosterRow));
                continue;
            }
            var entry = seat.Entry!;
            var host = seat.Host;
            Draw.Fill(g, Theme.Card, row);
            Draw.Fill(g, CardStates.Tint(CardStates.OfStep(entry.Step)), row);
            Draw.Fill(g, host ? Theme.Raised : Theme.Selection, avatar);
            if (entry.Name.Trim() is { Length: > 0 } name) Draw.Centered(g, char.ToUpperInvariant(name[0]).ToString(), Theme.Strong, Theme.Muted, avatar);
            var textX = avatar.Right + RosterText;
            Draw.Text(g, entry.Name, host ? Theme.Strong : Theme.Body, Theme.Text, new Rectangle(textX, row.Y, Math.Max(0, row.Right - textX), RosterRow));
            if (host) Draw.TextRight(g, HostWord, Theme.Small, Theme.Muted, new Rectangle(row.X, row.Y, row.Width - RosterText, RosterRow));
        }
    }

    private void PaintGrid(Graphics g)
    {
        var listing = _model.Listing;
        var labelWidth = GridLabels.Max(label => Draw.Width(label, Theme.StepSmall)) + GridLabelGap;
        var valueX = _right.X + labelWidth;
        var gameOk = LobbyPopupModel.GameOk(listing, _mod);
        for (var i = 0; i < GridRows; i++)
        {
            var row = new Rectangle(_right.X, _right.Y + i * GridPitch, _right.Width, GridPitch);
            Draw.Text(g, GridLabels[i], Theme.StepSmall, Theme.Muted, row with { Width = labelWidth });
            var value = row with { X = valueX, Width = Math.Max(0, row.Right - valueX) };
            switch (i)
            {
                case 0:
                    Draw.Text(g, LobbyRowModels.Ping(listing), Theme.MonoStep, Theme.Text, value);
                    break;
                case 1:
                    PaintChecked(g, value, !_model.ModDiffers, listing.ModBuild > 0 ? listing.ModBuild.ToString(CultureInfo.InvariantCulture) : string.Empty, Theme.MonoStep);
                    break;
                case 2:
                    PaintChecked(g, value, gameOk, listing.GameBuild > 0 ? string.Format(CultureInfo.InvariantCulture, BuildFormat, listing.GameBuild) : string.Empty, Theme.StepSmall);
                    break;
                case 3:
                    Draw.Text(g, LobbyPopupModel.Network(_model), Theme.StepSmall, Theme.Text, value);
                    break;
                default:
                    _playing.Paint(g, value, Theme.MonoStep, Theme.Text, BackColor);
                    break;
            }
        }
    }

    /// <summary>A green check or a red cross, then the value.</summary>
    private static void PaintChecked(Graphics g, Rectangle value, bool ok, string text, Font font)
    {
        var glyph = new Rectangle(value.X, value.Y + (value.Height - GlyphSize) / 2, GlyphSize, GlyphSize);
        if (ok) Draw.Check(g, Theme.Connected, glyph, 1);
        else Draw.Cross(g, Theme.Problem, glyph);
        Draw.Text(g, text, font, Theme.Text, value with { X = glyph.Right + GlyphGap, Width = Math.Max(0, value.Width - GlyphSize - GlyphGap) });
    }
}
