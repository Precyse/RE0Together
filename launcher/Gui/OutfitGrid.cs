namespace CoopLauncher.Gui;

/// <summary>One outfit of the grid: the option value it sets, its label and the figure it shows.</summary>
internal sealed record OutfitTile(string Value, string Label, Figure Figure);

/// <summary>The outfits of one character as a grid of tiles (art over a label band), as many columns as fit. One click picks
/// a tile; hovering one is reported so the stage can preview it. The picked tile has the selection fill and a border that
/// slide from tile to tile, a hovered tile fades to the card hover fill, and a different character's set swaps in as a layer.
/// Arrow keys, Home and End move the pick and commit it at once. Locked, the tiles show their art at 35 %, ignore the mouse
/// and the keys, and keep the border on the picked one.</summary>
internal sealed class OutfitGrid : Control
{
    private const string SlideChannel = "slide";
    private const string SetChannel = "set";
    private const string ArrivalChannel = "arrival";
    private const string HoverChannel = "hover";
    private const int NoTile = -1;
    private const float LockedOpacity = 0.35f;

    private static readonly int MinTileWidth = Theme.Scale(84);
    private static readonly int TileGap = Theme.Scale(8);
    private static readonly int TileHeight = Theme.Scale(88);
    private static readonly int LabelBand = Theme.Scale(22);
    private static readonly int LabelPad = Theme.Scale(6);
    private static readonly int SelectedBorder = Theme.Scale(2);
    private static readonly int FocusInset = SelectedBorder + Theme.Scale(1);

    private IReadOnlyList<OutfitTile> _tiles = [];
    private IReadOnlyList<OutfitTile>? _leaving;
    private string _set = string.Empty;
    private int _selected = NoTile;
    private int _leavingSelected = NoTile;
    private int _hovered = NoTile;
    private double[] _hover = [];
    private Rectangle _slideFrom;
    private double _slide = 1;
    private double _swap = 1;
    private double? _arrival;
    private bool _locked;
    private readonly OutfitArtStore _art;

    public OutfitGrid(OutfitArtStore art)
    {
        _art = art;
        BackColor = Theme.Bg;
        TabStop = true;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
    }

    /// <summary>Raised with the value of the tile the player picked.</summary>
    public event Action<string>? Committed;

    /// <summary>Raised with the value of the tile under the mouse, or null when it left the tiles.</summary>
    public event Action<string?>? Hovered;

    /// <summary>The grid's height for this many tiles at this width.</summary>
    public static int HeightFor(int width, int count)
    {
        var rows = (count + Columns(width) - 1) / Columns(width);
        return rows == 0 ? 0 : rows * TileHeight + (rows - 1) * TileGap;
    }

    /// <summary>Shows a set of tiles with the pick (a value, or null for none). The same set only moves the pick; another set
    /// swaps in.</summary>
    public void Show(string set, IReadOnlyList<OutfitTile> tiles, string? selected, bool locked)
    {
        SetLocked(locked);
        var index = selected == null ? NoTile : tiles.ToList().FindIndex(tile => tile.Value == selected);
        if (set != _set && _tiles.Count > 0)
        {
            SwapSets(tiles);
            _tiles = tiles;
            _selected = index;
            _slideFrom = index == NoTile ? Rectangle.Empty : TileRect(index);
            _slide = 1;
        }
        else
        {
            _tiles = tiles;
            if (index != _selected) MoveSelection(index);
        }
        _set = set;
        if (_hover.Length != tiles.Count) _hover = new double[tiles.Count];
        Invalidate();
    }

    /// <summary>Pictures arrived: they fade in over what showed in their place.</summary>
    public void ArtArrived() => Motion.Animate(this, ArrivalChannel, 0, 1, Durations.Reveal, Easing.Out, progress =>
    {
        _arrival = progress;
        Invalidate();
    }, () =>
    {
        _arrival = null;
        Invalidate();
    });

    protected override AccessibleObject CreateAccessibilityInstance() => new ItemListAccessibility(this,
        () => _tiles.Count, index => _tiles[index].Label, index => TileRect(index), index => index == _selected,
        index => Commit(index));

    protected override bool IsInputKey(Keys keyData) =>
        keyData is Keys.Left or Keys.Right or Keys.Up or Keys.Down or Keys.Home or Keys.End || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        var target = KeyTarget(e.KeyCode);
        if (_locked || target == NoTile) return;
        Commit(target);
        e.Handled = true;
        base.OnKeyDown(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        SetHovered(_locked ? NoTile : TileAt(e.Location));
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHovered(NoTile);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        if (!_locked && TileAt(e.Location) is var index and not NoTile) Commit(index);
        base.OnMouseClick(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        if (_leaving is { } leaving)
        {
            var leavingSelected = _leavingSelected;
            var leavingRect = leavingSelected == NoTile ? Rectangle.Empty : TileRect(leavingSelected);
            MotionLayer.Paint(g, ClientRectangle, Recipes.SwapOut(_swap), BackColor,
                (target, area) => PaintSet(target, area.Location, leaving, leavingSelected, leavingRect, live: false));
            MotionLayer.Paint(g, ClientRectangle, Recipes.SwapIn(_swap), BackColor,
                (target, area) => PaintSet(target, area.Location, _tiles, _selected, SelectionRect(), live: false));
        }
        else
        {
            PaintSet(g, Point.Empty, _tiles, _selected, SelectionRect(), live: true);
        }
        if (Focused && ShowFocusCues && !_locked && _selected != NoTile)
            Draw.Border(g, Rectangle.Inflate(TileRect(_selected), -FocusInset, -FocusInset), Theme.Text);
    }

    private static int Columns(int width) => Math.Max(1, (width + TileGap) / (MinTileWidth + TileGap));

    private static int TileWidth(int width) => (width - (Columns(width) - 1) * TileGap) / Columns(width);

    private Rectangle TileRect(int index, Point? origin = null)
    {
        var columns = Columns(Width);
        var width = TileWidth(Width);
        var at = origin ?? Point.Empty;
        return new Rectangle(at.X + index % columns * (width + TileGap), at.Y + index / columns * (TileHeight + TileGap), width, TileHeight);
    }

    /// <summary>Where the pick is drawn now: sliding from the tile it left to the one it is on.</summary>
    private Rectangle SelectionRect() =>
        _selected == NoTile ? Rectangle.Empty : Interpolate.Lerp(_slideFrom, TileRect(_selected), _slide);

    private int TileAt(Point point)
    {
        for (var i = 0; i < _tiles.Count; i++)
            if (TileRect(i).Contains(point)) return i;
        return NoTile;
    }

    private void SetLocked(bool locked)
    {
        if (_locked == locked) return;
        _locked = locked;
        TabStop = !locked;
        if (locked) SetHovered(NoTile);
    }

    private void Commit(int index)
    {
        if (index == _selected) return;
        Committed?.Invoke(_tiles[index].Value);
    }

    /// <summary>The tile a key moves the pick to: one tile or one row away, the first or the last, never past the ends; no
    /// tile for another key or when nothing moves.</summary>
    private int KeyTarget(Keys key)
    {
        if (_tiles.Count == 0) return NoTile;
        var from = Math.Max(_selected, 0);
        int? target = key switch
        {
            Keys.Left => from - 1,
            Keys.Right => from + 1,
            Keys.Up => from - Columns(Width),
            Keys.Down => from + Columns(Width),
            Keys.Home => 0,
            Keys.End => _tiles.Count - 1,
            _ => null,
        };
        return target is { } index ? Math.Clamp(index, 0, _tiles.Count - 1) : NoTile;
    }

    private void MoveSelection(int index)
    {
        _slideFrom = SelectionRect() is { IsEmpty: false } current ? current : index == NoTile ? Rectangle.Empty : TileRect(index);
        _selected = index;
        _slide = 0;
        Motion.Animate(this, SlideChannel, 0, 1, Durations.Tabs, Easing.Out, progress =>
        {
            _slide = progress;
            Invalidate();
        });
    }

    private void SwapSets(IReadOnlyList<OutfitTile> next)
    {
        _leaving = _tiles;
        _leavingSelected = _selected;
        _swap = 0;
        for (var i = 0; i < _hover.Length; i++) Motion.Cancel(this, HoverChannel + i);
        _hover = new double[next.Count];
        _hovered = NoTile;
        Motion.Animate(this, SetChannel, 0, 1, Durations.Swap, Easing.Out, progress =>
        {
            _swap = progress;
            Invalidate();
        }, () =>
        {
            _leaving = null;
            Invalidate();
        });
    }

    private void SetHovered(int index)
    {
        if (index == _hovered) return;
        var previous = _hovered;
        _hovered = index;
        Cursor = index == NoTile ? Cursors.Default : Cursors.Hand;
        if (previous != NoTile) FadeHover(previous, 0);
        if (index != NoTile) FadeHover(index, 1);
        Hovered?.Invoke(index == NoTile ? null : _tiles[index].Value);
    }

    private void FadeHover(int index, double target)
    {
        if (index >= _hover.Length) return;
        var channel = HoverChannel + index;
        Motion.Animate(this, channel, Motion.ValueOf(this, channel) ?? _hover[index], target, Durations.Hover, Easing.Css, amount =>
        {
            _hover[index] = amount;
            Invalidate();
        });
    }

    /// <summary>The tiles of one set at an origin: card fills (fading to the hover fill), the pick's fill, the art over each,
    /// the label bands, and last the pick's border. A set that is not live (leaving, or swapping in) paints without hover
    /// and without the arrival fade, which would nest layers.</summary>
    private void PaintSet(Graphics g, Point origin, IReadOnlyList<OutfitTile> tiles, int selected, Rectangle selection, bool live)
    {
        var opacity = _locked ? LockedOpacity : 1f;
        var shifted = selected == NoTile ? Rectangle.Empty : new Rectangle(selection.Location + (Size)origin, selection.Size);
        for (var i = 0; i < tiles.Count; i++)
        {
            var tile = TileRect(i, origin);
            var hover = live && i < _hover.Length ? _hover[i] : 0;
            Draw.Fill(g, Interpolate.Lerp(Theme.Card, Theme.CardHover, hover), tile);
        }
        if (!shifted.IsEmpty) Draw.Fill(g, Theme.Selection, shifted);
        for (var i = 0; i < tiles.Count; i++)
        {
            var tile = TileRect(i, origin);
            var art = new Rectangle(tile.X, tile.Y, tile.Width, tile.Height - LabelBand);
            var band = new Rectangle(tile.X, art.Bottom, tile.Width, LabelBand);
            _art.Paint(g, art, tiles[i].Figure, FigureBox.Tile, Theme.Card, opacity, live ? _arrival : null);
            Draw.Fill(g, Theme.Bg2, band);
            var ink = _locked ? Theme.Dim : i == selected ? Theme.Text : Theme.Muted;
            Draw.Text(g, tiles[i].Label, Theme.Small, ink, new Rectangle(band.X + LabelPad, band.Y, band.Width - LabelPad, band.Height));
        }
        if (shifted.IsEmpty) return;
        for (var inset = 0; inset < SelectedBorder; inset++) Draw.Border(g, Rectangle.Inflate(shifted, -inset, -inset), Theme.Text);
    }
}
