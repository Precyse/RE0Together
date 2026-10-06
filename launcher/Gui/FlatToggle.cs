namespace CoopLauncher.Gui;

/// <summary>A flat, squared on/off switch with its label: an outlined box that fills when on.</summary>
internal sealed class FlatToggle : Control
{
    private static readonly int BoxSize = Theme.Scale(16);
    private static readonly int FillInset = Theme.Scale(4);
    private static readonly int TextGap = Theme.Scale(10);

    private bool _checked;

    public FlatToggle(string text)
    {
        Text = text;
        Height = Theme.ControlHeight;
        Cursor = Cursors.Hand;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>Raised when the player flips the switch (not when <see cref="Checked"/> is set from code).</summary>
    public event Action<bool>? Flipped;

    public bool Checked
    {
        get => _checked;
        set
        {
            if (_checked == value) return;
            _checked = value;
            Invalidate();
        }
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        _checked = !_checked;
        Invalidate();
        Flipped?.Invoke(_checked);
        base.OnMouseClick(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Bg);
        var box = new Rectangle(0, (Height - BoxSize) / 2, BoxSize, BoxSize);
        Draw.Border(g, box, Theme.Muted);
        if (_checked)
        {
            using var fill = new SolidBrush(Theme.Text);
            g.FillRectangle(fill, Rectangle.Inflate(box, -FillInset, -FillInset));
        }

        var textX = box.Right + TextGap;
        Draw.Text(g, Text, Theme.Body, Theme.Text, new Rectangle(textX, 0, Width - textX, Height));
    }
}
