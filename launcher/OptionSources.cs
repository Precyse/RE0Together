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
        Characters => CampaignStore.List(profile, gameDir).Select(campaign => new OptionValue(campaign.Id, CharacterLabel(profile, gameDir!, campaign))),
        _ => [],
    };

    /// <summary>A campaign's character as the Character list shows it: its name, and its level once the adapter read it.</summary>
    public static string CharacterLabel(GameProfile profile, string gameDir, CampaignStore.Campaign campaign) =>
        CampaignStore.CharacterLevel(CampaignStore.CharacterFile(profile, gameDir, campaign.Id)) is { } level
            ? $"{campaign.Name} / Lv {level}"
            : campaign.Name;

    /// <summary>The campaign a host plays as the guests are told it: its name, or the campaign option's first label (the host's own save).
    /// Empty for a game without campaigns.</summary>
    public static string CampaignLabel(GameProfile profile, string? gameDir)
    {
        var option = profile.Options?.FirstOrDefault(candidate => candidate.Source == Campaigns);
        if (option == null) return string.Empty;
        var id = GameOptions.Current(profile, gameDir)[option.Key];
        if (id.Length == 0) return option.Values[0].Label;
        return gameDir != null ? CampaignStore.List(profile, gameDir).FirstOrDefault(campaign => campaign.Id == id)?.Name ?? id : id;
    }

    /// <summary>The character a guest brings as the others are told it (<see cref="CharacterLabel"/>); empty for a game without one.</summary>
    public static string PickedCharacterLabel(GameProfile profile, string? gameDir)
    {
        var option = profile.Options?.FirstOrDefault(candidate => candidate.Source == Characters);
        if (option == null || gameDir == null) return string.Empty;
        var id = GameOptions.Current(profile, gameDir)[option.Key];
        if (id.Length == 0)
        {
            var level = profile.Campaigns?.OwnCharacter is { } own ? CampaignStore.CharacterLevel(Path.Combine(gameDir, own)) : null;
            return level is { } known ? $"{option.Values[0].Label} / Lv {known}" : option.Values[0].Label;
        }
        return CampaignStore.List(profile, gameDir).FirstOrDefault(campaign => campaign.Id == id) is { } campaign ? CharacterLabel(profile, gameDir, campaign) : id;
    }

    /// <summary>The adapter ini key of the character a guest brings, or null for a game without one.</summary>
    public static string? CharacterOptionKey(GameProfile profile) => profile.Options?.FirstOrDefault(option => option.Source == Characters)?.Key;

    /// <summary>The character a guest picked: a campaign id, or empty for the game's own save; null for a game without the option.</summary>
    public static string? PickedCharacter(GameProfile profile, string gameDir) =>
        CharacterOptionKey(profile) is { } key ? GameOptions.Current(profile, gameDir)[key] : null;

    /// <summary>A game whose guest picks the character it brings: its game starts only once it pressed READY.</summary>
    public static bool GuestPicksCharacter(GameProfile profile) => profile.Options?.Any(option => option.Source == Characters) == true;

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

    /// <summary>Whether the adapter has not read the character a guest picked yet (a capture launch reads it first,
    /// <see cref="CharacterCapture"/>): a campaign's, or the game's own save's.</summary>
    public static bool CharacterUnread(GameProfile profile, string gameDir) => PickedCharacter(profile, gameDir) switch
    {
        null => false,
        "" => profile.Campaigns?.OwnCharacter is { } own && !File.Exists(Path.Combine(gameDir, own)),
        var id => !File.Exists(CampaignStore.CharacterFile(profile, gameDir, id)),
    };

    /// <summary>The world save folder of the campaign a host chose, or null when it plays its own save.</summary>
    public static string? CampaignSaveDir(GameProfile profile, string gameDir)
    {
        var option = profile.Options?.FirstOrDefault(candidate => candidate.Source == Campaigns);
        if (option == null) return null;
        var id = GameOptions.Current(profile, gameDir)[option.Key];
        return id.Length == 0 ? null : CampaignStore.SaveDir(profile, gameDir, id);
    }

    /// <summary>The words for a campaign that holds no saved world yet.</summary>
    public const string NoWorldWords = "This campaign has no saved world yet: start it alone once and save";

    /// <summary>Whether the campaign a host plays holds a world to send: its folder has the profile's first save file. True for the host's
    /// own save and for a game without campaigns (a new campaign is empty until the game wrote it).</summary>
    public static bool CampaignHasWorld(GameProfile profile, string gameDir) =>
        CampaignSaveDir(profile, gameDir) is not { } dir || profile.SaveSync?.SteamRemoteFiles.FirstOrDefault() is not { } first || File.Exists(Path.Combine(dir, first));

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
