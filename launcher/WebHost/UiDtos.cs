using CoopLauncher.Gui;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CoopLauncher.Web;

/// <summary>The JSON records the page receives (SPEC 3.2) and how they are written: camelCase names, enums as camelCase
/// strings, nulls kept. A Steam id (a <c>ulong</c>) is never a number here: ids are decimal strings, because they exceed 2^53.</summary>
internal static class UiJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
    };

    public static string Write<T>(T value) => JsonSerializer.Serialize(value, Options);

    /// <summary>A Steam id or lobby id as the page sees it.</summary>
    public static string Id(ulong id) => id.ToString(System.Globalization.CultureInfo.InvariantCulture);
}

/// <summary>An image slot: the route the page loads (null until there is one) and the letter shown without it.</summary>
internal sealed record ImgDto(string? Src, string Initial);

internal sealed record BtnDto(string Label, bool Enabled);

/// <summary>A mod action on show: the step row (text, percent and bar, check or cross) and, once it failed, the Retry button.</summary>
internal sealed record ModRunDto(StepDto Step, bool Failed, BtnDto? Retry);

internal sealed record ArtDto(string? Hero, string? Logo, string? Capsule);

internal sealed record TransitionDto(TransitionKind Kind, int Direction);

/// <summary>What the top bar is: the tabs, a session screen, or an open view (settings, select).</summary>
internal enum TopMode { Browse, Session, View }

internal sealed record TopDto(TopMode Mode, Tab Tab, BtnDto? Update);

internal sealed record RailGameDto(string Id, string Name, string? Capsule, string Sub, string Line, RailTone Tone, bool Locked, bool Selected);

internal sealed record FooterDto(string Event);

internal sealed record NoticeDto(string Key, string Words, string? Action, bool Report);

internal sealed record StepDto(string Text, StepMark Mark, string Trailing, double? Bar, bool Indeterminate, string[] Lines, string[][] Pairs);

internal sealed record LobbyBtnDto(string Label, bool Primary, bool Enabled);

internal sealed record OptionValueDto(string Value, string Label);

internal sealed record OptionRowDto(string Key, string Label, string Value, OptionValueDto[] Values, bool Locked);

internal sealed record RosterEntryDto(string Key, ImgDto Avatar, string Name, bool Host, string State);

internal sealed record CheckedDto(bool Ok, string Text);

internal sealed record OptionsPopupDto(string Kind, string GameName, ArtDto Art, bool ReadOnly, OptionRowDto[] Rows);

internal sealed record UpdatePopupDto(string Kind, string Build);

internal sealed record LobbyPopupDto(
    string Kind, string Key, ArtDto Art, string Caption, RosterEntryDto[] Roster, int OpenSeats, string Ping, CheckedDto Mod, CheckedDto Game,
    string Network, string Playing, LobbyBtnDto Action);

internal sealed record FriendDto(string Key, ImgDto Avatar, string Name, string Game, string Count, bool Primary);

internal sealed record HomeDto(
    string HostLabel, bool Native, bool HostEnabled, bool JoinEnabled, bool CodeEnabled, BtnDto? Mod, bool HasOptions, bool HasCharacters,
    bool OptionsOpen, NoticeDto? Notice, FriendDto[] Friends, ModRunDto? ModRun);

internal sealed record LobbyRowDto(
    string Key, string? Capsule, string GameName, LobbyHostDto Host, string Players, LobbyModDto Mod, string Ping, LobbyBtnDto Action);

internal sealed record LobbyHostDto(string Name, ImgDto Avatar);

internal sealed record LobbyModDto(string Text, bool Differs);

internal sealed record LobbyGameDto(string Id, string Name);

internal sealed record LobbiesDto(LobbyFilter Filter, string? GameId, LobbyGameDto[] Games, bool Searching, LobbyRowDto[] Rows);

internal sealed record JoiningDto(string Title, string Sub, ImgDto Host, StepDto[] Steps, BtnDto? Fix, bool StartAgain);

internal sealed record LobbyStateDto(LobbyStateKind Kind, string Words, string Timer);

/// <summary>A player card. Seat counts from 1; Unit, Agent, Outfit and Ready are the field file of a game with characters (empty without one).</summary>
internal sealed record CardDto(
    string Key, string Occupant, ImgDto Avatar, ImgDto? Figure, string Name, string Corner, string Detail, string Words, string Footer,
    string State, bool Local, StepDto[] Steps, string Right, double? Progress, int Seat, string Unit, string Agent, string Outfit, bool Ready);

internal sealed record SessionDto(
    bool Hosting, bool IsHost, string Code, StepDto[] Steps, bool StartEnabled, LobbyStateDto? State, string[][] Options, string[] Events, bool Relaunch,
    string Counter, CardLayout Layout, CardDto[] Seats, bool SeatsClickable, NoticeDto? Notice);

internal sealed record HalfDto(ImgDto Figure, string Name, string Role, string Label, string Kind, bool Locked, int Dots, int Index, bool CanCycle, double Opacity);

internal sealed record SelectDto(HalfDto Left, HalfDto Right, StageCentre Centre, int FocusHalf);

internal sealed record SettingsGameDto(
    string Id, string Name, string Folder, bool BrowseEnabled, bool ResetEnabled, bool UninstallEnabled, string ModLine, string GameLine, OptionRowDto[] Options, bool Archive,
    ModRunDto? ModRun);

internal sealed record SettingsDto(bool CheckForUpdates, UpdateCheck UpdateCheck, LobbyVisibility Visibility, bool VisibilityLocked, SettingsGameDto[] Games);

/// <summary>The main page's state (everything but <c>type</c> and <c>seq</c>, which the surface adds). Only the DTO of the screen
/// on show is set, plus <see cref="Popup"/> (an <see cref="OptionsPopupDto"/>, a <see cref="LobbyPopupDto"/> or an <see cref="UpdatePopupDto"/>).</summary>
internal sealed record MainState(
    ScreenKind Screen, TransitionDto Transition, TopDto Top, RailGameDto[] Games, ArtDto Art, FooterDto Footer, object? Popup,
    HomeDto? Home, LobbiesDto? Lobbies, JoiningDto? Joining, SessionDto? Session, SelectDto? Select, SettingsDto? Settings);

internal sealed record MiniHeaderDto(MiniHeaderKind Kind, string Text);

internal sealed record MiniAlertDto(CardDto Card, string Words, double? Remaining, string Holding);

internal sealed record MiniDto(string GameName, MiniHeaderDto Header, CardLayout Layout, CardDto[] Seats, MiniAlertDto? Alert, string Event, bool PlaySolo);

/// <summary>The mini page's state.</summary>
internal sealed record MiniState(ArtDto Art, MiniDto Mini);
