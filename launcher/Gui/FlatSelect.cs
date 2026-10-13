namespace CoopLauncher.Gui;

/// <summary>A flat dropdown list in the tool's greys: dark well, line border (white while focused), a large arrow,
/// the highlighted entry on the selection fill. The closed box is painted over the system's after each paint.</summary>
internal sealed class FlatSelect : ComboBox
{
    private const int WmPaint = 0x000F;
    private static readonly int ItemInset = Theme.Scale(6);
    private static readonly int ArrowBoxWidth = Theme.Scale(32);

    public FlatSelect()
    {
        DropDownStyle = ComboBoxStyle.DropDownList;
        FlatStyle = FlatStyle.Flat;
        DrawMode = DrawMode.OwnerDrawFixed;
        BackColor = Theme.Bg2;
        ForeColor = Theme.Text;
        Font = Theme.Body;
        ItemHeight = Theme.ControlHeight - Theme.Scale(6);
    }

    protected override void OnDrawItem(DrawItemEventArgs e)
    {
        if (e.Index < 0) return;
        var highlighted = (e.State & DrawItemState.Selected) != 0 && (e.State & DrawItemState.ComboBoxEdit) == 0;
        using var fill = new SolidBrush(highlighted ? Theme.Selection : Theme.Bg2);
        e.Graphics.FillRectangle(fill, e.Bounds);
        var text = new Rectangle(e.Bounds.X + ItemInset, e.Bounds.Y, e.Bounds.Width - ItemInset, e.Bounds.Height);
        Draw.Text(e.Graphics, GetItemText(Items[e.Index]) ?? string.Empty, Theme.Body, Enabled ? Theme.Text : Theme.Dim, text);
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

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == WmPaint) PaintFrame();
    }

    private void PaintFrame()
    {
        using var g = Graphics.FromHwnd(Handle);
        var arrowBox = new Rectangle(Width - ArrowBoxWidth, 0, ArrowBoxWidth, Height);
        using (var fill = new SolidBrush(BackColor)) g.FillRectangle(fill, Rectangle.Inflate(arrowBox, 0, -1));
        Draw.Arrow(g, Enabled ? Theme.Text : Theme.Dim, new Point(arrowBox.X + arrowBox.Width / 2, Height / 2), up: false);
        Draw.Border(g, ClientRectangle, Focused || DroppedDown ? Theme.Text : Theme.Line);
    }
}
