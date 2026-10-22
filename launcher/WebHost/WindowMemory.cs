namespace CoopLauncher.Gui;

/// <summary>Remembers the window's size, position and maximized state between runs (AppSettings).</summary>
internal static class WindowMemory
{
    private static readonly int TitleStripHeight = Theme.Scale(40);

    /// <summary>Puts the window where it was left, unless that place is no longer on any screen.</summary>
    public static void Restore(Form form)
    {
        if (AppSettings.Current.Window is not { } saved) return;
        var size = new Size(
            Math.Max(Theme.Scale(saved.Width), form.MinimumSize.Width),
            Math.Max(Theme.Scale(saved.Height), form.MinimumSize.Height));
        var bounds = new Rectangle(new Point(saved.X, saved.Y), size);
        var titleStrip = new Rectangle(bounds.X, bounds.Y, bounds.Width, TitleStripHeight);
        if (!Screen.AllScreens.Any(screen => screen.WorkingArea.IntersectsWith(titleStrip))) return;
        form.StartPosition = FormStartPosition.Manual;
        form.Bounds = bounds;
        if (saved.Maximized) form.WindowState = FormWindowState.Maximized;
    }

    public static void Save(Form form)
    {
        var maximized = form.WindowState == FormWindowState.Maximized;
        var bounds = form.WindowState == FormWindowState.Normal ? form.Bounds : form.RestoreBounds;
        AppSettings.Update(settings => settings with
        {
            Window = new WindowBounds(bounds.X, bounds.Y, Theme.Unscale(bounds.Width), Theme.Unscale(bounds.Height), maximized),
        });
    }
}
