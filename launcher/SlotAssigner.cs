namespace CoopLauncher;

public static class SlotAssigner
{
    public const byte HostSlot = 0;

    /// <summary>Host gets slot 0, the rest sorted ascending by id fill 1..maxPlayers-1. Extras get no slot.</summary>
    public static Dictionary<ulong, byte> Assign(ulong hostId, IEnumerable<ulong> memberIds, int maxPlayers)
    {
        var slots = new Dictionary<ulong, byte> { [hostId] = HostSlot };
        var next = HostSlot + 1;
        foreach (var id in memberIds.Where(id => id != hostId).Distinct().Order())
        {
            if (next >= maxPlayers) break;
            slots[id] = (byte)next++;
        }
        return slots;
    }
}
