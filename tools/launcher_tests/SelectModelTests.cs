using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The character select's pure mapping: who is on which half, what each half may do (arrows, lock, opacity), the dots and
/// the outfit the arrows step to.</summary>
public static class SelectModelTests
{
    private const string Game = "re0";
    private const double Tolerance = 1e-6;

    public static void Run()
    {
        using var data = new TempData();
        TempData.UseDefaults();
        BeforeASession();
        HostInALobby();
        GuestInALobby();
        GuestChangesItsOutfit();
        SavedShowsTheDefaultOutfit();
        LockedOnceTheGameStarts();
        Stepping();
    }

    private static GameProfile Profile() => GameProfile.Load(Game);

    private static PlayerSlot Slot(string name, bool host, bool local, SessionStep step = SessionStep.Lobby, string options = "") =>
        new(local ? 1UL : 2UL, name, host, local, "", step, 0, null, false, false, options);

    private static AppStatus Session(SessionStep step, params PlayerSlot[] players) =>
        new(AppState.PeerConnected, 1) { GameId = Game, Players = players, Step = step, SeatCharacters = ["Rebecca", "Billy"] };

    private static void BeforeASession()
    {
        var model = SelectModels.Build(Profile(), null, AppStatus.Idle, free: true);
        Check.That(model.Left.Kind == HalfKind.Own && model.Right.Kind == HalfKind.Partner, "before a session you are the left half and the other character the right");
        Check.That(model.Centre == StageCentre.Swap && model.SwapCharacter == model.Right.Figure.Character, "the swap button picks the right half's character");
        Check.That(model.Left.HasArrows && model.Right.HasArrows && model.Left.CanCycle && model.Right.CanCycle, "both halves' arrows work before a session");
        Check.That(model.Left.Opacity == 1f && model.Right.Opacity < model.Left.Opacity, "the partner's figure is dimmed");
        Check.That(model.Left.DotCount == model.Left.Outfits.Count && model.Left.Index >= 0 && model.Left.Index < model.Left.DotCount, "the dots count the outfits and the index is one of them");
        Check.That(model.OwnHalf == 0, "your half is the left one");
    }

    private static void HostInALobby()
    {
        var model = SelectModels.Build(Profile(), null, Session(SessionStep.Lobby, Slot("Precyse", host: true, local: true), Slot("Amy", host: false, local: false)), free: false);
        Check.That(model.Left.Kind == HalfKind.Own && model.Right.Kind == HalfKind.ReadOnly, "the host is the left half and the guest's half is read-only");
        Check.That(model.Right.Role == "Amy" && model.Right.DotCount == 0 && !model.Right.HasArrows && model.Right.Opacity < 1f, "the guest's half has the guest's name, no arrows and no dots");
        Check.That(model.Centre == StageCentre.Swap && model.SwapCharacter == "billy" && model.Left.CanCycle, "a lobby shows the swap to the other character and your outfit still cycles");
        var open = SelectModels.Build(Profile(), null, Session(SessionStep.Lobby, Slot("Precyse", host: true, local: true)), free: false);
        Check.That(open.Right.Role == "Open", "a host without a guest shows an open half");
    }

    private static void GuestInALobby()
    {
        var host = Slot("Precyse", host: true, local: false, options: "outfit_rebecca=85");
        var model = SelectModels.Build(Profile(), null, Session(SessionStep.Lobby, host, Slot("Amy", host: false, local: true)), free: false);
        Check.That(model.Left.Kind == HalfKind.ReadOnly && model.Right.Kind == HalfKind.Own, "a guest is the right half and the host's is the left, read-only");
        Check.That(model.Left.Role == "Precyse Host" && model.Left.Figure.Character == "rebecca" && model.Right.Figure.Character == "billy", "the host's character and name read from the seats");
        Check.That(model.Left.Figure.Value == "85" && model.Left.Label == "Nurse", "the host's published outfit shows");
        Check.That(model.OwnHalf == 1 && Math.Abs(model.Left.Opacity - 0.8) < Tolerance, "your half is the right one and the host's figure is at 80 %");
        Check.That(model.Centre == StageCentre.Swap && model.SwapCharacter == "rebecca", "a guest can swap too: the swap asks for the host's character");
        var unknown = SelectModels.Build(Profile(), null, Session(SessionStep.Lobby, Slot("Precyse", host: true, local: false), Slot("Amy", host: false, local: true)), free: false);
        Check.That(unknown.Left.Label.Length == 0 && unknown.Left.Figure.Value == null, "an outfit that was not published shows no label and the silhouette");
    }

    private static AppStatus GuestLobby() =>
        Session(SessionStep.Ready, Slot("Precyse", host: true, local: false, options: "outfit_rebecca=85"), Slot("Amy", host: false, local: true));

    /// <summary>The guest's right half cycles its own seat character's outfit, as the controller does it: the choice is saved under
    /// the half's outfit key and the next build shows it.</summary>
    private static void GuestChangesItsOutfit()
    {
        var shown = SelectModels.Build(Profile(), null, GuestLobby(), free: false).Right;
        Check.That(shown.CanCycle && shown.Figure.OptionKey == "outfit_billy" && shown.Index == 0, "a guest's half cycles the outfit of its seat's character");
        OptionsModel.Save(Game, shown.Figure.OptionKey, SelectModels.Neighbor(shown, 1).Value);
        var after = SelectModels.Build(Profile(), null, GuestLobby(), free: false).Right;
        Check.That(after.Index == 1 && after.Label == "Prisoner" && after.Figure.Value == "05", "the next build shows the chosen outfit");
        Check.That(GameOptions.OwnValues(Profile(), null)["outfit_billy"] == "05", "and it is what the guest publishes");
        OptionsModel.Save(Game, "outfit_billy", "");
    }

    private static void SavedShowsTheDefaultOutfit()
    {
        var saved = SelectModels.Build(Profile(), null, GuestLobby(), free: false).Right;
        Check.That(saved.Label == "Saved" && saved.Figure.Value == "05" && saved.Outfits[0].Label == "Saved" && saved.Outfits[0].Figure.Value == "05",
            "the saved outfit is labelled Saved and shows the character's first outfit");
        Check.That(saved.Outfits[0].Value == string.Empty, "its tile still sets the saved value");
    }

    private static void LockedOnceTheGameStarts()
    {
        foreach (var (step, locked) in new[]
                 {
                     (SessionStep.Lobby, false), (SessionStep.Ready, false), (SessionStep.CheckingBuild, false), (SessionStep.ReceivingSave, false),
                     (SessionStep.StartingGame, true), (SessionStep.InGame, true), (SessionStep.GameClosed, true),
                 })
        {
            var model = SelectModels.Build(Profile(), null, Session(step, Slot("Precyse", host: true, local: true), Slot("Amy", host: false, local: false)), free: false);
            Check.That(model.Left.Locked == locked && model.Left.CanCycle == !locked, $"your outfit is {(locked ? "locked" : "open")} at {step}");
            Check.That(model.Centre == (locked ? StageCentre.Lock : StageCentre.Swap), $"the centre is {(locked ? "the lock" : "the swap")} at {step}");
            var guest = SelectModels.Build(Profile(), null, Session(step, Slot("Precyse", host: true, local: false), Slot("Amy", host: false, local: true)), free: false);
            Check.That(guest.Centre == model.Centre, $"a guest's centre follows the same rule at {step}");
        }
    }

    private static void Stepping()
    {
        var half = SelectModels.Build(Profile(), null, AppStatus.Idle, free: true).Left;
        var count = half.Outfits.Count;
        Check.That(SelectModels.Neighbor(half, 1).Value == half.Outfits[(half.Index + 1) % count].Value, "next is the following outfit");
        Check.That(SelectModels.Neighbor(half with { Index = count - 1 }, 1) == half.Outfits[0], "next wraps from the last to the first");
        Check.That(SelectModels.Neighbor(half with { Index = 0 }, -1) == half.Outfits[count - 1], "previous wraps from the first to the last");
    }
}
