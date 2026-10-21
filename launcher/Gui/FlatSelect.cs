namespace CoopLauncher.Gui;

/// <summary>A flat dropdown in the tool's greys: a dark well with the value, a line border (white while focused or open)
/// and a large arrow. The list is a <see cref="SelectMenu"/>. Up and Down change the value without opening it; Space,
/// Enter, F4 and Alt+Down open the list. The mouse wheel never changes the value.</summary>
internal sealed class FlatSelect : Control
{
    private static readonly int ArrowBoxWidth = Theme.Scale(32);

    private readonly List<string> _items = new();
    private int _selected = -1;
    private SelectMenu? _menu;

    public FlatSelect()
    {
        Height = Theme.ControlHeight;
        BackColor = Theme.Bg2;
        ForeColor = Theme.Text;
        Font = Theme.Body;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleRole = AccessibleRole.ComboBox;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
    }

    public event EventHandler? SelectedIndexChanged;

    public IList<string> Items => _items;

    public int SelectedIndex
    {
        get => _selected;
        set
        {
            ArgumentOutOfRangeException.ThrowIfLessThan(value, -1);
            ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(value, _items.Count);
            if (value == _selected) return;
            _selected = value;
            AccessibleName = value >= 0 ? _items[value] : null;
            Invalidate();
            SelectedIndexChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>The list is showing.</summary>
    public bool DroppedDown => _menu != null;

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
        {
            if (_menu != null) _menu.CloseMenu();
            else Open();
        }
        base.OnMouseDown(e);
    }

    protected override bool IsInputKey(Keys keyData) => (keyData & Keys.KeyCode) switch
    {
        Keys.Up or Keys.Down or Keys.Enter => true,
        Keys.Escape => _menu != null,
        _ => base.IsInputKey(keyData),
    };

    protected override void OnKeyDown(KeyEventArgs e)
    {
        e.Handled = _menu != null ? _menu.HandleKey(e.KeyCode) : HandleClosedKey(e);
        base.OnKeyDown(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        _menu?.CloseMenu();
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        if (!Enabled) _menu?.CloseMenu();
        Cursor = Enabled ? Cursors.Hand : Cursors.Default;
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _menu?.CloseMenu();
        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var ink = Enabled ? Theme.Text : Theme.Dim;
        g.Clear(BackColor);
        if (_selected >= 0)
            Draw.Text(g, _items[_selected], Theme.Body, ink, new Rectangle(SelectMenu.TextInset, 0, Width - ArrowBoxWidth - SelectMenu.TextInset, Height));
        Draw.Arrow(g, ink, new Point(Width - ArrowBoxWidth / 2, Height / 2), ArrowDirection.Down);
        Draw.Border(g, ClientRectangle, Focused || _menu != null ? Theme.Text : Theme.Line);
    }

    private bool HandleClosedKey(KeyEventArgs e)
    {
        if (e.Alt && e.KeyCode == Keys.Down || e.KeyCode is Keys.Space or Keys.Enter or Keys.F4)
        {
            Open();
            return true;
        }
        if (e.KeyCode is not (Keys.Up or Keys.Down)) return false;
        var next = Math.Clamp(_selected + (e.KeyCode == Keys.Down ? 1 : -1), 0, _items.Count - 1);
        if (_items.Count > 0) SelectedIndex = next;
        return true;
    }

    private void Open()
    {
        if (!Enabled || _items.Count == 0 || _menu != null) return;
        var menu = new SelectMenu(this, _items, _selected);
        menu.Committed += index => SelectedIndex = index;
        menu.Dismissed += () =>
        {
            _menu = null;
            Invalidate();
        };
        _menu = menu;
        menu.Open();
        Invalidate();
    }
}
