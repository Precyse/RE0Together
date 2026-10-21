namespace CoopLauncher.Gui;

/// <summary>The character and outfit select of a game that has one (RE0), filling the window: the stage with the game's art
/// behind it. Before a session you are the left half and your partner the right, and the host swaps the characters; in a
/// session the host is the left half, a guest the right one, the other player's half is read-only, the swap gives way to the
/// lock, and your outfit is locked once your game starts. A pick is saved at once under the option keys the adapter reads
/// (<c>character</c>, <c>outfit_&lt;character&gt;</c>), so the adapter is untouched.</summary>
internal sealed class CharacterSelectView : Control
{
    private readonly OutfitArtStore _art;
    private readonly CharacterStage _stage;
    private GameProfile? _game;
    private string? _gameDir;
    private SelectModel? _model;

    public CharacterSelectView(OutfitArtStore art)
    {
        _art = art;
        _stage = new CharacterStage(art) { Dock = DockStyle.Fill };
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
        Controls.Add(_stage);
        _stage.Stepped += Step;
        _stage.SwapRequested += () => Save(GameOptions.CharacterKey, _model!.SwapCharacter);
        _art.Changed += _stage.ArtArrived;
    }

    /// <summary>Raised after a pick was saved.</summary>
    public event Action? Changed;

    /// <summary>The game's art shown faintly behind the stage, or null.</summary>
    public Image? Backdrop
    {
        get => _stage.Backdrop;
        set => _stage.Backdrop = value;
    }

    /// <summary>Shows the game's characters and outfits for this status. Its pictures are read the first time the view is
    /// open for a game and folder.</summary>
    public void Show(GameProfile game, string? gameDir, AppStatus status, bool free)
    {
        _game = game;
        _gameDir = gameDir;
        if (Visible) _art.Load(game, gameDir);
        _model = SelectModels.Build(game, gameDir, status, free);
        _stage.Show(_model);
    }

    /// <summary>Puts the focus on the half that plays the character with this label, when its outfit can be changed.</summary>
    public void FocusCharacter(string label)
    {
        if (_game == null || _model == null || GameOptions.CharacterValue(_game, label) is not { } character) return;
        for (var half = 0; half < SelectModel.Halves; half++)
            if (_model[half].Figure.Character == character && _model[half].CanCycle) _stage.FocusedHalf = half;
    }

    protected override void OnVisibleChanged(EventArgs e)
    {
        base.OnVisibleChanged(e);
        if (!Visible || _game == null) return;
        _art.Load(_game, _gameDir);
        _stage.Focus();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _art.Changed -= _stage.ArtArrived;
        base.Dispose(disposing);
    }

    private void Step(int half, int direction)
    {
        var shown = _model![half];
        var next = SelectModels.Neighbor(shown, direction);
        Save(shown.Figure.OptionKey, next.Value);
    }

    private void Save(string key, string value)
    {
        if (_game == null) return;
        AppSettings.Update(settings => settings.WithOption(_game.Id, key, value));
        Changed?.Invoke();
    }
}
