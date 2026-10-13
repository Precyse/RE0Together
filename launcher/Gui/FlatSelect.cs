namespace CoopLauncher.Gui;

/// <summary>A flat dropdown list in the tool's greys: dark well, the highlighted entry on the selection fill.</summary>
internal sealed class FlatSelect : ComboBox
{
    private static readonly int ItemInset = Theme.Scale(6);

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
        Draw.Text(e.Graphics, GetItemText(Items[e.Index]) ?? string.Empty, Theme.Body, Theme.Text, text);
    }
}
