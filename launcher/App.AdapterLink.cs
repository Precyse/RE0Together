using CoopLauncher.Gui;

namespace CoopLauncher;

/// <summary>What the loop and the game's own co-op page tell each other over the adapter link: the player's option changes made
/// there (OPTION_SET) are checked and saved, and the lobby (LOBBY_STATE) is sent to the adapter when it changed.</summary>
public sealed partial class App
{
    private string? _lobbyStateSent;
    private bool _lobbyStateDue;
    private AppSettings? _lobbyStateSettings;

    /// <summary>The adapter linked (also again after a reconnect): it gets the lobby text once more.</summary>
    private void OnAdapterLinked()
    {
        _lobbyStateSent = null;
        _lobbyStateDue = true;
    }

    /// <summary>A slot of the session changed: the text carries slots.</summary>
    private void OnSlotsChanged() => _lobbyStateDue = true;

    /// <summary>An option the player changed on the game's own page. An accepted one is saved like a choice made in the window (the
    /// game page is the authority once the game runs, so the selection lock does not apply), and reaches the lobby and every screen
    /// through the usual publishing of the settings.</summary>
    private void OnAdapterOptionSet(byte[] payload)
    {
        if (_profile is not { } profile || _session is not { } session) return;
        if (!ControlMessages.TryParseOptionSet(payload, out var key, out var value))
        {
            Log.Info("Adapter option ignored (malformed OPTION_SET)");
            return;
        }
        if (AdapterOptionRequest.WhyRefused(profile, _gameDir, session.IsHost, key, value) is { } reason)
        {
            Log.Info($"Adapter option {key}={value} refused ({reason})");
            return;
        }
        OptionsModel.Save(profile.Id, key, value);
        Log.Info($"Adapter option {key}={value} saved");
    }

    /// <summary>Sends the adapter the lobby text when the link is up and the text differs from the last one sent on this link. The
    /// text is worked out again only when the status, the settings or a slot changed, or the adapter linked (never every tick).</summary>
    private void PublishLobbyState(AppStatus status)
    {
        if (_bridge is not { IsReady: true } bridge || _session is not { } session || _profile is not { } profile || status.Players.Count == 0) return;
        var settings = AppSettings.Current;
        if (!_lobbyStateDue && status == Status && ReferenceEquals(settings, _lobbyStateSettings)) return;
        _lobbyStateDue = false;
        _lobbyStateSettings = settings;
        var text = LobbyStateText.Build(profile, _gameDir, status, GameOptions.OwnValues(profile, _gameDir), session.SlotOf);
        if (text == _lobbyStateSent) return;
        _lobbyStateSent = text;
        bridge.Send(ControlMessages.LobbyState(text));
    }
}
