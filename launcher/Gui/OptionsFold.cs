namespace CoopLauncher.Gui;

/// <summary>The game's options behind one button: folded at start, the button opens the grid under it and closes it again.
/// A game without options shows neither.</summary>
internal sealed class OptionsFold : Panel
{
    private const string ButtonText = "Options";

    private static readonly int ButtonWidth = Theme.ToolWidth + Theme.ArrowSize + Theme.Gap;

    private readonly FlatButton _button = new(ButtonText, ButtonKind.Ghost) { Arrow = ArrowDirection.Down };
    private readonly Panel _buttonRow = new() { Dock = DockStyle.Top, Height = Theme.ControlHeight, BackColor = Theme.Bg };
    private readonly Panel _gap = new() { Dock = DockStyle.Top, Height = Theme.Gap * 2, BackColor = Theme.Bg };
    private bool _open;

    public OptionsFold()
    {
        Dock = DockStyle.Top;
        BackColor = Theme.Bg;
        _button.SetBounds(0, 0, ButtonWidth, Theme.ControlHeight);
        _button.Click += (_, _) => SetOpen(!_open);
        _buttonRow.Controls.Add(_button);
        Rows.Resize += (_, _) => Refit();
        Refit();
    }

    /// <summary>Raised when the fold opens, closes or its grid changes size, so the section refits.</summary>
    public event Action? Resized;

    public OptionsRows Rows { get; } = new();

    private void SetOpen(bool open)
    {
        _open = open;
        _button.Arrow = open ? ArrowDirection.Up : ArrowDirection.Down;
        _button.Invalidate();
        Refit();
    }

    /// <summary>Docks the button row on top and, while open, the gap and the grid under it; sizes the fold to what it shows.</summary>
    private void Refit()
    {
        SuspendLayout();
        Controls.Clear();
        var hasOptions = Rows.Height > 0;
        if (hasOptions && _open) Controls.AddRange([Rows, _gap]);
        if (hasOptions) Controls.Add(_buttonRow);
        ResumeLayout();
        Height = !hasOptions ? 0 : _buttonRow.Height + (_open ? _gap.Height + Rows.Height : 0);
        Resized?.Invoke();
    }
}
