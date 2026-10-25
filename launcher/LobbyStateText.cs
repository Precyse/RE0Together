using System.Text;

namespace CoopLauncher;

/// <summary>The LOBBY_STATE payload: the lobby as text for the adapter's own co-op page. Lines are separated by LF and fields by
/// TAB (tabs and line breaks inside a field become spaces): <c>member</c> (slot, host, local, character value and label, outfit label,
/// name), <c>option</c> (a host option's key, value and label), <c>value</c> (each value such an option offers, key, value and label, so
/// the host's page can step through them), <c>outfit</c> (a character's outfit value and label offered here) and
/// <c>own</c> (this player's own option values). The text is capped at <see cref="MaxBytes"/>; whole lines past the cap are dropped.</summary>
public static class LobbyStateText
{
    public const int MaxBytes = 8192;
    public const int MaxNameChars = 64;

    /// <summary>The slot of a member the session has no slot for yet.</summary>
    public const byte NoSlot = 255;

    private const char FieldSeparator = '\t';
    private const char LineSeparator = '\n';
    private const string Yes = "1";
    private const string No = "0";

    /// <param name="own">This player's own option values (<see cref="GameOptions.OwnValues"/>).</param>
    /// <param name="slotOf">A member's session slot, null when it has none yet.</param>
    public static string Build(GameProfile profile, string? gameDir, AppStatus status, IReadOnlyDictionary<string, string> own, Func<ulong, byte?> slotOf)
    {
        var lines = MemberLines(profile, status, slotOf)
            .Concat(OptionLines(profile, status))
            .Concat(ValueLines(profile, gameDir))
            .Concat(OutfitLines(profile, gameDir))
            .Concat(own.Select(pair => Line("own", pair.Key, pair.Value)));
        return string.Join(LineSeparator, WithinCap(lines));
    }

    private static IEnumerable<string> MemberLines(GameProfile profile, AppStatus status, Func<ulong, byte?> slotOf) =>
        status.Players.Select((player, seat) =>
        {
            var character = GameOptions.CharacterAtSeat(profile, status.SeatCharacters, seat);
            return Line(
                "member", (slotOf(player.Id) ?? NoSlot).ToString(), Flag(player.IsHost), Flag(player.IsLocal), character ?? "",
                character == null ? "" : status.SeatCharacters[seat], OutfitLabel(profile, character, player.Options), CutName(player.Name));
        });

    /// <summary>The label of the outfit a member published for its character; empty when it published none or there is no character.</summary>
    private static string OutfitLabel(GameProfile profile, string? character, string publishedOptions)
    {
        if (character == null || GameOptions.OutfitOf(profile, character) is not { } outfit
            || !GameOptions.Decode(publishedOptions).TryGetValue(outfit.Key, out var value)) return "";
        return LabelOf(outfit, value);
    }

    /// <summary>The host options the page shows: not worked out at run time, not the seat count.</summary>
    private static IEnumerable<GameOption> PageHostOptions(GameProfile profile) =>
        (profile.Options ?? []).Where(option => option.Host && !GameOptions.IsRuntime(option) && option.Key != GameOptions.SeatsKey);

    private static IEnumerable<string> ValueLines(GameProfile profile, string? gameDir) =>
        PageHostOptions(profile).SelectMany(option =>
            GameOptions.Offered(profile, option, gameDir).Select(value => Line("value", option.Key, value.Value, value.Label)));

    private static IEnumerable<string> OptionLines(GameProfile profile, AppStatus status) =>
        PageHostOptions(profile)
            .Select(option =>
            {
                var value = status.HostOptions.GetValueOrDefault(option.Key, option.Default);
                return Line("option", option.Key, value, LabelOf(option, value));
            });

    private static IEnumerable<string> OutfitLines(GameProfile profile, string? gameDir)
    {
        var characters = profile.Options?.FirstOrDefault(option => option.Key == GameOptions.CharacterKey)?.Values ?? [];
        foreach (var character in characters)
            if (GameOptions.OutfitOf(profile, character.Value) is { } outfit)
                foreach (var value in GameOptions.Offered(profile, outfit, gameDir))
                    yield return Line("outfit", character.Value, value.Value, value.Label);
    }

    /// <summary>The lines that fit <see cref="MaxBytes"/> as one text (LF between them), in order; the first line that would not fit ends the list.</summary>
    private static IEnumerable<string> WithinCap(IEnumerable<string> lines)
    {
        var bytes = 0;
        foreach (var line in lines)
        {
            bytes += Encoding.UTF8.GetByteCount(line) + (bytes == 0 ? 0 : 1);
            if (bytes > MaxBytes) yield break;
            yield return line;
        }
    }

    private static string Line(params string[] fields) => string.Join(FieldSeparator, fields.Select(RosterCodec.Clean));

    private static string Flag(bool on) => on ? Yes : No;

    private static string LabelOf(GameOption option, string value) => option.Values.FirstOrDefault(v => v.Value == value)?.Label ?? "";

    /// <summary>The name cut to <see cref="MaxNameChars"/> without leaving half a surrogate pair.</summary>
    private static string CutName(string name)
    {
        if (name.Length <= MaxNameChars) return name;
        return char.IsHighSurrogate(name[MaxNameChars - 1]) ? name[..(MaxNameChars - 1)] : name[..MaxNameChars];
    }
}
