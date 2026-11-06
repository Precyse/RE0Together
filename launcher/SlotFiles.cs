using System.Globalization;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>The game save slots of a folder whose slots are separate files (Dragon's Dogma 2): <c>data000.bin</c> is slot 0 (the
/// autosave), <c>data00NSlot.bin</c> slot N; the system file (<c>data00-1.bin</c>) and anything else is no game slot. A slot's time is
/// its file's write time. The pick list of the host's forced slot (<see cref="OptionSources.SaveFileSlots"/>) and the capture launch read
/// the same files.</summary>
public static partial class SlotFiles
{
    private const string AutosaveLabel = "Autosave";
    private const string SlotWord = "Slot";
    private const string LabelGap = " / ";
    private const string TimeFormat = "d MMM HH:mm";

    /// <summary>One slot in a folder: its number and when its file was last written.</summary>
    public sealed record Entry(int Number, DateTime WrittenUtc)
    {
        /// <summary>The slot the way the picker lists it: <c>Autosave / 12 Nov 19:31</c>, <c>Slot 1 / 12 Nov 18:46</c>.</summary>
        public string Label => string.Create(CultureInfo.InvariantCulture,
            $"{(Number == 0 ? AutosaveLabel : $"{SlotWord} {Number}")}{LabelGap}{WrittenUtc.ToLocalTime().ToString(TimeFormat, CultureInfo.InvariantCulture)}");
    }

    /// <summary>The slot number a file name stands for, or null when it is no game slot (pure, unit tested).</summary>
    public static int? NumberOf(string fileName) =>
        GameSlot().Match(fileName) is { Success: true } match ? int.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture) : null;

    /// <summary>The slots among file names, the newest first (a slot with no time known ranks last); an unreadable folder or
    /// none is an empty list.</summary>
    public static IReadOnlyList<Entry> OfNames(IEnumerable<string> names, Func<string, DateTime>? writeTime = null) =>
        names.Select(name => (Number: NumberOf(name), Written: writeTime?.Invoke(name) ?? DateTime.MinValue))
            .Where(pair => pair.Number != null)
            .Select(pair => new Entry(pair.Number!.Value, pair.Written))
            .OrderByDescending(entry => entry.WrittenUtc)
            .ToList();

    /// <summary>The slots saved in a folder, the newest first; with <paramref name="only"/> just the files of that list (the ones the save
    /// sync sends).</summary>
    public static IReadOnlyList<Entry> InFolder(string? folder, IReadOnlyCollection<string>? only = null)
    {
        if (folder == null || !Directory.Exists(folder)) return [];
        try
        {
            var names = Directory.GetFiles(folder).Select(Path.GetFileName).OfType<string>()
                .Where(name => only == null || only.Contains(name, StringComparer.OrdinalIgnoreCase));
            return OfNames(names, name => File.GetLastWriteTimeUtc(Path.Combine(folder, name)));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Save slots in {folder} not listed: {e.Message}");
            return [];
        }
    }

    /// <summary>The number of the newest slot among file names, or null when there is none.</summary>
    public static int? Newest(IEnumerable<string> names, Func<string, DateTime>? writeTime = null) => OfNames(names, writeTime).FirstOrDefault()?.Number;

    [GeneratedRegex(@"^data0*(\d+)(?:Slot)?\.bin$", RegexOptions.IgnoreCase)]
    private static partial Regex GameSlot();
}
