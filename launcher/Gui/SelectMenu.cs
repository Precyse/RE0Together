namespace CoopLauncher.Gui;

/// <summary>The list a <see cref="FlatSelect"/> opens: a borderless popup owned by the window, shown without taking focus
/// (keys keep going to the select), under the box or above it when the screen has no room. It opens by scaling up from the
/// box's corner and closes by scaling down (menu-dropdown). A click commits the value at once; a click outside it or the
/// window deactivating closes it.</summary>
internal sealed class SelectMenu : Form, IMessageFilter
{
    public const int MaxRows = 8;

    /// <summary>Where item text starts, shared with the closed box so the value does not move when the list opens.</summary>
    public static readonly int TextInset = Theme.Scale(10);

    private const string OpenChannel = "open";
    private const string CloseChannel = "close";
    private const int WsExNoActivate = 0x08000000;
    private const int WsExToolWindow = 0x00000080;
    private const int WmLButtonDown = 0x0201;
    private const int WmRButtonDown = 0x0204;
    private const int WmMButtonDown = 0x0207;
    private const int WmNcLButtonDown = 0x00A1;
    private const int WmNcRButtonDown = 0x00A4;
    private const int BorderWidth = 1;

    private static readonly int RowHeight = Theme.ControlHeight - Theme.Scale(6);
    private static readonly int ScrollThumbWidth = Theme.Scale(3);

    private readonly Control _box;
    private readonly IReadOnlyList<string> _items;
    private readonly int _current;
    private readonly int _visibleRows;
    private Form? _window;
    private int _highlight;
    private int _top;
    private Point _origin;
    private Layer _layer = Recipes.DropdownOpen(0);
    private bool _closing;

    public SelectMenu(Control box, IReadOnlyList<string> items, int current)
    {
        _box = box;
        _items = items;
        _current = current;
        _highlight = Math.Max(0, current);
        _visibleRows = Math.Min(items.Count, MaxRows);
        _top = Math.Clamp(_highlight - _visibleRows + 1, 0, items.Count - _visibleRows);
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.Manual;
        BackColor = Theme.Card;
        Opacity = 0;
        DoubleBuffered = true;
        ClientSize = new Size(box.Width, _visibleRows * RowHeight + 2 * BorderWidth);
    }

    /// <summary>The player chose the item (a click or Enter).</summary>
    public event Action<int>? Committed;

    /// <summary>The menu started closing; the select is no longer open.</summary>
    public event Action? Dismissed;

    protected override bool ShowWithoutActivation => true;

    protected override CreateParams CreateParams
    {
        get
        {
            var parameters = base.CreateParams;
            parameters.ExStyle |= WsExNoActivate | WsExToolWindow;
            return parameters;
        }
    }

    /// <summary>Places the menu against the box, shows it and plays the open recipe.</summary>
    public void Open()
    {
        var anchor = _box.PointToScreen(Point.Empty);
        var work = Screen.FromControl(_box).WorkingArea;
        var below = anchor.Y + _box.Height + Height <= work.Bottom || anchor.Y - Height < work.Top;
        Location = new Point(anchor.X, below ? anchor.Y + _box.Height : anchor.Y - Height);
        _origin = new Point(0, below ? 0 : Height);

        _window = _box.FindForm();
        if (_window != null)
        {
            _window.Deactivate += OnWindowChanged;
            _window.LocationChanged += OnWindowChanged;
            _window.SizeChanged += OnWindowChanged;
        }
        Application.AddMessageFilter(this);
        if (_window != null) Show(_window);
        else Show();
        Motion.Animate(this, OpenChannel, 0, 1, Durations.DropdownOpen, Easing.Out, progress => Apply(Recipes.DropdownOpen(progress)));
    }

    /// <summary>Closes with the close recipe; the select is closed from the first moment, the window goes when it ends.</summary>
    public void CloseMenu()
    {
        if (_closing) return;
        _closing = true;
        Application.RemoveMessageFilter(this);
        if (_window != null)
        {
            _window.Deactivate -= OnWindowChanged;
            _window.LocationChanged -= OnWindowChanged;
            _window.SizeChanged -= OnWindowChanged;
        }
        Dismissed?.Invoke();
        Motion.Cancel(this, OpenChannel);
        Motion.Animate(this, CloseChannel, 0, 1, Durations.DropdownClose, Easing.Out,
            progress => Apply(Recipes.DropdownClose(progress)), Close);
    }

    /// <summary>Up, Down, Enter and Escape while the menu is open; true when the key was used.</summary>
    public bool HandleKey(Keys key)
    {
        switch (key)
        {
            case Keys.Up:
                Highlight(_highlight - 1);
                return true;
            case Keys.Down:
                Highlight(_highlight + 1);
                return true;
            case Keys.Enter:
                Commit(_highlight);
                return true;
            case Keys.Escape:
                CloseMenu();
                return true;
            default:
                return false;
        }
    }

    bool IMessageFilter.PreFilterMessage(ref Message m)
    {
        if (m.Msg is WmLButtonDown or WmRButtonDown or WmMButtonDown or WmNcLButtonDown or WmNcRButtonDown)
        {
            var point = Cursor.Position;
            if (!Bounds.Contains(point) && !_box.RectangleToScreen(_box.ClientRectangle).Contains(point)) CloseMenu();
        }
        return false;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        Highlight(_top + Math.Clamp((e.Y - BorderWidth) / RowHeight, 0, _visibleRows - 1));
        base.OnMouseMove(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left) Commit(_top + Math.Clamp((e.Y - BorderWidth) / RowHeight, 0, _visibleRows - 1));
        base.OnMouseDown(e);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        ScrollTo(_top - Math.Sign(e.Delta));
        base.OnMouseWheel(e);
    }

    protected override void OnPaint(PaintEventArgs e) =>
        MotionLayer.Paint(e.Graphics, ClientRectangle, _layer with { Opacity = 1 }, Theme.Card, PaintContent, _origin);

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        if (!_closing) Application.RemoveMessageFilter(this);
        base.OnFormClosed(e);
    }

    private void PaintContent(Graphics g, Rectangle bounds)
    {
        Draw.Fill(g, Theme.Card, bounds);
        for (var row = 0; row < _visibleRows; row++)
        {
            var index = _top + row;
            var cell = new Rectangle(bounds.X + BorderWidth, bounds.Y + BorderWidth + row * RowHeight, bounds.Width - 2 * BorderWidth, RowHeight);
            if (index == _highlight) Draw.Fill(g, Theme.Selection, cell);
            Draw.Text(g, _items[index], index == _current ? Theme.Strong : Theme.Body, Theme.Text,
                new Rectangle(cell.X + TextInset - BorderWidth, cell.Y, cell.Width - TextInset, cell.Height));
        }
        var track = new Rectangle(bounds.X + BorderWidth, bounds.Y + BorderWidth, bounds.Width - 2 * BorderWidth, _visibleRows * RowHeight);
        Draw.ScrollThumb(g, track, _items.Count * RowHeight, _top * RowHeight, ScrollThumbWidth);
        Draw.Border(g, bounds, Theme.Line);
    }

    private void Apply(Layer layer)
    {
        _layer = layer;
        Opacity = layer.Opacity;
        Invalidate();
    }

    private void Highlight(int index)
    {
        index = Math.Clamp(index, 0, _items.Count - 1);
        if (index < _top) _top = index;
        else if (index >= _top + _visibleRows) _top = index - _visibleRows + 1;
        if (index == _highlight) return;
        _highlight = index;
        Invalidate();
    }

    private void ScrollTo(int top)
    {
        top = Math.Clamp(top, 0, _items.Count - _visibleRows);
        if (top == _top) return;
        _top = top;
        Invalidate();
    }

    private void Commit(int index)
    {
        if (_closing) return;
        Committed?.Invoke(index);
        CloseMenu();
    }

    private void OnWindowChanged(object? sender, EventArgs e) => CloseMenu();
}
