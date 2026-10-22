namespace CoopLauncher;

/// <summary>Trading characters between the seats until the game starts. A guest asks by publishing the character it wants in its
/// options (<see cref="GameOptions.SwapRequestKey"/>); the host's launcher answers by flipping the host's own character option
/// and writing it to the lobby's host options, which turns the seats' characters for everyone.</summary>
public sealed partial class App
{
    private string? _swapWanted;
    private ILobby? _hostCharacterSyncedTo;
    private AppSettings? _hostCharacterSyncedFrom;

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

    /// <summary>Writes the host's character choice to the lobby when it is not what the lobby carries (looked at only when the
    /// settings or the lobby were replaced, never every tick).</summary>
    private void PublishHostCharacter(ILobby lobby)
    {
        var settings = AppSettings.Current;
        if (_profile is not { HasCharacterSelect: true } profile
            || (ReferenceEquals(lobby, _hostCharacterSyncedTo) && ReferenceEquals(settings, _hostCharacterSyncedFrom))) return;
        _hostCharacterSyncedTo = lobby;
        _hostCharacterSyncedFrom = settings;
        var character = GameOptions.Current(profile, _gameDir)[GameOptions.CharacterKey];
        if (lobby.HostOptions.GetValueOrDefault(GameOptions.CharacterKey) == character) return;
        var options = new Dictionary<string, string>(lobby.HostOptions) { [GameOptions.CharacterKey] = character };
        lobby.SetLobbyData(LobbyKey.HostOptions, GameOptions.Encode(options));
    }
}
