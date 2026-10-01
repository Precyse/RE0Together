#include "command_input.h"

#include <windows.h>

#include "camera_parity.h"
#include "character_owner.h"
#include "command_log.h"
#include "debug_stats.h"
#include "game.h"
#include "key_config.h"
#include "net_pad.h"
#include "party_mode.h"
#include "virtual_keys.h"
#include "window_focus.h"

namespace {

constexpr int kKeyDownMask = 0x8000;
constexpr UINT kExtendedScanPrefix = 0xE000;
constexpr uint8_t kDikExtendedBit = 0x80;
constexpr UINT kScanCodeMask = 0xFF;

key_config::CommandKeys g_keys{};
bool g_changeWasDown = false;  // net thread only
bool g_traceWasDown = false;
uint8_t g_changeScancode = 0;

// DirectInput key code of a virtual key: its scan code, with the high bit for extended keys (arrows).
uint8_t dikOf(int virtualKey) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
    const bool extended = (scan & ~kScanCodeMask) == kExtendedScanPrefix;
    return static_cast<uint8_t>((scan & kScanCodeMask) | (extended ? kDikExtendedBit : 0));
}

// True on the poll where the key goes down. The edge state follows the raw key on every poll, so it cannot stick.
bool pressedEdge(int virtualKey, bool& wasDown) {
    const bool down = (GetAsyncKeyState(virtualKey) & kKeyDownMask) != 0;
    const bool edge = down && !wasDown;
    wasDown = down;
    return edge;
}

void publishFocus() {
    const auto focused = character_owner::identify(game::controlled());
    debug_stats::set(debug_stats::Gauge::FocusedCharacter,
                     focused == character_owner::Character::Unknown ? debug_stats::kUnknownOwner
                                                                    : static_cast<int>(focused));
}

// Logs the press; true when it should become a request (game window in front and a peer connected).
bool accept(const char* name, int virtualKey, bool foreground) {
    command_log::press(name, virtualKey, foreground);
    if (!foreground) {
        command_log::decide("%s ignored: window not foreground", name);
        return false;
    }
    if (!net_pad::active()) {
        command_log::decide("%s ignored: no peer connected", name);
        return false;
    }
    debug_stats::count(debug_stats::Counter::CommandsSent);
    return true;
}

}  // namespace

namespace command_input {

void enable() {
    g_keys = key_config::load();
    g_changeScancode = dikOf(g_keys.change);
}

void onNetTick() {
    publishFocus();
    // With a peer the adapter owns switching: the game's own switch would zap to the other player's character.
    virtual_keys::setMutedKey(net_pad::active() ? g_changeScancode : 0);
    const bool foreground = gameIsForeground();
    const bool change = pressedEdge(g_keys.change, g_changeWasDown);
    const bool trace = pressedEdge(g_keys.trace, g_traceWasDown);
    if (change && accept("switch", g_keys.change, foreground)) camera_parity::onLocalSwitchKey();
    if (trace && accept("party", g_keys.trace, foreground)) party_mode::onLocalToggleKey();
}

}  // namespace command_input
