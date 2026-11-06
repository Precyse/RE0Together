// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.
namespace CoopLauncher.SaveResign;

/// <summary>The user id a game encrypts its saves for, derived from a SteamID64 by the game's variant (a number in its profile). Pure.</summary>
internal static class MandarinUserId
{
    private const ulong AccountIdMask = 0xFFFFFFFF;
    private const ulong ObfuscationKey = 0x1A3B5C7DD0C2B4A8;
    private const int ObfuscationRotation = 32;
    private const uint SteamIdVariant = 0;
    private const uint AccountIdComplementVariant = 1;
    private const uint SteamIdComplementVariant = 2;
    private const uint ObfuscatedComplementVariant = 3;

    public static bool IsSupported(uint variant) => variant <= ObfuscatedComplementVariant;

    public static ulong FromSteamId64(ulong steamId64, uint variant) => variant switch
    {
        SteamIdVariant => steamId64,
        AccountIdComplementVariant => ~(uint)steamId64 | ~AccountIdMask,
        SteamIdComplementVariant => ~steamId64,
        ObfuscatedComplementVariant => ~ulong.RotateLeft(steamId64 ^ ObfuscationKey, ObfuscationRotation),
        _ => throw new NotSupportedException($"Mandarin user id variant {variant} is not supported"),
    };
}
