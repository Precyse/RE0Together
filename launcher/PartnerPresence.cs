namespace CoopLauncher;

/// <summary>What the local adapter should hear about a partner's game.</summary>
public enum PresenceChange { None, Down, Up }

/// <summary>Whether each partner's game is in the session, from the step its launcher publishes (a partner whose launcher stays up while
/// its game crashed or closed is still a lobby member, so membership never says it left). A partner leaves with GameClosed, or with
/// Refused or Lobby after StartingGame or InGame; it is back when it publishes InGame again. Each change is reported once;
/// <see cref="AdapterToldUp"/> says the adapter was told the partner is up (membership), so a game still gone is reported again.</summary>
public sealed class PartnerPresence
{
    private sealed class Partner
    {
        public string Text = string.Empty;
        public SessionStep Last = SessionStep.None;
        public bool GameGone;  // the game left and has not been in the world since
        public bool Told;  // the adapter was told it is gone
    }

    private readonly Dictionary<ulong, Partner> _partners = new();

    /// <summary>The change a partner's published step text (<see cref="StepCodec"/>) brings; none for a text already seen or a step that is
    /// not known.</summary>
    public PresenceChange Observe(ulong id, string stepText)
    {
        var partner = PartnerOf(id);
        if (stepText == partner.Text || !StepCodec.TryDecode(stepText, out var report) || report.Step == SessionStep.None) return PresenceChange.None;
        partner.Text = stepText;
        if (Left(report.Step, partner.Last)) partner.GameGone = true;
        else if (report.Step == SessionStep.InGame) partner.GameGone = false;
        partner.Last = report.Step;
        if (partner.GameGone == partner.Told) return PresenceChange.None;
        partner.Told = partner.GameGone;
        return partner.Told ? PresenceChange.Down : PresenceChange.Up;
    }

    /// <summary>The adapter was told this partner is up: its step is looked at again.</summary>
    public void AdapterToldUp(ulong id)
    {
        var partner = PartnerOf(id);
        partner.Told = false;
        partner.Text = string.Empty;
    }

    /// <summary>The partner is out of the session: nothing is remembered about it.</summary>
    public void Forget(ulong id) => _partners.Remove(id);

    private Partner PartnerOf(ulong id)
    {
        if (!_partners.TryGetValue(id, out var partner)) _partners[id] = partner = new Partner();
        return partner;
    }

    private static bool Left(SessionStep step, SessionStep last) =>
        step == SessionStep.GameClosed
        || (step is SessionStep.Refused or SessionStep.Lobby && last is SessionStep.StartingGame or SessionStep.InGame);
}
