using CoopLauncher;

namespace LauncherTests;

/// <summary>SaveIndex on a synthetic save (never a real one): the entry name hashes, the slots with their fields and hash, the labels, and every way
/// a file is refused (wrong key, damaged, wrong size, an entry missing or of the wrong size). Also the FNV-1a vectors and the PE key read.</summary>
public static class SaveIndexTests
{
    public static void Run()
    {
        NameHashes();
        FnvVectors();
        SlotsAndLabels();
        Refusals();
        KeyFromTheExecutable();
    }

    private static void NameHashes()
    {
        Check.That(SaveIndex.NameHash("VERSION") == 0x7FA6D226, "save index: VERSION hashes as the game stores it");
        Check.That(SaveIndex.NameHash("SYSTEMDATA") == 0xD940A282, "save index: SYSTEMDATA");
        Check.That(SaveIndex.NameHash("GAMEDATA0") == 0x16AEC1DF && SaveIndex.NameHash("GAMEDATA4") == 0x11C305C6, "save index: GAMEDATA0 and GAMEDATA4");
    }

    private static void FnvVectors()
    {
        Check.That(Fnv1a.Hash("a"u8) == 0xE40C292C && Fnv1a.Hash("foobar"u8) == 0xBF9CF968 && Fnv1a.Hash([]) == 0x811C9DC5, "fnv1a: the published 32-bit vectors");
        Check.That(SaveIndex.HashOf("a"u8) == "e40c292c", "save index: a slot hash is 8 lowercase hex digits");
    }

    private static void SlotsAndLabels()
    {
        var index = SaveIndex.Parse(SaveFixture.Build(SaveFixture.Slots, SaveFixture.Key), SaveFixture.Key, SaveFixture.Layout);
        Check.That(index.Failure == null && index.Slots.Count == SaveFixture.SlotCount, $"save index: a good file lists its slots ({index.Failure})");
        var first = index.Slots[0];
        Check.That(first.Used && first.Area == 4 && first.SaveCount == 11 && first.Difficulty == 1 && !first.Wesker && first.Room == 0x59 && !first.Cleared,
            "save index: slot 0 fields (area, save count, difficulty, Wesker, room)");
        Check.That(first.PlayTime == TimeSpan.FromSeconds(137), "save index: 8200 frames at 60 Hz are 2:17");
        Check.That(first.Label == "Slot 1 / Normal / 0:02:17 / Area 4", $"save index: the label of slot 0 ({first.Label})");
        var second = index.Slots[1];
        Check.That(second.Wesker && second.Difficulty == 2 && second.Label == "Slot 2 / Hard / 27:46:40 / Area 13 / Wesker", $"save index: Wesker, Hard and a play time past 24 hours ({second.Label})");
        Check.That(!index.Slots[2].Used && !index.Slots[3].Used, "save index: a slot with no area, count, flag and mode bytes is empty");
        Check.That(first.Hash == SaveIndex.HashOf(SaveFixture.SlotBytes(SaveFixture.Slots[0])) && first.Hash != second.Hash && index.Slots[2].Hash == index.Slots[3].Hash,
            "save index: the hash is the FNV-1a of the slot's bytes");
        var cleared = SaveIndex.Parse(SaveFixture.Build([new SyntheticSlot(0, 0x0C, 1, 0, 60f, false, 1)], SaveFixture.Key), SaveFixture.Key, SaveFixture.Layout).Slots[0];
        Check.That(cleared.Cleared && cleared.Label == "Slot 1 / Easy / 0:00:01 / Cleared", $"save index: area 0x0c is a cleared save ({cleared.Label})");
        var odd = SaveIndex.Parse(SaveFixture.Build([new SyntheticSlot(0, 1, 1, 9, float.NaN, false, 1)], SaveFixture.Key), SaveFixture.Key, SaveFixture.Layout).Slots[0];
        Check.That(odd.Label == "Slot 1 / 0:00:00 / Area 1", $"save index: an unknown difficulty and a play time that is not a number are left out or zero ({odd.Label})");
    }

    private static void Refusals()
    {
        var file = SaveFixture.Build(SaveFixture.Slots, SaveFixture.Key);
        var wrongKey = SaveIndex.Parse(file, "another-key-another-key-another!"u8, SaveFixture.Layout);
        Check.That(wrongKey.Slots.Count == 0 && wrongKey.Failure != null, "save index: the wrong key lists nothing and says why");
        var damaged = (byte[])file.Clone();
        damaged[^1] ^= 0xFF;
        Check.That(SaveIndex.Parse(damaged, SaveFixture.Key, SaveFixture.Layout).Failure?.Contains("checksum") == true, "save index: a damaged file fails the header checksum");
        Check.That(SaveIndex.Parse(file.AsSpan(0, file.Length - 3), SaveFixture.Key, SaveFixture.Layout).Failure?.Contains("wrong size") == true, "save index: a length that is not whole blocks is refused");
        Check.That(SaveIndex.Parse(file.AsSpan(0, 16), SaveFixture.Key, SaveFixture.Layout).Failure != null, "save index: a file shorter than the header is refused");
        Check.That(SaveIndex.Parse([], SaveFixture.Key, SaveFixture.Layout).Failure != null, "save index: an empty file is refused");
        var missing = SaveFixture.BuildEntries([("GAMEDATA0", SaveFixture.SlotBytes(null)), ("GAMEDATA1", SaveFixture.SlotBytes(null))], SaveFixture.Key);
        Check.That(SaveIndex.Parse(missing, SaveFixture.Key, SaveFixture.Layout).Failure?.Contains("GAMEDATA2") == true, "save index: a slot entry that is not in the file is refused by name");
        var small = SaveFixture.BuildEntries(Enumerable.Range(0, SaveFixture.SlotCount).Select(i => ($"GAMEDATA{i}", new byte[SaveFixture.SlotSize - 8])).ToList(), SaveFixture.Key);
        Check.That(SaveIndex.Parse(small, SaveFixture.Key, SaveFixture.Layout).Failure?.Contains("wrong size") == true, "save index: a slot of the wrong size is refused");
        Check.That(SaveIndex.Parse(file, SaveFixture.Key, SaveFixture.Layout with { SlotSize = 100 }).Failure != null, "save index: a profile whose slot size cannot hold the fields is refused");
    }

    private static void KeyFromTheExecutable()
    {
        var root = Path.Combine(Path.GetTempPath(), "coop-pe-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var exe = Path.Combine(root, "game.exe");
            File.WriteAllBytes(exe, SaveFixture.Exe(SaveFixture.Key));
            Check.That(PeImage.ReadAt(exe, SaveFixture.KeyAddress, SaveFixture.Key.Length)?.SequenceEqual(SaveFixture.Key) == true, "pe: bytes are read by virtual address through the section table");
            Check.That(PeImage.ReadAt(exe, SaveFixture.KeyAddress + 0x1F0, 0x20) == null, "pe: bytes that run past the section are not read");
            Check.That(PeImage.ReadAt(exe, 0x00500000, 4) == null && PeImage.ReadAt(exe, 0x00100000, 4) == null, "pe: an address in no section, or below the image base, is not read");
            var text = Path.Combine(root, "readme.txt");
            File.WriteAllText(text, "not an executable at all, just text that is long enough to have a header");
            Check.That(PeImage.ReadAt(text, SaveFixture.KeyAddress, 4) == null && PeImage.ReadAt(Path.Combine(root, "none.exe"), SaveFixture.KeyAddress, 4) == null, "pe: text and a missing file give nothing");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
