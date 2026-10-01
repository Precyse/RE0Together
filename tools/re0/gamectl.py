"""Drive the RE0 window for testing: focus it, press keys (scan codes, DirectInput-safe), screenshot it.

usage: python gamectl.py shot [out.png]
       python gamectl.py key <name> [hold_ms] [repeat]   e.g. key enter / key up 50 3 / key w 1500
       python gamectl.py keys <name,name,...>             tap each in order
       python gamectl.py loadsave <slot> [--from-logo]    from the title menu (or the boot logo), load a save slot and continue
       python gamectl.py idle                             seconds since the user last touched the mouse or keyboard
Keys are only sent while RE0 is the foreground window; if focus cannot be taken, nothing is sent.
Menus: Enter confirms, Space cancels, Esc opens pause (Quit Game lives there).
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

WINDOW_TITLE_PART = "RESIDENT EVIL 0"
DEFAULT_SHOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shot.png")
TAP_MS = 60
GAP_MS = 150
FOCUS_SETTLE_MS = 250
KEYEVENTF_SCANCODE = 0x0008
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_EXTENDEDKEY = 0x0001
INPUT_KEYBOARD = 1
EXTENDED = 0xE000
SCANCODES = {
    "esc": 0x01, "enter": 0x1C, "space": 0x39, "tab": 0x0F, "backspace": 0x0E,
    "lshift": 0x2A, "lctrl": 0x1D, "lalt": 0x38,
    "w": 0x11, "a": 0x1E, "s": 0x1F, "d": 0x20, "q": 0x10, "e": 0x12, "r": 0x13, "f": 0x21, "v": 0x2F,
    "i": 0x17, "j": 0x24, "k": 0x25, "l": 0x26, "m": 0x32, "x": 0x2D, "c": 0x2E, "z": 0x2C,
    "1": 0x02, "2": 0x03, "3": 0x04, "4": 0x05, "f7": 0x41,
    "up": EXTENDED | 0x48, "down": EXTENDED | 0x50, "left": EXTENDED | 0x4B, "right": EXTENDED | 0x4D,
}

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDPIAware()


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def find_window():
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if WINDOW_TITLE_PART.lower() in buf.value.lower() and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(cb, 0)
    if not found:
        sys.exit("RE0 window not found")
    return found[0]


def focus(hwnd):
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.keybd_event(0x12, 0, 0, 0)  # an Alt tap lets SetForegroundWindow succeed from a background process
    user32.keybd_event(0x12, 0, KEYEVENTF_KEYUP, 0)
    user32.SetForegroundWindow(hwnd)
    time.sleep(FOCUS_SETTLE_MS / 1000)


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.UINT), ("dwTime", wt.DWORD)]


def idle_seconds():
    info = LASTINPUTINFO(cbSize=ctypes.sizeof(LASTINPUTINFO))
    user32.GetLastInputInfo(ctypes.byref(info))
    return (ctypes.windll.kernel32.GetTickCount() - info.dwTime) / 1000


def ensure_foreground(hwnd):
    """Input must only ever reach the game: refocus once, then refuse."""
    if user32.GetForegroundWindow() == hwnd:
        return
    focus(hwnd)
    if user32.GetForegroundWindow() != hwnd:
        sys.exit("RE0 is not the foreground window; no input sent")


def send_scan(code, up):
    flags = KEYEVENTF_SCANCODE | (KEYEVENTF_KEYUP if up else 0) | (KEYEVENTF_EXTENDEDKEY if code & EXTENDED else 0)
    inp = INPUT(type=INPUT_KEYBOARD)
    inp.ki = KEYBDINPUT(0, code & 0xFF, flags, 0, 0)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def press(hwnd, name, hold_ms=TAP_MS):
    ensure_foreground(hwnd)
    code = SCANCODES[name]
    send_scan(code, False)
    time.sleep(hold_ms / 1000)
    send_scan(code, True)
    time.sleep(GAP_MS / 1000)


def shot(hwnd, path):
    from PIL import ImageGrab
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).save(path)
    print(path)


# Each step is (key, seconds to wait after).
BOOT_STEPS = [("enter", 4), ("enter", 5), ("enter", 4)]  # title logo -> data notice -> load notice -> title menu
LOAD_MENU_STEPS = [("enter", 9)]                          # title menu (Load Game highlighted) -> save list
CONFIRM_STEPS = [("enter", 14), ("enter", 14)]             # load slot -> Continue


def run_steps(hwnd, steps):
    for key, wait in steps:
        press(hwnd, key)
        time.sleep(wait)


def load_save(hwnd, slot, from_logo):
    focus(hwnd)
    run_steps(hwnd, (BOOT_STEPS if from_logo else []) + LOAD_MENU_STEPS + [("down", 1)] * (slot - 1) + CONFIRM_STEPS)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "idle":
        print(f"{idle_seconds():.0f}")
        return
    hwnd = find_window()
    if cmd == "shot":
        focus(hwnd)
        shot(hwnd, sys.argv[2] if len(sys.argv) > 2 else DEFAULT_SHOT)
    elif cmd == "key":
        focus(hwnd)
        hold = int(sys.argv[3]) if len(sys.argv) > 3 else TAP_MS
        for _ in range(int(sys.argv[4]) if len(sys.argv) > 4 else 1):
            press(hwnd, sys.argv[2], hold)
    elif cmd == "loadsave":
        load_save(hwnd, int(sys.argv[2]) if len(sys.argv) > 2 else 1, "--from-logo" in sys.argv)
    elif cmd == "keys":
        focus(hwnd)
        for name in sys.argv[2].split(","):
            press(hwnd, name)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
