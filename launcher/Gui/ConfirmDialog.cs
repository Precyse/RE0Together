namespace CoopLauncher.Gui;

/// <summary>The themed yes/no question, modal over a window: a card with the question and two buttons. It opens by
/// scaling up and fading in (modal) and closes by scaling down before it returns.</summary>
internal static class ConfirmDialog
{
    /// <summary>Asks the question; true when the player confirmed, false for the cancel button, Esc or closing the dialog.</summary>
    public static bool Ask(IWin32Window owner, string question, string confirm, string cancel)
    {
        using var dialog = new ConfirmForm(question, confirm, cancel);
        dialog.PlaceOver(owner);
        dialog.ShowDialog(owner);
        return dialog.Confirmed;
    }
}

/// <summary>The dialog window of <see cref="ConfirmDialog"/>. Focus starts on the cancel button; Esc cancels; Enter
/// presses the focused button. Clicks work while it opens.</summary>
internal sealed class ConfirmForm : Form
{
    private static readonly int DialogWidth = Theme.Scale(380);
    private static readonly int DialogHeight = Theme.Scale(136);
    private static readonly int DialogPad = Theme.Scale(24);
    private static readonly int ConfirmWidth = Theme.Scale(168);

    private readonly CardPanel _card;
    private readonly FlatButton _confirm;
    private readonly FlatButton _cancel;
    private bool _closing;

    public ConfirmForm(string question, string confirm, string cancel)
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        ShowInTaskbar = false;
        KeyPreview = true;
        BackColor = Theme.Card;
        Opacity = 0;
        ClientSize = new Size(DialogWidth, DialogHeight);

        _card = new CardPanel(question) { Dock = DockStyle.Fill };
        _confirm = new FlatButton(confirm, ButtonKind.Primary) { Width = ConfirmWidth };
        _cancel = new FlatButton(cancel, ButtonKind.Ghost) { Width = Theme.ToolWidth };
        _confirm.Click += (_, _) => Choose(true);
        _cancel.Click += (_, _) => Choose(false);
        _card.Controls.Add(_confirm);
        _card.Controls.Add(_cancel);
        Controls.Add(_card);

        var y = DialogHeight - DialogPad - Theme.ControlHeight;
        _cancel.Location = new Point(DialogWidth - DialogPad - _cancel.Width, y);
        _confirm.Location = new Point(_cancel.Left - Theme.Gap - _confirm.Width, y);
        _confirm.TabIndex = 0;
        _cancel.TabIndex = 1;
        ActiveControl = _cancel;
    }

    public bool Confirmed { get; private set; }

    /// <summary>Centres the dialog over the owner window.</summary>
    public void PlaceOver(IWin32Window owner)
    {
        var window = Control.FromHandle(owner.Handle)?.FindForm();
        var area = window?.Bounds ?? Screen.PrimaryScreen!.WorkingArea;
        Location = new Point(area.X + (area.Width - Width) / 2, area.Y + (area.Height - Height) / 2);
    }

    /// <summary>Plays the open motion; the dialog is already clickable.</summary>
    public void PlayOpen()
    {
        Motion.Animate(this, nameof(Opacity), 0, 1, Durations.ModalOpen, Easing.Out, opacity => Opacity = opacity);
        MotionOverlay.Play(_card, Durations.ModalOpen, Easing.Out, Recipes.ModalOpen, Theme.Card);
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        PlayOpen();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape)
        {
            Choose(false);
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!_closing && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            Choose(false);
        }
        base.OnFormClosing(e);
    }

    private void Choose(bool confirmed)
    {
        if (_closing) return;
        _closing = true;
        Confirmed = confirmed;
        Motion.Animate(this, nameof(Opacity), Opacity, 0, Durations.ModalClose, Easing.Out, opacity => Opacity = opacity, Close);
        MotionOverlay.Play(_card, Durations.ModalClose, Easing.Out, Recipes.ModalClose, Theme.Card);
    }

    /// <summary>The card: question text and the 1 px line border, and the parent of the two buttons.</summary>
    private sealed class CardPanel : Panel
    {
        private readonly string _question;

        public CardPanel(string question)
        {
            _question = question;
            BackColor = Theme.Card;
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            Draw.Wrapped(e.Graphics, _question, Theme.Strong, Theme.Text,
                new Rectangle(DialogPad, DialogPad, Width - 2 * DialogPad, Height - 2 * DialogPad - Theme.ControlHeight));
            Draw.Border(e.Graphics, ClientRectangle, Theme.Line);
        }
    }
}
