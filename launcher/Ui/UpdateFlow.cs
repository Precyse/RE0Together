namespace CoopLauncher.Gui;

/// <summary>What the Settings "Check for updates" button shows: nothing special, a check the player asked for is running, or it found
/// nothing newer (shown for a moment).</summary>
internal enum UpdateCheck { Idle, Checking, UpToDate }

/// <summary>The window's one update path: the release is read in the background, a change to any component (the launcher or a
/// game's mod) is offered (Update launcher vN, Update mods), downloaded and installed in the background without starting it (Downloading), and then waits for the player's restart
/// (Restart); an update that leaves the launcher as it is (mods only) needs no restart. With "Check for updates at start" on the
/// download starts by itself, but only while no session is open (the package is replaced on disk); with it off it starts on the
/// click. A failed install returns to the offer. The player can also ask for a check (<see cref="CheckNow"/>); it follows the same path. <see cref="Settled"/> says when the package on disk will not change by itself.
/// Must be used from the UI thread.</summary>
internal sealed class UpdateFlow
{
    private enum Phase { None, Offered, Downloading, Ready }

    private const int UpToDateShownMs = 1500;

    private static readonly IReadOnlyDictionary<string, int> NoMods = new Dictionary<string, int>();

    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private Phase _phase = Phase.None;
    private bool _free = true;
    private bool _autoPending;
    private bool _reading;
    private bool _asked;
    private bool _upToDate;
    private int _percent;

    /// <summary>The offer, the download or the staged build changed: the button needs redrawing.</summary>
    public event Action? Changed;

    /// <summary>The player clicked Restart: close the window so the staged build starts.</summary>
    public event Action? RestartRequested;

    /// <summary>The package on disk will not change until the player asks: the check found nothing newer (or failed), a newer build
    /// waits for the click, or an installed update needs no restart. The mods can be brought up to the package now.</summary>
    public event Action? Settled;

    /// <summary>The launcher version on offer, 0 while the launcher is not among the changes.</summary>
    public int NewerBuild { get; private set; }

    /// <summary>The mod version on offer for each game whose mod component changed.</summary>
    public IReadOnlyDictionary<string, int> NewerMods { get; private set; } = NoMods;

    /// <summary>A download is under way (the package is being replaced).</summary>
    public bool Busy => _phase == Phase.Downloading;

    /// <summary>The state of a check the player asked for, for the Settings button.</summary>
    public UpdateCheck CheckState => _asked ? UpdateCheck.Checking : _upToDate ? UpdateCheck.UpToDate : UpdateCheck.Idle;

    /// <summary>The top bar button's label, or null while there is nothing to offer.</summary>
    public string? Label => _phase switch
    {
        Phase.Offered => NewerBuild > 0 ? $"Update launcher v{NewerBuild}" : "Update mods",
        Phase.Downloading => _percent > 0 ? $"Downloading {_percent}%" : "Downloading",
        Phase.Ready => "Restart",
        _ => null,
    };

    /// <summary>The button can be clicked: an offer or a staged build, and no session open.</summary>
    public bool Enabled => _free && _phase is Phase.Offered or Phase.Ready;

    /// <summary>A staged build waits for the player's restart and nothing stops it (no session is open).</summary>
    public bool RestartOffered => _free && _phase == Phase.Ready;

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
    public void Check(bool automatic) => Read(automatic, asked: false);

    /// <summary>The player asked for a check (Settings): the same as <see cref="Check"/>, with the button showing it runs and, when
    /// nothing newer exists, for a moment that it is up to date. Ignored while a check or a download is under way.</summary>
    public void CheckNow(bool automatic)
    {
        if (!_reading && _phase != Phase.Downloading) Read(automatic, asked: true);
    }

    private void Read(bool automatic, bool asked)
    {
        _autoPending = automatic;
        _reading = true;
        _asked = asked;
        _upToDate = false;
        if (asked) Changed?.Invoke();
        Task.Run(() => ReleaseFeed.Read(force: asked)).ContinueWith(task =>
        {
            _reading = false;
            var wasAsked = _asked;
            _asked = false;
            if (task.Result is { UpdateAvailable: true } release) Offer(release);
            else
            {
                if (wasAsked && task.Result != null) ShowUpToDate();
                Settled?.Invoke();
            }
        }, _ui);
    }

    private void ShowUpToDate()
    {
        _upToDate = true;
        Changed?.Invoke();
        Task.Delay(UpToDateShownMs).ContinueWith(_ =>
        {
            _upToDate = false;
            Changed?.Invoke();
        }, _ui);
    }

    /// <summary>The button was clicked: download the offered build, or restart into the staged one.</summary>
    public void Click()
    {
        if (!_free) return;
        if (_phase == Phase.Offered) Download();
        else Restart();
    }

    /// <summary>Restarts into the staged build, when one waits and nothing stops it.</summary>
    public void Restart()
    {
        if (RestartOffered) RestartRequested?.Invoke();
    }

    internal void Offer(ReleaseFeed.Release release)
    {
        if (_phase == Phase.Downloading) return;
        var changed = release.Changed;
        var launcherChanged = changed.Any(component => component.Id == InstalledRelease.LauncherId);
        if (launcherChanged || _phase != Phase.Ready) NewerBuild = launcherChanged ? release.Build : 0;
        NewerMods = changed.Where(component => component.Id != InstalledRelease.LauncherId).ToDictionary(component => component.Id, component => component.Version);
        _phase = Updater.Staged ? Phase.Ready : Phase.Offered;
        Changed?.Invoke();
        StartAutomaticDownload();
        if (_phase != Phase.Downloading) Settled?.Invoke();
    }

    private void ClearOffer()
    {
        NewerBuild = 0;
        NewerMods = NoMods;
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
            if (_phase == Phase.None) ClearOffer();
            Changed?.Invoke();
            if (_phase == Phase.None) Settled?.Invoke();
        }, _ui);
    }
}
