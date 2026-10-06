namespace CoopLauncher.Gui;

/// <summary>A one-line text field: dark well, 1px line border that turns white while focused.</summary>
internal sealed class FieldBox : Panel
{
    private const int InsetX = 10;
    private const int InsetY = 9;

    public TextBox Input { get; } = new()
    {
        BorderStyle = BorderStyle.None,
        BackColor = Theme.Bg,
        ForeColor = Theme.Text,
        Font = Theme.Mono,
        Dock = DockStyle.Fill,
    };

    public FieldBox()
    {
        Height = Theme.ControlHeight;
        BackColor = Theme.Bg;
        Padding = new Padding(InsetX, InsetY, InsetX, 0);
        Controls.Add(Input);
        Input.GotFocus += (_, _) => Invalidate();
        Input.LostFocus += (_, _) => Invalidate();
        SetStyle(ControlStyles.ResizeRedraw, true);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Draw.Border(e.Graphics, ClientRectangle, Input.Focused ? Theme.Text : Theme.Line);
    }
}
