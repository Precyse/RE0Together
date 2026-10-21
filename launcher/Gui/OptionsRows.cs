namespace CoopLauncher.Gui;

/// <summary>The selected game's options as a two-column grid of label and dropdown, filled row by row; a change is saved
/// at once and reaches the adapter ini when the session launches the game. One grid shows the session options and another
/// (in the settings view) the options marked for settings. A guest's grid shows the host's value of every host option. The
/// grid is rebuilt only when the game, its folder or the shown host values change, so an open list is never disturbed by a
/// status refresh. The options of a game's character select (character and outfits) are not in the grid: the select view
/// has them.</summary>
internal sealed class OptionsRows : Panel
{
    private const int Columns = 2;
    private static readonly int MinLabelWidth = Theme.Scale(110);
    private static readonly int LabelPad = Theme.Scale(8);
    private static readonly int ColumnGap = Theme.Scale(24);

    private readonly List<FlatSelect> _selects = new();
    private readonly bool _settingsOptions;
    private string? _shownFor;
    private int _labelWidth;

    public event Action? Changed;

    /// <summary>The grid of the session options, or (settingsOptions) of the options the settings view shows.</summary>
    public OptionsRows(bool settingsOptions = false)
    {
        _settingsOptions = settingsOptions;
        BackColor = Theme.Bg;
        Dock = DockStyle.Top;
        Height = 0;
    }

    /// <summary>Shows the game's options. With host values (a session is open) each host option reads the host's value when
    /// the game offers it.</summary>
    public void Show(GameProfile game, string? gameDir, IReadOnlyDictionary<string, string>? hostValues = null)
    {
        var options = (game.Options ?? []).Where(option => option.Settings == _settingsOptions && !GameOptions.InCharacterSelect(game, option)).ToList();
        var shown = options.Select(option => ShownValue(game, option, gameDir, hostValues)).ToList();
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
    private TableLayoutPanel GridRow(List<Control> cells)
    {
        while (cells.Count < Columns * 2) cells.Add(new Panel { BackColor = Theme.Bg });
        return ControlRow.Create(new[] { cells[0], cells[1], new Panel { BackColor = Theme.Bg }, cells[2], cells[3] },
            _labelWidth, -1, ColumnGap, _labelWidth, -1);
    }

    private static Label Caption(string text) => new()
    {
        Text = text, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
        BackColor = Theme.Bg, AutoEllipsis = true,
    };

    /// <summary>The value the option reads: the host's when a host value is given and offered, else the player's own.</summary>
    private static string ShownValue(GameProfile game, GameOption option, string? gameDir, IReadOnlyDictionary<string, string>? hostValues) =>
        option.Host && hostValues != null && hostValues.TryGetValue(option.Key, out var host) && option.Values.Any(v => v.Value == host)
            ? host
            : GameOptions.Current(game, gameDir)[option.Key];

    private FlatSelect Build(GameProfile game, GameOption option, string? gameDir, string current)
    {
        var select = new FlatSelect();
        var values = option.AvailableIn(gameDir).ToList();
        foreach (var value in values) select.Items.Add(value.Label);
        select.SelectedIndex = Math.Max(0, values.FindIndex(v => v.Value == current));
        select.SelectedIndexChanged += (_, _) =>
        {
            AppSettings.Update(settings => settings.WithOption(game.Id, option.Key, values[select.SelectedIndex].Value));
            Changed?.Invoke();
        };
        return select;
    }
}
