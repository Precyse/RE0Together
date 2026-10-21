namespace CoopLauncher.Gui;

/// <summary>The mini mode: a small always-on-top window (360x220, no caption, a 1 px border that turns red while a partner is
/// lost) with its own taskbar button, at the bottom right of the main window's screen. Its inside is a <see cref="MiniSurface"/>.
/// It opens and closes with the modal recipe (the form fades while the surface scales from 0.96 and fades); it is dragged by its
/// header; Alt+F4 and closing it from the taskbar act as Expand. Shown with a <see cref="MiniModel"/>, again on every change.</summary>
internal sealed class MiniWindow : Form
{
    private const string OpacityChannel = "opacity";
    private const int Border = 1;
    private const int ScreenMargin = 16;

    private static readonly Size WindowSize = new(Theme.Scale(360), Theme.Scale(220));

    private readonly MiniSurface _surface;
    private Point? _dragFrom;
    private bool _open;

    public MiniWindow(AvatarStore avatars, OutfitArtStore art)
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        TopMost = true;
        ShowInTaskbar = true;
        Text = "Co-op";
        ClientSize = WindowSize;
        BackColor = Theme.Line;
        _surface = new MiniSurface(avatars, art)
        {
            Bounds = new Rectangle(Border, Border, WindowSize.Width - 2 * Border, WindowSize.Height - 2 * Border),
        };
        Controls.Add(_surface);
        _surface.ExpandClicked += () => ExpandClicked?.Invoke();
        _surface.LeaveClicked += () => LeaveClicked?.Invoke();
        _surface.PlaySoloClicked += () => PlaySoloClicked?.Invoke();
        _surface.MouseDown += OnSurfaceMouseDown;
        _surface.MouseMove += OnSurfaceMouseMove;
        _surface.MouseUp += (_, _) => _dragFrom = null;
    }

    public event Action? ExpandClicked;
    public event Action? LeaveClicked;
    public event Action? PlaySoloClicked;

    /// <summary>The window already has its place (it is put at the screen's corner on its first show unless this is set).</summary>
    public bool Placed { get; set; }

    /// <summary>The corner of a work area where the window sits: bottom right, 16 px from both edges.</summary>
    public static Point Corner(Rectangle workArea, Size size) =>
        new(workArea.Right - size.Width - Theme.Scale(ScreenMargin), workArea.Bottom - size.Height - Theme.Scale(ScreenMargin));

    /// <summary>Shows the window (the first call opens it with the modal recipe) or updates what it shows.</summary>
    public void Show(MiniModel model, SteamArt art, Form main)
    {
        BackColor = model.Alert != null ? Theme.Problem : Theme.Line;
        _surface.Show(model, art);
        if (_open) return;
        _open = true;
        if (!Placed)
        {
            Location = Corner(Screen.FromControl(main).WorkingArea, Size);
            Placed = true;
        }
        Opacity = 0;
        base.Show();
        Fade(0, 1, Durations.ModalOpen);
        MotionOverlay.Play(_surface, Durations.ModalOpen, Easing.Out, Recipes.ModalOpen, Theme.Bg);
    }

    /// <summary>Closes the window with the modal recipe, then calls <paramref name="done"/>.</summary>
    public void Close(Action done)
    {
        if (!_open)
        {
            done();
            return;
        }
        _open = false;
        Fade(1, 0, Durations.ModalClose);
        MotionOverlay.Play(_surface, Durations.ModalClose, Easing.Out, Recipes.ModalClose, Theme.Bg, done: () =>
        {
            Hide();
            done();
        });
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            ExpandClicked?.Invoke();
        }
        base.OnFormClosing(e);
    }

    private void Fade(double from, double to, int durationMs) =>
        Motion.Animate(this, OpacityChannel, from, to, durationMs, Easing.Out, value => Opacity = value);

    private void OnSurfaceMouseDown(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left && e.Y < _surface.Header) _dragFrom = Cursor.Position - (Size)Location;
    }

    private void OnSurfaceMouseMove(object? sender, MouseEventArgs e)
    {
        if (_dragFrom is { } from && e.Button == MouseButtons.Left) Location = Cursor.Position - (Size)from;
    }
}
