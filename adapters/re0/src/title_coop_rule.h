#pragma once
#include <cstdint>

// Pure rules of the Co-op entry on the title screen's main menu (title_coop.cpp, unit tested). The title (uGUITitle)
// runs one state machine on +0x27c; its main menu has a fifth row the PC build never shows (+0x27a, the console
// "Online Store" row, whose choice sets state 0x2b and opens the Steam store overlay). The entry is that row: shown,
// relabelled (title_label.h), and its choice parked in a state the title's dispatch ignores while our page is open.
namespace title_coop_rule {

// uGUITitle states (+0x27c); the dispatch handles 0..0x3a and returns on anything above.
constexpr uint32_t kStateMainMenuOpen = 0x1f;  // highlights the cursor row, then waits for input in 0x20
constexpr uint32_t kStateStoreChosen = 0x2b;   // the fifth row was chosen: the game would open the store overlay
constexpr uint32_t kLastGameState = 0x3a;
constexpr uint32_t kStateParked = 0x7f;  // the dispatch does nothing: no input read, no store, no game start
static_assert(kStateParked > kLastGameState);

constexpr uint8_t kFiveRows = 1;  // +0x27a: the main menu shows its fifth row

enum class Step {
    None,       // the title runs as it would
    OpenPage,   // park the title and open the page
    KeepPage,   // the page stays open, the title stays parked
    ClosePage,  // close the page and reopen the main menu
    DropPage,   // the title left the parked state by itself: close the page without touching the title
};

// What one frame of the title's move does with the page. `closeInput` is the page's confirm or cancel this frame.
constexpr Step step(uint32_t titleState, bool pageOpen, bool closeInput) {
    if (pageOpen) {
        if (titleState != kStateParked) return Step::DropPage;
        return closeInput ? Step::ClosePage : Step::KeepPage;
    }
    return titleState == kStateStoreChosen ? Step::OpenPage : Step::None;
}

// The page closes on confirm (its one row is Back) or cancel; `trigger` is the menu trigger word, the masks are the
// pad's confirm and cancel bits.
constexpr bool closesPage(uint32_t trigger, uint32_t confirmMask, uint32_t cancelMask) {
    return (trigger & (confirmMask | cancelMask)) != 0;
}

}  // namespace title_coop_rule
