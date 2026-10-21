using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>What the character select paints for one half and how it moves: the figure (cross-fades when the character or role
/// changes, slides sideways when only the outfit changes, crosses to the other half when the characters swap), the name and
/// role, the outfit's label (words swap) and the dots, whose active one slides to the new outfit. The arrows are the stage's
/// controls. Painted by <see cref="CharacterStage"/>, which owns the control.</summary>
internal sealed class StageHalfView
{
    private const string CrossChannel = "cross";
    private const string SlideChannel = "slide";
    private const string DotChannel = "dot";
    private const int NameSpacing = 1;
    private const int RoleGap = 10;

    private readonly Control _owner;
    private readonly OutfitArtStore _art;
    private readonly string _prefix;
    private readonly StatusLine _label;
    private StageHalf? _leaving;
    private bool _traded;
    private double _cross;
    private Figure? _slidingFrom;
    private int _direction = 1;
    private double _slide;
    private double _dotAt;

    public StageHalfView(Control owner, OutfitArtStore art, string prefix, StageHalf model)
    {
        _owner = owner;
        _art = art;
        _prefix = prefix;
        Model = model;
        _label = new StatusLine(owner, prefix + "/label", centered: true);
        _label.Snap(model.Label);
        _dotAt = Math.Max(0, model.Index);
    }

    public StageHalf Model { get; private set; }

    /// <summary>Shows the half's next state. A different character, role or kind cross-fades (or, for <paramref name="traded"/>,
    /// brings the figure across from the other half); a different outfit slides in the direction (1 = next, -1 = previous).</summary>
    public void Show(StageHalf next, int direction, bool traded)
    {
        var before = Model;
        if (before == next) return;
        Model = next;
        if (before.Label != next.Label) _label.Set(next.Label);
        if (Identity(before) != Identity(next))
        {
            Cross(before, traded);
            Motion.Cancel(_owner, DotChannel);
            _dotAt = Math.Max(0, next.Index);
        }
        else
        {
            if (before.Figure != next.Figure) SlideOutfit(before.Figure, direction);
            if (before.Index != next.Index) MoveDot(next.Index);
        }
        _owner.Invalidate();
    }

    public void Dispose()
    {
        foreach (var channel in new[] { CrossChannel, SlideChannel, DotChannel }) Motion.Cancel(_owner, channel);
        Motion.Cancel(_owner, _prefix + "/label");
    }

    /// <summary>Paints the half. <paramref name="otherFigure"/> is the other half's figure box (where a traded figure comes
    /// from); <paramref name="arrival"/> fades pictures that arrived late in.</summary>
    public void Paint(Graphics g, HalfLayout layout, Rectangle otherFigure, double? arrival)
    {
        PaintFigure(g, layout.Figure, otherFigure, arrival);
        PaintNames(g, layout.Name);
        var kind = Model.Kind;
        _label.Paint(g, layout.Label, Theme.NameTall, kind == HalfKind.Own ? Theme.Text : Theme.Muted, Theme.Bg);
        if (Model.DotCount > 0) PaintDots(g, layout.Dots);
    }

    private static (string Character, string Role, HalfKind Kind) Identity(StageHalf half) => (half.Figure.Character, half.Role, half.Kind);

    private void Cross(StageHalf before, bool traded)
    {
        Motion.Cancel(_owner, SlideChannel);
        _slidingFrom = null;
        _leaving = before;
        _traded = traded;
        _cross = 0;
        Motion.Animate(_owner, CrossChannel, 0, 1, traded ? Durations.Cross : Durations.Reveal, Easing.InOut, progress =>
        {
            _cross = progress;
            _owner.Invalidate();
        }, () =>
        {
            _leaving = null;
            _traded = false;
            _owner.Invalidate();
        });
    }

    private void SlideOutfit(Figure from, int direction)
    {
        _slidingFrom = from;
        _direction = direction;
        _slide = 0;
        Motion.Animate(_owner, SlideChannel, 0, 1, Durations.Outfit, Easing.Out, progress =>
        {
            _slide = progress;
            _owner.Invalidate();
        }, () =>
        {
            _slidingFrom = null;
            _owner.Invalidate();
        });
    }

    private void MoveDot(int index)
    {
        if (index < 0) return;
        Motion.Animate(_owner, DotChannel, Motion.ValueOf(_owner, DotChannel) ?? _dotAt, index, Durations.Tabs, Easing.Out, position =>
        {
            _dotAt = position;
            _owner.Invalidate();
        });
    }

    private void PaintFigure(Graphics g, Rectangle box, Rectangle otherBox, double? arrival)
    {
        var half = Model;
        if (_leaving is { } leaving)
        {
            if (_traded)
            {
                _art.Paint(g, Interpolate.Lerp(otherBox, box, _cross), half.Figure, FigureBox.Portrait, Theme.Bg, half.Opacity);
                return;
            }
            MotionLayer.Paint(g, box, Recipes.RevealOut(_cross), Color.Transparent,
                (target, area) => _art.Paint(target, area, leaving.Figure, FigureBox.Portrait, Theme.Bg, leaving.Opacity));
            MotionLayer.Paint(g, box, Recipes.RevealIn(_cross), Color.Transparent,
                (target, area) => _art.Paint(target, area, half.Figure, FigureBox.Portrait, Theme.Bg, half.Opacity));
            return;
        }
        if (_slidingFrom is { } from)
        {
            var room = Rectangle.Inflate(box, StageLayout.SlideRoom, 0);
            MotionLayer.Paint(g, room, Recipes.OutfitOut(_slide, _direction), Color.Transparent,
                (target, area) => _art.Paint(target, Rectangle.Inflate(area, -StageLayout.SlideRoom, 0), from, FigureBox.Portrait, Theme.Bg, half.Opacity));
            MotionLayer.Paint(g, room, Recipes.OutfitIn(_slide, _direction), Color.Transparent,
                (target, area) => _art.Paint(target, Rectangle.Inflate(area, -StageLayout.SlideRoom, 0), half.Figure, FigureBox.Portrait, Theme.Bg, half.Opacity));
            return;
        }
        _art.Paint(g, box, half.Figure, FigureBox.Portrait, Theme.Bg, half.Opacity, arrival);
    }

    /// <summary>Name and role, side by side and centred in the row; the old pair fades out and the new one in while the
    /// character or role changes.</summary>
    private void PaintNames(Graphics g, Rectangle row)
    {
        if (_leaving is not { } leaving)
        {
            PaintNameGroup(g, row, Model);
            return;
        }
        MotionLayer.Paint(g, row, Recipes.RevealOut(_cross), Theme.Bg, (target, area) => PaintNameGroup(target, area, leaving));
        MotionLayer.Paint(g, row, Recipes.RevealIn(_cross), Theme.Bg, (target, area) => PaintNameGroup(target, area, Model));
    }

    private static void PaintNameGroup(Graphics g, Rectangle row, StageHalf half)
    {
        var spacing = Theme.Scale(NameSpacing);
        var nameWidth = Draw.SpacedWidth(half.Name, Theme.Hero, spacing);
        var roleWidth = Draw.Width(half.Role, Theme.StepSmall);
        var x = row.X + (row.Width - (nameWidth + Theme.Scale(RoleGap) + roleWidth)) / 2;
        Draw.TextSpaced(g, half.Name, Theme.Hero, half.Kind == HalfKind.Partner ? Theme.Muted : Theme.Text, new Rectangle(x, row.Y, nameWidth, row.Height), spacing);
        var top = row.Y + Draw.Ascent(Theme.Hero) - Draw.Ascent(Theme.StepSmall);
        Draw.Text(g, half.Role, Theme.StepSmall, Theme.Muted, new Rectangle(x + nameWidth + Theme.Scale(RoleGap), top, roleWidth, Theme.StepSmall.Height));
    }

    /// <summary>One round dot per outfit, the active one a brighter dot that slides to the outfit now shown.</summary>
    private void PaintDots(Graphics g, Rectangle row)
    {
        var count = Model.DotCount;
        var span = (count - 1) * StageLayout.DotSpacing + StageLayout.Dot;
        var left = row.X + (row.Width - span) / 2;
        var own = Model.Kind == HalfKind.Own;
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (var rest = new SolidBrush(own ? Theme.Dim : Theme.Selection))
            for (var i = 0; i < count; i++)
                g.FillEllipse(rest, left + i * StageLayout.DotSpacing, row.Y, StageLayout.Dot, StageLayout.Dot);
        using (var active = new SolidBrush(own ? Theme.Text : Theme.Muted))
            g.FillEllipse(active, (float)(left + _dotAt * StageLayout.DotSpacing), row.Y, StageLayout.Dot, StageLayout.Dot);
        g.Restore(state);
    }
}
