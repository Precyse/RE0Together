namespace CoopLauncher.Gui;

/// <summary>The window's one update path: the release is read in the background, a newer build is offered (Update to N),
/// downloaded and installed in the background without starting it (Downloading), and then waits for the player's restart
/// (Restart); an update that leaves the launcher as it is (mods only) needs no restart. With "Check for updates at start" on the
/// download starts by itself, but only while no session is open (the package is replaced on disk); with it off it starts on the
/// click. A failed install returns to the offer. <see cref="Settled"/> says when the package on disk will not change by itself.
/// Must be used from the UI thread.</summary>
internal sealed class UpdateFlow
{
    private enum Phase { None, Offered, Downloading, Ready }

    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private Phase _phase = Phase.None;
    private bool _free = true;
    private bool _autoPending;
    private int _percent;

    /// <summary>The offer, the download or the staged build changed: the button needs redrawing.</summary>
    public event Action? Changed;

    /// <summary>The player clicked Restart: close the window so the staged build starts.</summary>
    public event Action? RestartRequested;

    /// <summary>The package on disk will not change until the player asks: the check found nothing newer (or failed), a newer build
    /// waits for the click, or an installed update needs no restart. The mods can be brought up to the package now.</summary>
    public event Action? Settled;

    /// <summary>The newer build on offer, 0 while none is known.</summary>
    public int NewerBuild { get; private set; }

    /// <summary>A download is under way (the package is being replaced).</summary>
    public bool Busy => _phase == Phase.Downloading;

    /// <summary>The top bar button's label, or null while there is nothing to offer.</summary>
    public string? Label => _phase switch
    {
        Phase.Offered => $"Update to {NewerBuild}",
        Phase.Downloading => _percent > 0 ? $"Downloading {_percent}%" : "Downloading",
        Phase.Ready => "Restart",
        _ => null,
    };

    /// <summary>The button can be clicked: an offer or a staged build, and no session open.</summary>
    public bool Enabled => _free && _phase is Phase.Offered or Phase.Ready;

    /// <summary>Whether no session is open. The automatic download waits for it.</summary>
    public void SetFree(bool free)
    {
        if (_free == free) return;
        _free = free;
        StartAutomaticDownload();
        Changed?.Invoke();
    }

    /// <summary>Reads the release in the background and offers a newer build. <paramref name="automatic"/> starts the download
    /// as soon as it is offered.</summary>
    public void Check(bool automatic)
    {
        _autoPending = automatic;
        Task.Run(() => ReleaseFeed.Read()).ContinueWith(task =>
        {
            if (task.Result?.Build is { } build && build > BuildCheck.LocalBuild()) Offer(build);
            else Settled?.Invoke();
        }, _ui);
    }

    /// <summary>The button was clicked: download the offered build, or restart into the staged one.</summary>
    public void Click()
    {
        if (!_free) return;
        if (_phase == Phase.Offered) Download();
        else if (_phase == Phase.Ready) RestartRequested?.Invoke();
    }

    internal void Offer(int build)
    {
        NewerBuild = build;
        _phase = Updater.Staged ? Phase.Ready : Phase.Offered;
        Changed?.Invoke();
        StartAutomaticDownload();
        if (_phase != Phase.Downloading) Settled?.Invoke();
    }

    private void StartAutomaticDownload()
    {
        if (_autoPending && _free && _phase == Phase.Offered) Download();
    }

    private void Download()
    {
        _autoPending = false;
        _phase = Phase.Downloading;
        Changed?.Invoke();
        _percent = 0;
        var progress = new ThrottledProgress(new Progress<ModProgress>(report =>
        {
            _percent = report.Percent;
            Changed?.Invoke();
        }), () => Environment.TickCount64);
        Task.Run(() => Updater.Stage(progress)).ContinueWith(task =>
        {
            _percent = 0;
            _phase = !task.Result ? Phase.Offered : Updater.Staged ? Phase.Ready : Phase.None;
            if (_phase == Phase.Ready) Log.Event($"Build {NewerBuild} ready");
            if (_phase == Phase.None) NewerBuild = 0;
            Changed?.Invoke();
            if (_phase == Phase.None) Settled?.Invoke();
        }, _ui);
    }
}
