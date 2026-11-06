using System.Globalization;

namespace CoopLauncher;

/// <summary>Options whose values this machine supplies at run time (a profile option with a <c>source</c>): the save slot a host forces
/// the game to load at launch, and the hash of that slot, which tells a guest's copy of the save from a stale one. The choice is the
/// slot's number (empty = none, the title screen as usual); the hash is worked out from the save, never chosen. The campaign a host
/// plays and the character a guest brings (<see cref="CampaignStore"/>) are chosen in the options grids like any option; picking
/// <see cref="NewCampaign"/> or <see cref="CopyCampaign"/> makes a campaign and chooses it (<see cref="Choose"/>).</summary>
public static class OptionSources
{
    public const string SaveSlots = "saveSlots";
    public const string SaveHash = "saveHash";
    public const string Campaigns = "campaigns";
    public const string Characters = "characters";
    public const string NewCampaign = "new";
    public const string CopyCampaign = "copy";
    private const string NewCampaignLabel = "New campaign";
    private const string CopyCampaignLabel = "New from own save";
    public const string SlotKey = "load_slot";
    public const string HashKey = "load_hash";

    /// <summary>The option that picks the slot to load, or null when the game has none.</summary>
    public static GameOption? SlotOption(GameProfile profile) => profile.Options?.FirstOrDefault(option => option.Source == SaveSlots);

    /// <summary>The used slots of the host's save as choices (value: the slot's number from 0, label: <see cref="SaveSlot.Label"/>); none for an
    /// option with another source or when the save cannot be read.</summary>
    public static IEnumerable<OptionValue> Values(GameProfile profile, GameOption option, string? gameDir) => option.Source switch
    {
        SaveSlots => SaveIndexFiles.ReadHost(profile, gameDir).Slots.Where(slot => slot.Used).Select(slot => new OptionValue(SlotValue(slot), slot.Label)),
        Campaigns => CampaignStore.List(profile, gameDir).Select(campaign => new OptionValue(campaign.Id, campaign.Name))
            .Concat([new OptionValue(NewCampaign, NewCampaignLabel), new OptionValue(CopyCampaign, CopyCampaignLabel)]),
        Characters => CampaignStore.List(profile, gameDir).Where(campaign => File.Exists(CampaignStore.CharacterFile(profile, gameDir!, campaign.Id)))
            .Select(campaign => new OptionValue(campaign.Id, campaign.Name)),
        _ => [],
    };

    /// <summary>An option the player picks in the grids although its values come from this machine (campaign, character).</summary>
    public static bool IsChoice(GameOption option) => option.Source is Campaigns or Characters;

    /// <summary>The value to keep for a pick: a new campaign's id for <see cref="NewCampaign"/> (an empty world: the game starts a new
    /// one) or <see cref="CopyCampaign"/> (a copy of the game's own save), else the pick itself.</summary>
    public static string Choose(GameProfile profile, string gameDir, GameOption option, string value)
    {
        if (option.Source != Campaigns || value is not (NewCampaign or CopyCampaign)) return value;
        var name = CampaignStore.NextName(CampaignStore.List(profile, gameDir));
        var ownSave = value == CopyCampaign && profile.SaveSync?.HostSaveDir is { } template ? SavePaths.Expand(template) : null;
        return CampaignStore.Create(profile, gameDir, name, ownSave).Id;
    }

    /// <summary>The world save folder of the campaign a host chose, or null when it plays its own save.</summary>
    public static string? CampaignSaveDir(GameProfile profile, string gameDir)
    {
        var option = profile.Options?.FirstOrDefault(candidate => candidate.Source == Campaigns);
        if (option == null) return null;
        var id = GameOptions.Current(profile, gameDir)[option.Key];
        return id.Length == 0 ? null : CampaignStore.SaveDir(profile, gameDir, id);
    }

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
