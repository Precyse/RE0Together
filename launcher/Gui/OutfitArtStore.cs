using System.Collections.Concurrent;
using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>A character in an outfit as a portrait or tile shows it. A null value is an outfit that is not known and "" the
/// saved one: both show the character's silhouette. The initial stands in when the character has no art at all.</summary>
internal sealed record Figure(string Character, string OptionKey, string? Value, string Initial);

/// <summary>How a figure fills its box: a portrait scales to the box's height and sits in the middle (a wide picture fills
/// the box from the top), a tile fills the box from the top (a figure's head and chest).</summary>
internal enum FigureBox { Portrait, Tile }

/// <summary>The outfit pictures of the selected game as bitmaps, read from the game's files in the background the first time
/// they are wanted for a game and folder (<see cref="OutfitArt"/>). UI thread only. An outfit without a picture shows its
/// character's silhouette (the shape of the character's first picture in <see cref="Theme.Dim"/>), and a character without
/// any picture its initial. Pictures that arrive late are announced by <see cref="Changed"/>, so that what shows can fade
/// them in.</summary>
internal sealed class OutfitArtStore : IDisposable
{
    private const float FullOpacity = 1f;

    private readonly TaskScheduler _ui;
    private readonly Dictionary<string, Bitmap> _pictures = new();
    private readonly Dictionary<string, Bitmap> _silhouettes = new();
    private readonly ConcurrentDictionary<string, bool> _failed = new();
    private string _loadedFor = string.Empty;
    private int _generation;

    private sealed record Job(string OptionKey, string Character, OptionValue Value)
    {
        public string Key => PictureKey(OptionKey, Value.Value);
    }

    /// <summary>Created on the UI thread: pictures come back to it.</summary>
    public OutfitArtStore(TaskScheduler ui) => _ui = ui;

    /// <summary>Raised on the UI thread after pictures were stored.</summary>
    public event Action? Changed;

    /// <summary>Starts reading the pictures of the game's offered outfits, unless this game and folder are already loaded.</summary>
    public void Load(GameProfile game, string? gameDir)
    {
        var key = $"{game.Id}|{gameDir}";
        if (key == _loadedFor) return;
        _loadedFor = key;
        Clear();
        var generation = ++_generation;
        if (gameDir == null) return;
        var jobs = (game.Options ?? []).Where(option => option.Character != null)
            .SelectMany(option => option.AvailableIn(gameDir).Select(value => new Job(option.Key, option.Character!, value)))
            .ToList();
        Task.Run(() => jobs.Select(job => (Job: job, Picture: Read(game, gameDir, job))).Where(read => read.Picture != null).ToList())
            .ContinueWith(task => Arrived(task.Result!, generation), _ui);
    }

    /// <summary>Draws the figure in the box. With <paramref name="arrival"/> (0..1) a picture fades in over what showed until
    /// it arrived; <paramref name="opacity"/> dims the figure (a locked tile).</summary>
    public void Paint(Graphics g, Rectangle box, Figure figure, FigureBox kind, Color background, float opacity = FullOpacity, double? arrival = null)
    {
        var picture = PictureOf(figure);
        if (picture == null)
        {
            PaintStandIn(g, box, figure, kind, opacity);
            return;
        }
        if (arrival is not { } progress)
        {
            PaintPicture(g, box, picture, kind, opacity);
            return;
        }
        MotionLayer.Paint(g, box, Recipes.RevealOut(progress), background, (target, area) => PaintStandIn(target, area, figure, kind, opacity));
        MotionLayer.Paint(g, box, Recipes.RevealIn(progress), background, (target, area) => PaintPicture(target, area, picture, kind, opacity));
    }

    public void Dispose() => Clear();

    private static string PictureKey(string optionKey, string value) => $"{optionKey}/{value}";

    /// <summary>The picture of a job, or null when it has none; one that cannot be read is logged once and has none.</summary>
    private Bitmap? Read(GameProfile game, string gameDir, Job job)
    {
        try
        {
            return OutfitArt.Load(game, gameDir, job.Value);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException)
        {
            if (_failed.TryAdd(job.Key, true)) Log.Info($"Outfit art {job.Value.Value} unreadable: {e.Message}");
            return null;
        }
    }

    private void Arrived(List<(Job Job, Bitmap? Picture)> reads, int generation)
    {
        if (generation != _generation)
        {
            foreach (var read in reads) read.Picture!.Dispose();
            return;
        }
        foreach (var read in reads) _pictures[read.Job.Key] = read.Picture!;
        foreach (var character in reads.GroupBy(read => read.Job.Character))
            _silhouettes[character.Key] = OutfitArt.Silhouette(character.First().Picture!, Theme.Dim);
        Changed?.Invoke();
    }

    private Bitmap? PictureOf(Figure figure) =>
        figure.Value is { Length: > 0 } value && _pictures.TryGetValue(PictureKey(figure.OptionKey, value), out var picture) ? picture : null;

    private void PaintStandIn(Graphics g, Rectangle box, Figure figure, FigureBox kind, float opacity)
    {
        if (_silhouettes.TryGetValue(figure.Character, out var silhouette))
        {
            PaintPicture(g, box, silhouette, kind, opacity);
            return;
        }
        Draw.Fill(g, Theme.Card, box);
        Draw.Centered(g, figure.Initial, Theme.Brand, Interpolate.Lerp(Theme.Card, Theme.Muted, opacity), box);
    }

    private static void PaintPicture(Graphics g, Rectangle box, Bitmap picture, FigureBox kind, float opacity)
    {
        var state = g.Save();
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        var area = kind == FigureBox.Portrait ? FitHeight(picture, box) : box;
        if (opacity < FullOpacity) Draw.Faded(g, picture, area, opacity, anchorTop: true);
        else Draw.Cover(g, picture, area, anchorTop: true);
        g.Restore(state);
    }

    /// <summary>The box, or when the picture scaled to the box's height is narrower than the box, that width in the middle.</summary>
    private static Rectangle FitHeight(Bitmap picture, Rectangle box)
    {
        var width = (int)Math.Round((double)picture.Width * box.Height / picture.Height);
        return width < box.Width ? new Rectangle(box.X + (box.Width - width) / 2, box.Y, width, box.Height) : box;
    }

    private void Clear()
    {
        foreach (var picture in _pictures.Values) picture.Dispose();
        foreach (var silhouette in _silhouettes.Values) silhouette.Dispose();
        _pictures.Clear();
        _silhouettes.Clear();
    }
}
