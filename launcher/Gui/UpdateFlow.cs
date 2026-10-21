namespace CoopLauncher.Gui;

/// <summary>The window's one update path: the release is read in the background, a newer build is offered (Update to N),
/// downloaded and installed in the background without starting it (Downloading), and then waits for the player's restart
/// (Restart). With "Check for updates at start" on the download starts by itself, but only while no session is open (the
/// package is replaced on disk); with it off it starts on the click. A failed install returns to the offer. Must be used from
/// the UI thread.</summary>
internal sealed class UpdateFlow
{
    private enum Phase { None, Offered, Downloading, Ready }

    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private Phase _phase = Phase.None;
    private bool _free = true;
    private bool _autoPending;

    /// <summary>The offer, the download or the staged build changed: the button needs redrawing.</summary>
    public event Action? Changed;

    /// <summary>The player clicked Restart: close the window so the staged build starts.</summary>
    public event Action? RestartRequested;

    /// <summary>The newer build on offer, 0 while none is known.</summary>
    public int NewerBuild { get; private set; }

    /// <summary>The top bar button's label, or null while there is nothing to offer.</summary>
    public string? Label => _phase switch
    {
        Phase.Offered => $"Update to {NewerBuild}",
        Phase.Downloading => "Downloading",
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
        Task.Run(Updater.Stage).ContinueWith(task =>
        {
            _phase = task.Result ? Phase.Ready : Phase.Offered;
            if (_phase == Phase.Ready) Log.Event($"Build {NewerBuild} ready");
            Changed?.Invoke();
        }, _ui);
    }
}
