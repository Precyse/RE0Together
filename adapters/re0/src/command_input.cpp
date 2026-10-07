#include "command_input.h"

#include <windows.h>

#include "character_owner.h"
#include "command_log.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "key_config.h"
#include "net_pad.h"
#include "pad_commands.h"
#include "party_mode.h"
#include "resync.h"
#include "virtual_keys.h"

namespace {

constexpr UINT kExtendedScanPrefix = 0xE000;
constexpr uint8_t kDikExtendedBit = 0x80;
constexpr UINT kScanCodeMask = 0xFF;
constexpr int kResyncKey = VK_F9;  // the game binds no function keys, so it is not hidden from it

key_config::CommandKeys g_keys{};
bool g_traceWasDown = false;  // net thread only: keyboard or controller held at the last poll
bool g_resyncWasDown = false;  // net thread only
virtual_keys::HiddenKeys g_commandScancodes{};  // the switch (hidden only) and partner keys as DirectInput codes

// DirectInput key code of a virtual key: its scan code, with the high bit for extended keys (arrows).
uint8_t dikOf(int virtualKey) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
    const bool extended = (scan & ~kScanCodeMask) == kExtendedScanPrefix;
    return static_cast<uint8_t>((scan & kScanCodeMask) | (extended ? kDikExtendedBit : 0));
}

// As the game's own keyboard read last saw it: the game opens its keyboard foreground-only, so a key typed into
// another window never counts.
bool keyDown(int virtualKey) { return virtual_keys::realKeyDown(dikOf(virtualKey)); }

// True on the poll where the command goes down (key or controller button). The edge state follows the raw input on
// every poll, so it cannot stick.
bool pressedEdge(bool down, bool& wasDown) {
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

// With a peer, in gameplay, the adapter owns both commands: the game's own switch would take this machine's camera,
// and its partner command would act on the other player's character. In menus and other screens the keys and buttons
// keep the game's own meaning.
bool commandsOwned() { return net_pad::active() && game_state::playing(); }

// Logs the press; true when it should become a request (a peer connected, in gameplay).
bool accept(const char* name, const char* source, bool owned) {
    command_log::press(name, source);
    if (!net_pad::active()) {
        command_log::decide("%s ignored: no peer connected", name);
        return false;
    }
    if (!owned) {
        command_log::decide("%s ignored: not in gameplay", name);
        return false;
    }
    debug_stats::count(debug_stats::Counter::CommandsSent);
    return true;
}

}  // namespace

namespace command_input {

void enable() {
    g_keys = key_config::load();
    g_commandScancodes = {dikOf(g_keys.change), dikOf(g_keys.trace)};
}

void onNetTick() {
    publishFocus();
    const bool owned = commandsOwned();
    virtual_keys::setHiddenKeys(owned ? g_commandScancodes : virtual_keys::HiddenKeys{});
    pad_commands::setHidden(owned);
    const bool traceKey = keyDown(g_keys.trace);
    const bool trace = pressedEdge(traceKey || pad_commands::pressed().trace, g_traceWasDown);
    if (trace && accept("party", traceKey ? "keyboard" : "controller", owned)) party_mode::onLocalToggleKey();
    const bool resyncKey = pressedEdge(keyDown(kResyncKey), g_resyncWasDown);
    if (resyncKey && accept("resync", "keyboard", owned)) resync::request("resync key");
}

}  // namespace command_input
