using System.Globalization;

namespace CoopLauncher.Gui;

internal static partial class SessionScreens
{
    private const string PartnerLostWords = "PARTNER LOST";
    private const string JoiningSuffix = " JOINING";
    private const string HoldingFormat = "Holding {0} {1}";
    private const long MillisecondsPerSecond = 1000;

    /// <summary>The mini window: the header state (the highest partner ping, a partner joining, a partner lost), the seat cards
    /// in the compact layout of the game, the partner alone while one is held or silent, the newest event, and Play solo for the
    /// host in an alert. <paramref name="events"/> are the player events, oldest first.</summary>
    public static MiniModel Mini(AppStatus s, GameProfile g, string? gameDir, IReadOnlyList<string> events)
    {
        var partners = s.Players.Where(player => !player.IsLocal).ToList();
        var lost = partners.FirstOrDefault(player => player.Held || player.Silent);
        var portrait = g.HasCharacterSelect && SeatCount(s, g, gameDir) <= TwoSeats;
        var seats = Seats(s, g, gameDir, preGame: false, compact: true);
        if (portrait) seats = seats.Select(card => card with { Detail = CharacterOf(card) }).ToList();
        return new MiniModel(
            g.Name.ToUpperInvariant(), MiniHeaderOf(partners, lost), seats, portrait ? CardLayout.MiniPortrait : CardLayout.Mini,
            lost == null ? null : AlertOf(lost, seats, s), events.LastOrDefault() ?? string.Empty,
            PlaySolo: lost != null && s.Players.Any(player => player.IsLocal && player.IsHost));
    }

    private static MiniHeader MiniHeaderOf(IReadOnlyList<PlayerSlot> partners, PlayerSlot? lost)
    {
        if (lost != null) return new MiniHeader(MiniHeaderKind.Lost, PartnerLostWords);
        if (partners.FirstOrDefault(player => CardStates.Of(player) == CardState.Progress) is { } joining)
            return new MiniHeader(MiniHeaderKind.Joining, joining.Name.ToUpperInvariant() + JoiningSuffix);
        var ping = partners.Where(player => player.RttMs != null).Select(player => player.RttMs!.Value).DefaultIfEmpty(-1).Max();
        return new MiniHeader(MiniHeaderKind.Ping, ping >= 0 ? string.Format(CultureInfo.InvariantCulture, PingFormat, ping) : string.Empty);
    }

    /// <summary>The partner that is held or silent: its words, the share of the grace that is left (a silent partner is not held
    /// yet, so it has none) and the line under the bar, which names its character.</summary>
    private static MiniAlert AlertOf(PlayerSlot lost, IReadOnlyList<PlayerCard> seats, AppStatus s)
    {
        var card = seats[Ordered(s).IndexOf(lost)];
        var character = CharacterOf(card);
        var time = MinutesSeconds(lost.HoldSecondsLeft);
        var grace = (lost.IsHost ? MemberGrace.HostGraceMs : MemberGrace.PeerGraceMs) / MillisecondsPerSecond;
        return new MiniAlert(
            card, CardStates.Words(lost), lost.Held ? Math.Clamp((double)lost.HoldSecondsLeft / grace, 0, 1) : null,
            !lost.Held ? string.Empty : character.Length > 0 ? string.Format(HoldingFormat, character, time) : time);
    }
}
