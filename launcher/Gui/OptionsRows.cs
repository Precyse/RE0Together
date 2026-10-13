namespace CoopLauncher.Gui;

/// <summary>The selected game's options as a two-column grid of label and dropdown, filled row by row; a change is saved
/// at once and reaches the adapter ini when the session launches the game. The grid is rebuilt only when the game or its
/// folder changes, so an open list is never disturbed by a status refresh.</summary>
internal sealed class OptionsRows : Panel
{
    private const int Columns = 2;
    private static readonly int LabelWidth = Theme.Scale(110);
    private static readonly int ColumnGap = Theme.Scale(24);

    private readonly List<FlatSelect> _selects = new();
    private string? _shownFor;

    public event Action? Changed;

    public OptionsRows()
    {
        BackColor = Theme.Bg;
        Dock = DockStyle.Top;
        Height = 0;
    }

    public bool HasOptions => _selects.Count > 0;

    public void Show(GameProfile game, string? gameDir)
    {
        var key = game.Id + "|" + gameDir;
        if (_shownFor == key) return;
        _shownFor = key;
        Controls.Clear();
        _selects.Clear();
        var cells = new List<Control>();
        foreach (var option in game.Options ?? [])
        {
            var select = Build(game, option, gameDir);
            _selects.Add(select);
            cells.Add(Caption(option.Label));
            cells.Add(select);
        }

        var rows = new List<Control>();
        for (var start = 0; start < cells.Count; start += Columns * 2)
        {
            if (rows.Count > 0) rows.Add(ControlRow.Spacer(Theme.Gap));
            rows.Add(GridRow(cells.Skip(start).Take(Columns * 2).ToList()));
        }
        ControlRow.Stack(this, rows.ToArray());
        Height = rows.Sum(row => row.Height);
    }

    /// <summary>Lists are locked while a session is open.</summary>
    public void SetLocked(bool locked)
    {
        foreach (var select in _selects) select.Enabled = !locked;
    }

    /// <summary>Label, list, gap, label, list; a row with one option leaves the second column empty.</summary>
    private static TableLayoutPanel GridRow(List<Control> cells)
    {
        while (cells.Count < Columns * 2) cells.Add(new Panel { BackColor = Theme.Bg });
        return ControlRow.Create(new[] { cells[0], cells[1], new Panel { BackColor = Theme.Bg }, cells[2], cells[3] },
            LabelWidth, -1, ColumnGap, LabelWidth, -1);
    }

    private static Label Caption(string text) => new()
    {
        Text = text, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
        BackColor = Theme.Bg, AutoEllipsis = true,
    };

    private FlatSelect Build(GameProfile game, GameOption option, string? gameDir)
    {
        var select = new FlatSelect();
        var values = option.AvailableIn(gameDir).ToList();
        foreach (var value in values) select.Items.Add(value.Label);
        var current = GameOptions.Current(game, gameDir)[option.Key];
        select.SelectedIndex = Math.Max(0, values.FindIndex(v => v.Value == current));
        select.SelectedIndexChanged += (_, _) =>
        {
            AppSettings.Update(settings => settings.WithOption(game.Id, option.Key, values[select.SelectedIndex].Value));
            Changed?.Invoke();
        };
        return select;
    }
}
