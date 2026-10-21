namespace CoopLauncher.Gui;

/// <summary>The selected game's options as a two-column grid of label and dropdown, filled row by row; a change is saved
/// at once and reaches the adapter ini when the session launches the game. One grid shows the session options and another
/// (in the settings view) the options marked for settings. A guest's grid shows the host's value of every host option. The
/// grid is rebuilt only when the game, its folder or the shown host values change, so an open list is never disturbed by a
/// status refresh. The options of a game's character select (character and outfits) are not in the grid: the select view
/// has them.</summary>
internal sealed class OptionsRows : Panel
{
    private static readonly int MinLabelWidth = Theme.Scale(110);
    private static readonly int LabelPad = Theme.Scale(8);
    private static readonly int ColumnGap = Theme.Scale(24);

    private readonly List<FlatSelect> _selects = new();
    private readonly bool _settingsOptions;
    private readonly int _columns;
    private readonly Color _background;
    private string? _shownFor;
    private int _labelWidth;

    public event Action? Changed;

    /// <summary>The grid of the session options, or (settingsOptions) of the options the settings view shows, in this many
    /// columns of label and dropdown, on this background.</summary>
    public OptionsRows(bool settingsOptions = false, int columns = 2, Color? background = null)
    {
        _settingsOptions = settingsOptions;
        _columns = columns;
        _background = background ?? Theme.Bg;
        BackColor = _background;
        Dock = DockStyle.Top;
        Height = 0;
    }

    /// <summary>Shows the game's options. With host values (a session is open) each host option reads the host's value when
    /// the game offers it.</summary>
    public void Show(GameProfile game, string? gameDir, IReadOnlyDictionary<string, string>? hostValues = null)
    {
        var options = OptionsModel.OptionsOf(game, _settingsOptions);
        var shown = options.Select(option => OptionsModel.ShownValue(game, option, gameDir, hostValues)).ToList();
        var key = $"{game.Id}|{gameDir}|{string.Join(',', shown)}";
        if (_shownFor == key) return;
        _shownFor = key;
        Controls.Clear();
        _selects.Clear();
        _labelWidth = options.Select(option => TextRenderer.MeasureText(option.Label, Theme.Body).Width + LabelPad)
            .Append(MinLabelWidth).Max();
        var cells = new List<Control>();
        for (var i = 0; i < options.Count; i++)
        {
            var option = options[i];
            var select = Build(game, option, gameDir, shown[i]);
            _selects.Add(select);
            cells.Add(Caption(option.Label));
            cells.Add(select);
        }

        var rows = new List<Control>();
        for (var start = 0; start < cells.Count; start += _columns * 2)
        {
            if (rows.Count > 0) rows.Add(Spacer());
            rows.Add(GridRow(cells.Skip(start).Take(_columns * 2).ToList()));
        }
        ControlRow.Stack(this, rows.ToArray());
        Height = rows.Sum(row => row.Height);
    }

    /// <summary>Lists are locked while a session is open.</summary>
    public void SetLocked(bool locked)
    {
        foreach (var select in _selects) select.Enabled = !locked;
    }

    /// <summary>Label, list, then for each further column a gap, label, list; a row with fewer options leaves the last columns empty.</summary>
    private TableLayoutPanel GridRow(List<Control> cells)
    {
        while (cells.Count < _columns * 2) cells.Add(new Panel { BackColor = _background });
        var controls = new List<Control> { cells[0], cells[1] };
        var widths = new List<int> { _labelWidth, -1 };
        for (var column = 1; column < _columns; column++)
        {
            controls.AddRange([new Panel { BackColor = _background }, cells[column * 2], cells[column * 2 + 1]]);
            widths.AddRange([ColumnGap, _labelWidth, -1]);
        }
        var row = ControlRow.Create(controls.ToArray(), widths.ToArray());
        row.BackColor = _background;
        return row;
    }

    private Panel Spacer()
    {
        var spacer = ControlRow.Spacer(Theme.Gap);
        spacer.BackColor = _background;
        return spacer;
    }

    private Label Caption(string text) => new()
    {
        Text = text, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
        BackColor = _background, AutoEllipsis = true,
    };

    private FlatSelect Build(GameProfile game, GameOption option, string? gameDir, string current)
    {
        var select = new FlatSelect();
        var values = OptionsModel.Values(option, gameDir);
        foreach (var value in values) select.Items.Add(value.Label);
        select.SelectedIndex = Math.Max(0, values.FindIndex(v => v.Value == current));
        select.SelectedIndexChanged += (_, _) =>
        {
            OptionsModel.Save(game.Id, option.Key, values[select.SelectedIndex].Value);
            Changed?.Invoke();
        };
        return select;
    }
}
