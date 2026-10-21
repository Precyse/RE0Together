namespace CoopLauncher.Gui;

/// <summary>The character and outfit select of a game that has one (RE0), in the place of the right pane like the settings
/// view: the stage with you on the left and your partner on the right and the swap button between them, one separator, and
/// under it the outfits of the focused half's character as a grid. Only the host picks the character, and only before
/// hosting; everyone picks their own outfits. A pick is saved at once, under the same option keys the options grid used, so
/// the adapter is untouched; while a session is open everything is locked. Scrolls when it outgrows the pane, with the
/// scrollbar's gutter kept free.</summary>
internal sealed class CharacterSelectView : Panel
{
    private const string SwapLabel = "Swap characters";
    private const int SeparatorHeight = 1;

    private static readonly int ScrollbarGutter = SystemInformation.VerticalScrollBarWidth;
    private static readonly int SwapButtonWidth = Theme.Scale(44);

    private readonly OutfitArtStore _art;
    private readonly CharacterStage _stage;
    private readonly OutfitGrid _grid;
    private readonly FlatButton _swap = new(string.Empty, ButtonKind.Normal)
    {
        Arrow = ArrowDirection.Left, ArrowAfter = ArrowDirection.Right, AccessibleName = SwapLabel, Width = SwapButtonWidth,
    };
    private readonly Panel _separator = new() { BackColor = Theme.Line, Height = SeparatorHeight };
    private GameProfile? _game;
    private string? _gameDir;
    private AppStatus _status = AppStatus.Idle;
    private bool _free = true;
    private Side _side = Side.You;
    private SelectModel? _model;

    public CharacterSelectView(OutfitArtStore art)
    {
        _art = art;
        _stage = new CharacterStage(art);
        _grid = new OutfitGrid(art);
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
        AutoScroll = true;
        NativeTheme.DarkScrollbars(this);
        Controls.Add(_swap);
        Controls.Add(_stage);
        Controls.Add(_separator);
        Controls.Add(_grid);
        _swap.BringToFront();
        _swap.TabIndex = 0;
        _stage.TabIndex = 1;
        _grid.TabIndex = 2;

        _swap.Click += (_, _) => Save(GameOptions.CharacterKey, _model!.PartnerCharacter);
        _grid.Committed += value => Save(_model!.Outfit!.Key, value);
        _grid.Hovered += value => _stage.Preview(value == null ? null : _model!.Tiles.FirstOrDefault(tile => tile.Value == value));
        _stage.SideChosen += Choose;
        _art.Changed += OnArtChanged;
    }

    /// <summary>Raised after a pick was saved.</summary>
    public event Action? Changed;

    /// <summary>Shows the game's characters and outfits for this status. Its pictures are read the first time the view is
    /// open for a game and folder.</summary>
    public void Show(GameProfile game, string? gameDir, AppStatus status, bool free)
    {
        _game = game;
        _gameDir = gameDir;
        _status = status;
        _free = free;
        if (Visible) _art.Load(game, gameDir);
        Rebuild();
    }

    /// <summary>Puts the focus on the half that plays the character with this label.</summary>
    public void FocusCharacter(string label)
    {
        if (_game == null || GameOptions.CharacterValue(_game, label) is not { } character || _model == null) return;
        Choose(_model.You.Figure.Character == character ? Side.You : Side.Partner);
    }

    protected override void OnVisibleChanged(EventArgs e)
    {
        base.OnVisibleChanged(e);
        if (!Visible || _game == null) return;
        _art.Load(_game, _gameDir);
        (_free ? (Control)_grid : _stage).Focus();
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        LayoutContent();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _art.Changed -= OnArtChanged;
        base.Dispose(disposing);
    }

    private void OnArtChanged()
    {
        _stage.ArtArrived();
        _grid.ArtArrived();
    }

    private void Choose(Side side)
    {
        if (_side == side) return;
        _stage.Preview(null);
        _side = side;
        Rebuild();
    }

    /// <summary>Works the model out again and gives it to the stage and the grid, which keep what did not change.</summary>
    private void Rebuild()
    {
        if (_game == null) return;
        var model = SelectModels.Build(_game, _gameDir, _status, _free, _side);
        _model = model;
        _stage.Show(model.You, model.Partner);
        _stage.FocusedSide = _side;
        var character = _side == Side.You ? model.You.Figure.Character : model.PartnerCharacter;
        _grid.Show(character, model.Tiles, model.Selected, !_free);
        _swap.Visible = _free;
        LayoutContent();
    }

    private void Save(string key, string value)
    {
        if (_game == null || !_free) return;
        AppSettings.Update(settings => settings.WithOption(_game.Id, key, value));
        Changed?.Invoke();
    }

    /// <summary>Stage, one separator across the pane, grid: each under the one above with the section padding between. The
    /// content is left aligned, no wider than the content maximum, and leaves the scrollbar's gutter free.</summary>
    private void LayoutContent()
    {
        if (_model == null) return;
        var origin = DisplayRectangle.Location;
        var gutter = VerticalScroll.Visible ? 0 : ScrollbarGutter;
        var width = Math.Min(ClientSize.Width - gutter - 2 * Theme.SectionPadX, Theme.ContentMaxWidth);
        var x = origin.X + Theme.SectionPadX;
        var y = origin.Y + Theme.SectionPadY;
        _stage.SetBounds(x, y, width, CharacterStage.StageHeight);
        var slot = _stage.SwapSlot;
        _swap.Location = new Point(x + slot.X + (slot.Width - _swap.Width) / 2, y + (CharacterStage.StageHeight - _swap.Height) / 2);
        y += CharacterStage.StageHeight + Theme.SectionPadY;
        _separator.SetBounds(origin.X, y, ClientSize.Width, SeparatorHeight);
        y += SeparatorHeight + Theme.SectionPadY;
        var gridHeight = OutfitGrid.HeightFor(width, _model.Tiles.Count);
        _grid.SetBounds(x, y, width, gridHeight);
        AutoScrollMinSize = new Size(0, y - origin.Y + gridHeight + Theme.SectionPadY);
    }
}
