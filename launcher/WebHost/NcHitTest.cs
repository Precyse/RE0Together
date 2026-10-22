namespace CoopLauncher.Gui;

/// <summary>WM_NCHITTEST: the message, the answers this window uses, and the screen point it asks about.</summary>
internal static class NcHitTest
{
    public const int WindowMessage = 0x0084;
    public const int Transparent = -1;
    public const int Client = 1;
    public const int Caption = 2;
    public const int Top = 12;
    public const int TopLeft = 13;
    public const int TopRight = 14;

    /// <summary>The screen point packed in the message's lParam (signed: screens left of or above the primary).</summary>
    public static Point ScreenPoint(Message m)
    {
        var value = m.LParam.ToInt64();
        return new Point((short)(value & 0xFFFF), (short)((value >> 16) & 0xFFFF));
    }
}
