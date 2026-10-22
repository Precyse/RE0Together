namespace CoopLauncher.Gui;

/// <summary>One line the home hero or a session screen has to say: the key tells updates of the same notice from a different one. The
/// caller supplies every word.</summary>
public sealed record Notice(string Key, string Words, string? Action, bool Report);
