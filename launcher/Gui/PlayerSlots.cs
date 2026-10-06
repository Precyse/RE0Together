namespace CoopLauncher.Gui;

/// <summary>One cell per player the game allows, side by side in one outlined card: lamp, name, role and character,
/// and the partner's ping. An empty seat reads Open.</summary>
internal sealed class PlayerSlots : Control
{
    private static readonly int CardHeight = Theme.Scale(64);
    private static readonly int PadX = Theme.Scale(14);
    private static readonly int NameX = PadX + Theme.LampSize + Theme.Scale(12);
    private static readonly int MetaLineHeight = Theme.Scale(16);
    private const string OpenSeat = "Open";

    private IReadOnlyList<PlayerSlot> _players = Array.Empty<PlayerSlot>();
    private IReadOnlyList<string> _characters = Array.Empty<string>();
    private int _seats = 2;
    private int? _pingMs;

    public PlayerSlots()
    {
        Height = CardHeight;
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public void Show(GameProfile profile, IReadOnlyList<PlayerSlot> players, int? pingMs)
    {
        _seats = Math.Max(profile.MaxPlayers, players.Count);
        _characters = profile.Characters ?? new List<string>();
        _players = players;
        _pingMs = pingMs;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        var cellWidth = Width / _seats;
        for (var seat = 0; seat < _seats; seat++)
        {
            var cell = new Rectangle(seat * cellWidth, 0, seat == _seats - 1 ? Width - seat * cellWidth : cellWidth, Height);
            PaintSeat(g, seat, cell);
            if (seat > 0) Draw.VerticalLine(g, cell.X, 0, Height);
        }
        Draw.Border(g, ClientRectangle, Theme.Line);
    }

    private void PaintSeat(Graphics g, int seat, Rectangle cell)
    {
        var player = seat < _players.Count ? _players[seat] : null;
        var middle = cell.Y + cell.Height / 2;
        Draw.Lamp(g, player != null ? Theme.Live : Theme.Dim, cell.X + PadX, middle);

        var role = seat == 0 ? "Host" : "Guest";
        var detail = seat < _characters.Count ? _characters[seat] : string.Empty;
        if (player is { IsLocal: false } && _pingMs is { } ping) detail = $"{detail} {ping} ms".Trim();
        var metaWidth = Math.Max(Draw.Width(role, Theme.Small), Draw.Width(detail, Theme.Small));
        var metaX = cell.Right - PadX - metaWidth;
        var metaColor = player != null ? Theme.Muted : Theme.Dim;
        if (detail.Length == 0)
        {
            Draw.TextRight(g, role, Theme.Small, metaColor, new Rectangle(metaX, cell.Y, metaWidth, cell.Height));
        }
        else
        {
            Draw.TextRight(g, role, Theme.Small, metaColor, new Rectangle(metaX, middle - MetaLineHeight, metaWidth, MetaLineHeight));
            Draw.TextRight(g, detail, Theme.Small, metaColor, new Rectangle(metaX, middle, metaWidth, MetaLineHeight));
        }

        var name = player?.Name ?? OpenSeat;
        Draw.Text(g, name, Theme.Strong, player != null ? Theme.Text : Theme.Dim,
            new Rectangle(cell.X + NameX, cell.Y, metaX - cell.X - NameX - Theme.Gap, cell.Height));
    }
}
