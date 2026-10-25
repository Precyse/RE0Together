using System.Globalization;

namespace CoopLauncher;

/// <summary>Options whose values this machine supplies at run time (a profile option with a <c>source</c>): the save slot a host forces
/// the game to load at launch, and the hash of that slot, which tells a guest's copy of the save from a stale one. The choice is the
/// slot's number (empty = none, the title screen as usual); the hash is worked out from the save, never chosen.</summary>
public static class OptionSources
{
    public const string SaveSlots = "saveSlots";
    public const string SaveHash = "saveHash";
    public const string SlotKey = "load_slot";
    public const string HashKey = "load_hash";

    /// <summary>The option that picks the slot to load, or null when the game has none.</summary>
    public static GameOption? SlotOption(GameProfile profile) => profile.Options?.FirstOrDefault(option => option.Source == SaveSlots);

    /// <summary>The used slots of the host's save as choices (value: the slot's number from 0, label: <see cref="SaveSlot.Label"/>); none for an
    /// option with another source or when the save cannot be read.</summary>
    public static IEnumerable<OptionValue> Values(GameProfile profile, GameOption option, string? gameDir) =>
        option.Source == SaveSlots
            ? SaveIndexFiles.ReadHost(profile, gameDir).Slots.Where(slot => slot.Used).Select(slot => new OptionValue(SlotValue(slot), slot.Label))
            : [];

    /// <summary>The hash of the chosen slot in the host's save, empty when no slot is chosen or it is not in the save.</summary>
    public static string SlotHash(GameProfile profile, string? gameDir, string? slotValue) =>
        SlotOf(SaveIndexFiles.ReadHost(profile, gameDir), slotValue)?.Hash ?? string.Empty;

    /// <summary>Whether this guest's copy of the save holds the slot the host forces, byte for byte (the host's hash). True when the host
    /// forces none.</summary>
    public static bool SessionCopyMatches(GameProfile profile, string? gameDir, IReadOnlyDictionary<string, string> hostValues)
    {
        if (!hostValues.TryGetValue(SlotKey, out var slotValue) || slotValue.Length == 0) return true;
        var copy = SlotOf(SaveIndexFiles.ReadSession(profile, gameDir), slotValue);
        return copy != null && copy.Hash == hostValues.GetValueOrDefault(HashKey);
    }

    private static string SlotValue(SaveSlot slot) => slot.Index.ToString(CultureInfo.InvariantCulture);

    private static SaveSlot? SlotOf(SaveIndex index, string? slotValue) =>
        int.TryParse(slotValue, NumberStyles.None, CultureInfo.InvariantCulture, out var number)
            ? index.Slots.FirstOrDefault(slot => slot.Index == number && slot.Used)
            : null;
}
