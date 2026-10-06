namespace CoopLauncher.Gui;

/// <summary>A row of controls at control height, docked to the top of its container.</summary>
internal static class ControlRow
{
    /// <summary>Widths are in pixels; -1 takes the remaining space.</summary>
    public static TableLayoutPanel Create(Control[] controls, params int[] widths)
    {
        var row = new TableLayoutPanel
        {
            Dock = DockStyle.Top, Height = Theme.ControlHeight, ColumnCount = controls.Length, RowCount = 1,
            BackColor = Theme.Bg, Margin = Padding.Empty, Padding = Padding.Empty,
        };
        row.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        for (var i = 0; i < controls.Length; i++)
        {
            row.ColumnStyles.Add(widths[i] < 0 ? new ColumnStyle(SizeType.Percent, 100) : new ColumnStyle(SizeType.Absolute, widths[i]));
            controls[i].Dock = DockStyle.Fill;
            controls[i].Margin = new Padding(i == 0 ? 0 : Theme.Gap, 0, 0, 0);
            row.Controls.Add(controls[i], i, 0);
        }
        return row;
    }

    /// <summary>Empty space between stacked rows.</summary>
    public static Panel Spacer(int height) => new() { Dock = DockStyle.Top, Height = height, BackColor = Theme.Bg };

    /// <summary>Adds the controls to a container top to bottom (docking stacks the last added control on top).</summary>
    public static void Stack(Control container, params Control[] topToBottom)
    {
        foreach (var control in topToBottom.Reverse())
        {
            control.Dock = DockStyle.Top;
            container.Controls.Add(control);
        }
    }
}
