namespace CoopLauncher;

/// <summary>The rule for an option the game's own co-op page asks to change (OPTION_SET): only an option the profile has, that
/// is not worked out by the launcher, that this player may set (a host option only for the host) to a value offered here.</summary>
public static class AdapterOptionRequest
{
    /// <summary>Why the request is refused, or null when it is accepted.</summary>
    public static string? WhyRefused(GameProfile profile, string? gameDir, bool localHosts, string key, string value)
    {
        if (profile.Options?.FirstOrDefault(option => option.Key == key) is not { } option) return "unknown option";
        if (option.Source == OptionSources.SaveHash) return "worked out by the launcher";
        if (option.Host && !localHosts) return "host option, not hosting";
        return GameOptions.Offered(profile, option, gameDir).Any(offered => offered.Value == value) ? null : "value not offered";
    }
}
