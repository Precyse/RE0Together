using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>The two halves of the character select (S1, S3) over the game's art: each a cutout with the character's name, the
/// outfit label between big previous and next arrows, and the dots; between them the swap button (before a session) or the
/// lock. The arrows of the focused half also answer to Left and Right, Tab moves to the other half. What moves is
/// <see cref="StageHalfView"/>'s. Pictures that arrive late fade in.</summary>
internal sealed class CharacterStage : Control
{
    private const string ArrivalChannel = "arrival";
    private const float BackdropOpacity = 0.22f;
    private const float FadeStart = 0.45f;
    private const float FadeMid = 0.78f;
    private const int FadeMidAlpha = 217;
    private const int LockGlyphWidth = 18;
    private const int LockGlyphHeight = 22;
    private const int FocusInset = 6;
    private const int Forward = 1;
    private const int Backward = -1;
    private const string PreviousLabel = "Previous outfit";
    private const string NextLabel = "Next outfit";

    private readonly OutfitArtStore _art;
    private readonly StageHalfView?[] _views = new StageHalfView?[SelectModel.Halves];
    private readonly StageArrow[] _previous = [new(StageGlyph.Previous, PreviousLabel), new(StageGlyph.Previous, PreviousLabel)];
    private readonly StageArrow[] _next = [new(StageGlyph.Next, NextLabel), new(StageGlyph.Next, NextLabel)];
    private readonly int[] _direction = [Forward, Forward];
    private readonly StageArrow _swap = new(StageGlyph.Swap, "Swap characters");
    private Image? _backdrop;
    private SelectModel? _model;
    private int _focus;
    private double? _arrival;

    public CharacterStage(OutfitArtStore art)
    {
        _art = art;
        BackColor = Theme.Bg;
        TabStop = true;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        for (var half = 0; half < SelectModel.Halves; half++)
        {
            var side = half;
            _previous[half].Click += (_, _) => Step(side, Backward);
            _next[half].Click += (_, _) => Step(side, Forward);
            Controls.Add(_previous[half]);
            Controls.Add(_next[half]);
        }
        _swap.Click += (_, _) => SwapRequested?.Invoke();
        Controls.Add(_swap);
    }

    /// <summary>Raised with the half and the direction (1 next, -1 previous) when the player asks for another outfit.</summary>
    public event Action<int, int>? Stepped;

    /// <summary>Raised when the player asks to swap the characters.</summary>
    public event Action? SwapRequested;

    /// <summary>The game's art laid under the stage, or null.</summary>
    public Image? Backdrop
    {
        get => _backdrop;
        set
        {
            _backdrop = value;
            Invalidate();
        }
    }

    /// <summary>The half the keys act on (0 left, 1 right).</summary>
    public int FocusedHalf
    {
        get => _focus;
        set
        {
            if (_focus == value) return;
            _focus = value;
            Invalidate();
        }
    }

    /// <summary>Shows the model. The first call shows it at once; later, a half moves what changed (see
    /// <see cref="StageHalfView.Show"/>), and two characters that traded places cross over.</summary>
    public void Show(SelectModel model)
    {
        var before = _model;
        _model = model;
        if (before == null)
        {
            for (var half = 0; half < SelectModel.Halves; half++) _views[half] = new StageHalfView(this, _art, $"half{half}", model[half]);
            _focus = model.OwnHalf;
        }
        else
        {
            var traded = TradedPlaces(before, model);
            for (var half = 0; half < SelectModel.Halves; half++) _views[half]!.Show(model[half], _direction[half], traded);
        }
        Array.Fill(_direction, Forward);
        if (!model[_focus].CanCycle && model[model.OwnHalf].CanCycle) _focus = model.OwnHalf;
        UpdateControls();
        Invalidate();
    }

    /// <summary>Pictures arrived: they fade in over what showed in their place.</summary>
    public void ArtArrived() => Motion.Animate(this, ArrivalChannel, 0, 1, Durations.Reveal, Easing.Out, progress =>
    {
        _arrival = progress;
        Invalidate();
    }, () =>
    {
        _arrival = null;
        Invalidate();
    });

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            foreach (var view in _views) view?.Dispose();
            Motion.Cancel(this, ArrivalChannel);
        }
        base.Dispose(disposing);
    }

    protected override bool IsInputKey(Keys keyData) =>
        (keyData & Keys.KeyCode) is Keys.Left or Keys.Right || ((keyData & Keys.KeyCode) == Keys.Tab && NextCycling(keyData.HasFlag(Keys.Shift) ? Backward : Forward) >= 0)
        || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        switch (e.KeyCode)
        {
            case Keys.Left or Keys.Right:
                Step(_focus, e.KeyCode == Keys.Left ? Backward : Forward);
                e.Handled = true;
                break;
            case Keys.Tab when NextCycling(e.Shift ? Backward : Forward) is var half and >= 0:
                FocusedHalf = half;
                e.Handled = true;
                break;
        }
        base.OnKeyDown(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        if (_model != null)
            for (var half = 0; half < SelectModel.Halves; half++)
                if (_model[half].CanCycle && Columns()[half].Contains(e.Location)) FocusedHalf = half;
        base.OnMouseClick(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        UpdateControls();
    }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Theme.Bg);
        if (_backdrop == null) return;
        Draw.Backdrop(g, _backdrop, ClientRectangle, BackdropOpacity);
        using var brush = new LinearGradientBrush(ClientRectangle, Color.Black, Color.Black, LinearGradientMode.Vertical)
        {
            InterpolationColors = new ColorBlend
            {
                Colors = [Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg), Color.FromArgb(FadeMidAlpha, Theme.Bg), Theme.Bg],
                Positions = [0f, FadeStart, FadeMid, 1f],
            },
        };
        g.FillRectangle(brush, ClientRectangle);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_model == null) return;
        var g = e.Graphics;
        var layouts = Layouts();
        for (var half = 0; half < SelectModel.Halves; half++)
            _views[half]!.Paint(g, layouts[half], layouts[SelectModel.Halves - 1 - half].Figure, _arrival);
        if (_model.Centre == StageCentre.Lock) PaintLock(g);
        if (Focused && ShowFocusCues && _model[_focus].CanCycle)
            Draw.Border(g, Rectangle.Inflate(layouts[_focus].ArrowRow, Theme.Scale(FocusInset), Theme.Scale(FocusInset)), Theme.Text);
    }

    private Rectangle[] Columns() => StageLayout.Columns(ClientSize);

    private HalfLayout[] Layouts() => Columns().Select(StageLayout.Of).ToArray();

    private static bool TradedPlaces(SelectModel before, SelectModel after) =>
        before.Left.Figure.Character != before.Right.Figure.Character
        && before.Left.Figure.Character == after.Right.Figure.Character && before.Right.Figure.Character == after.Left.Figure.Character;

    /// <summary>The half in the direction from the focused one that has usable arrows, or -1.</summary>
    private int NextCycling(int direction)
    {
        if (_model == null) return -1;
        var half = _focus + direction;
        return half is >= 0 and < SelectModel.Halves && _model[half].CanCycle ? half : -1;
    }

    private void Step(int half, int direction)
    {
        if (_model == null || !_model[half].CanCycle) return;
        FocusedHalf = half;
        _direction[half] = direction;
        Stepped?.Invoke(half, direction);
    }

    /// <summary>Puts the arrows and the swap button where the layout says, and shows what the model has.</summary>
    private void UpdateControls()
    {
        if (_model == null) return;
        var layouts = Layouts();
        for (var half = 0; half < SelectModel.Halves; half++)
        {
            var shown = _model[half];
            _previous[half].Bounds = layouts[half].Previous;
            _next[half].Bounds = layouts[half].Next;
            foreach (var arrow in new[] { _previous[half], _next[half] })
            {
                arrow.Visible = shown.HasArrows;
                arrow.Enabled = shown.CanCycle;
                arrow.Outlined = shown.Kind == HalfKind.Partner;
            }
        }
        _swap.Bounds = StageLayout.CentreBox(ClientSize);
        _swap.Visible = _model.Centre == StageCentre.Swap;
    }

    private void PaintLock(Graphics g)
    {
        var box = StageLayout.CentreBox(ClientSize);
        Draw.Border(g, box, Theme.Line);
        var glyph = new Size(Theme.Scale(LockGlyphWidth), Theme.Scale(LockGlyphHeight));
        Draw.Lock(g, Theme.Muted, new Rectangle(box.X + (box.Width - glyph.Width) / 2, box.Y + (box.Height - glyph.Height) / 2, glyph.Width, glyph.Height));
    }
}
