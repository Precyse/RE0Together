namespace CoopLauncher;

/// <summary>Trading characters between the seats until the game starts. A guest asks by publishing the character it wants in its
/// options (<see cref="GameOptions.SwapRequestKey"/>); the host's launcher answers by flipping the host's own character option
/// and writing it to the lobby's host options, which turns the seats' characters for everyone. The same write carries the host's other
/// host options that change while the lobby is open (the save slot to load, the world pause).</summary>
public sealed partial class App
{
    private string? _swapWanted;
    private ILobby? _hostChoicesSyncedTo;
    private AppSettings? _hostChoicesSyncedFrom;

    /// <summary>A guest's swap: asks the host to give up the character the guest wants (<paramref name="character"/>).</summary>
    public void RequestSeatSwap(string character) => _commands.Enqueue(() => _swapWanted = character);

    /// <summary>The guest's request ends when the seats show the character it wanted, when the game starts or when there is no seat
    /// any more; the host flips its character for a guest that wants the one it plays.</summary>
    private void ServeSwaps(AppStatus status)
    {
        if (_profile is not { HasCharacterSelect: true } profile)
        {
            _swapWanted = null;
            return;
        }
        if (_swapWanted != null && (SessionProgress.SelectionLocked(status.Step)
            || GameOptions.CharacterAtSeat(profile, status.SeatCharacters, status.LocalSeat) is not { } held || held == _swapWanted))
            _swapWanted = null;
        if (_transport == null || _hostId != _transport.LocalId || SessionProgress.SelectionLocked(status.Step)) return;
        var hostCharacter = GameOptions.Current(profile, _gameDir)[GameOptions.CharacterKey];
        if (status.Players.Any(player => !player.IsLocal && GameOptions.Decode(player.Options).GetValueOrDefault(GameOptions.SwapRequestKey) == hostCharacter))
            AppSettings.Update(settings => settings.WithOption(profile.Id, GameOptions.CharacterKey, GameOptions.OtherCharacter(profile, hostCharacter)!));
    }

    /// <summary>Writes the host's options that change while the lobby is open (the character, the save slot and its hash, and what the
    /// game's own co-op page changes in play, such as the world pause) to the lobby when they are not what it carries (looked at only
    /// when the settings or the lobby were replaced, never every tick).</summary>
    private void PublishHostChoices(ILobby lobby)
    {
        var settings = AppSettings.Current;
        if (_profile == null || (ReferenceEquals(lobby, _hostChoicesSyncedTo) && ReferenceEquals(settings, _hostChoicesSyncedFrom))) return;
        _hostChoicesSyncedTo = lobby;
        _hostChoicesSyncedFrom = settings;
        var current = GameOptions.HostValues(_profile, _gameDir);
        var changed = current.Keys.Where(key => lobby.HostOptions.GetValueOrDefault(key) != current[key]).ToList();
        if (changed.Count == 0) return;
        var options = new Dictionary<string, string>(lobby.HostOptions);
        foreach (var key in changed) options[key] = current[key];
        lobby.SetLobbyData(LobbyKey.HostOptions, GameOptions.Encode(options));
    }
}
