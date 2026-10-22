namespace CoopLauncher.Gui;

internal enum StepMark { Pending, Active, Done, Failed }

/// <summary>One line of a step list. Trailing is a right-aligned number (percent, timer). Bar is a share 0..1 under the text, or
/// Indeterminate for a bar of unknown length. Lines are mono lines under the text (the file being received); Pairs are label and
/// value rows (host build against yours).</summary>
internal sealed record StepRow(string Text, StepMark Mark, string Trailing = "", double? Bar = null, bool Indeterminate = false,
    IReadOnlyList<string>? Lines = null, IReadOnlyList<(string Label, string Value)>? Pairs = null);
