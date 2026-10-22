namespace CoopLauncher.Gui;

/// <summary>The lobby the player is joining: the id, the host's name and its Steam id (the avatar).</summary>
internal sealed record JoinTarget(ulong LobbyId, string HostName, ulong HostId);

/// <summary>What the joining screen shows: the title and the seat line under it, the host (avatar), the steps, the fix a
/// refusal offers (its upper-case label, and whether it can be used now) and whether Start again is offered.</summary>
internal sealed record JoiningModel(
    string Title, string Sub, ulong HostId, string HostName, IReadOnlyList<StepRow> Steps, NoticeFix Fix, string FixLabel,
    bool FixEnabled, bool StartAgain);

/// <summary>The state line of a lobby: in game with the session timer, reconnecting or waiting for the host with the seconds
/// left, or the game closed.</summary>
internal enum LobbyStateKind { InGame, Reconnecting, WaitingForHost, GameClosed }

internal sealed record LobbyState(LobbyStateKind Kind, string Words, string Timer);

/// <summary>What the hosting and lobby screen shows. Hosting is the screen before the game starts (the step list and START
/// GAME), else the lobby (the state line, the host's options and the last events). Counter is the PLAYERS caption of the
/// hosting screen. Seats are the cards, Layout the layout they use.</summary>
internal sealed record SessionModel(
    bool Hosting, string Code, IReadOnlyList<StepRow> Steps, bool StartEnabled, LobbyState? State,
    IReadOnlyList<(string Label, string Value)> Options, IReadOnlyList<string> Events, bool Relaunch, string Counter,
    IReadOnlyList<PlayerCard> Seats, CardLayout Layout, Notice? Notice);

/// <summary>The mini window's header state: the highest partner ping, a partner joining, or a partner lost.</summary>
internal enum MiniHeaderKind { Ping, Joining, Lost }

internal sealed record MiniHeader(MiniHeaderKind Kind, string Text);

/// <summary>The partner the mini window shows alone while it is held or silent: its card (name, avatar, figure), the words,
/// the share of the hold that is left (null while it is only silent) and the line under the bar.</summary>
internal sealed record MiniAlert(PlayerCard Card, string Words, double? Remaining, string Holding);

/// <summary>What the mini window shows: the game, the header state, the seat cards (replaced by the alert while there is one),
/// the newest event and whether Play solo is offered.</summary>
internal sealed record MiniModel(
    string GameName, MiniHeader Header, IReadOnlyList<PlayerCard> Seats, CardLayout Layout, MiniAlert? Alert, string Event, bool PlaySolo);
