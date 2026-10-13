namespace CoopLauncher.Gui;

/// <summary>One dropdown row per option of the selected game (label left, list right); a change is saved at once and
/// reaches the adapter ini when the session launches the game. The rows are rebuilt only when the game or its folder
/// changes, so an open list is never disturbed by a status refresh.</summary>
internal sealed class OptionsRows : Panel
{
    private static readonly int LabelWidth = Theme.Scale(130);
    private static readonly int RowGap = Theme.Scale(8);

    private readonly List<FlatSelect> _selects = new();
    private string? _shownFor;

    public event Action? Changed;

    public OptionsRows()
    {
        BackColor = Theme.Bg;
        Dock = DockStyle.Top;
        Height = 0;
    }

    public void Show(GameProfile game, string? gameDir)
    {
        var key = game.Id + "|" + gameDir;
        if (_shownFor == key) return;
        _shownFor = key;
        Controls.Clear();
        _selects.Clear();
        var rows = new List<Control> { ControlRow.Spacer(RowGap * 2) };
        foreach (var option in game.Options ?? [])
        {
            var select = Build(game, option, gameDir);
            _selects.Add(select);
            rows.Add(ControlRow.Create(new Control[] { Caption(option.Label), select }, LabelWidth, -1));
            rows.Add(ControlRow.Spacer(RowGap));
        }
        ControlRow.Stack(this, rows.ToArray());
        Height = rows.Sum(row => row.Height);
    }

    /// <summary>Lists are locked while a session is open.</summary>
    public void SetLocked(bool locked)
    {
        foreach (var select in _selects) select.Enabled = !locked;
    }

    private static Label Caption(string text) => new()
    {
        Text = text, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
        BackColor = Theme.Bg,
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
