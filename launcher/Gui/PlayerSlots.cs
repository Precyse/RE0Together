namespace CoopLauncher.Gui;

/// <summary>One cell per player the game allows, side by side in one outlined card: lamp, name over character, and the
/// role with the partner's ping on the right. An empty seat reads Open.</summary>
internal sealed class PlayerSlots : Control
{
    private static readonly int CardHeight = Theme.Scale(64);
    private static readonly int PadX = Theme.Scale(14);
    private static readonly int NameX = PadX + Theme.LampSize + Theme.Scale(12);
    private static readonly int LineHeight = Theme.Scale(18);
    private const string OpenSeat = "Open";

    private IReadOnlyList<PlayerSlot> _players = Array.Empty<PlayerSlot>();
    private IReadOnlyList<string> _characters = Array.Empty<string>();  // seat order, host first
    private int _seats = 2;
    private int? _pingMs;

    public PlayerSlots()
    {
        Height = CardHeight;
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public void Show(GameProfile profile, IReadOnlyList<PlayerSlot> players, int? pingMs, IReadOnlyList<string> characters)
    {
        _seats = Math.Max(profile.MaxPlayers, players.Count);
        _characters = characters;
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

        var quiet = player != null ? Theme.Muted : Theme.Dim;
        var role = seat == 0 ? "Host" : "Guest";
        if (player is { IsLocal: false } && _pingMs is { } ping) role = $"{role}  {ping} ms";
        var roleWidth = Draw.Width(role, Theme.Small);
        var roleX = cell.Right - PadX - roleWidth;
        Draw.TextRight(g, role, Theme.Small, quiet, new Rectangle(roleX, cell.Y, roleWidth, cell.Height));

        var name = player?.Name ?? OpenSeat;
        var character = seat < _characters.Count ? _characters[seat] : string.Empty;
        var textWidth = roleX - cell.X - NameX - Theme.Gap;
        var nameColor = player != null ? Theme.Text : Theme.Dim;
        if (character.Length == 0)
        {
            Draw.Text(g, name, Theme.Strong, nameColor, new Rectangle(cell.X + NameX, cell.Y, textWidth, cell.Height));
            return;
        }
        Draw.Text(g, name, Theme.Strong, nameColor, new Rectangle(cell.X + NameX, middle - LineHeight, textWidth, LineHeight));
        Draw.Text(g, character, Theme.Small, quiet, new Rectangle(cell.X + NameX, middle, textWidth, LineHeight));
    }
}
