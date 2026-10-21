#pragma once
#include <array>
#include <cstdint>

// Pure rules of the Co-op entry in the Options top menu (options_coop.cpp, unit tested): where the item goes in the
// game's list, when choosing it opens our page, and what closes the page. The game's Options screen is two pieces: the
// top menu (uGUIOptionTop) and a router (uOptionManager) that, once the top has closed after a choice, switches on the
// chosen item id to open that item's page. Item id 6 is never put in the list on PC and has no case in the router.
namespace options_coop_rule {

constexpr uint32_t kCoopItem = 6;
constexpr uint32_t kBackItem = 9;
constexpr size_t kItemCapacity = 10;  // the top menu's item array

// Router states (uOptionManager +0x54). 0 is closed, 1..0x16 are the game's own.
constexpr uint32_t kStateReopenTop = 2;  // shows the top menu again, then waits in 3
constexpr uint32_t kStateTopWait = 3;    // waits for the top to close, then switches on the chosen item
constexpr uint32_t kStateParked = 0x7f;  // above 0x16: the router's dispatch falls to its default case and does nothing
constexpr uint32_t kTopClosed = 0;       // uGUIOptionTop state while it is closed

using Items = std::array<uint32_t, kItemCapacity>;

// Puts Co-op before the trailing Back and returns the new count. The list is left alone when it already holds Co-op,
// is full, or does not end with Back.
constexpr uint32_t withCoopItem(Items& items, uint32_t count) {
    if (count == 0 || count >= kItemCapacity || items[count - 1] != kBackItem) return count;
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i] == kCoopItem) return count;
    }
    items[count] = kBackItem;
    items[count - 1] = kCoopItem;
    return count + 1;
}

// The item the top menu's choice selects: Back after a cancel, else the item under the cursor (scroll offset plus
// cursor row, as the game computes it). -1 when the position is outside the list.
constexpr int64_t selectedItem(bool cancelled, const Items& items, uint32_t count, uint32_t scroll, uint32_t cursor) {
    if (cancelled) return kBackItem;
    if (count > kItemCapacity || scroll >= count || cursor >= count - scroll) return -1;
    return items[scroll + cursor];
}

struct RouterView {
    uint32_t state;
    bool creating;     // the pages are still being built
    bool dialog;       // a confirm dialog is up
    uint32_t topState;
    int64_t selected;  // selectedItem
};

// True on the frame the router would switch on Co-op, the item it cannot handle.
constexpr bool opensCoopPage(const RouterView& view) {
    return view.state == kStateTopWait && !view.creating && !view.dialog && view.topState == kTopClosed &&
           view.selected == kCoopItem;
}

// True while the router is where we parked it: any other state means the game moved on (the screen was closed or
// rebuilt) and the page must go without touching the router.
constexpr bool stillParked(uint32_t routerState) { return routerState == kStateParked; }

// The page closes on confirm (its one row is Back) or cancel; `trigger` is the menu trigger word, the masks are the
// pad's confirm and cancel bits.
constexpr bool closesPage(uint32_t trigger, uint32_t confirmMask, uint32_t cancelMask) {
    return (trigger & (confirmMask | cancelMask)) != 0;
}

}  // namespace options_coop_rule
