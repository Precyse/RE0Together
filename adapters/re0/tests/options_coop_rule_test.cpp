// Checks the Co-op entry rules of the Options screen: the item list, when the page opens and what closes it (no
// game). Exit 0 when every check passes.
#include <cstdio>
#include <initializer_list>

#include "../src/options_coop_rule.h"

namespace {

using namespace options_coop_rule;

constexpr uint32_t kConfirm = 0x1000;
constexpr uint32_t kCancel = 0x2000;
constexpr uint32_t kUp = 0x10;
constexpr uint32_t kTopOpen = 3;
constexpr uint32_t kRouterLastOwnState = 0x16;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Items listOf(std::initializer_list<uint32_t> ids) {
    Items items{};
    size_t i = 0;
    for (const uint32_t id : ids) items[i++] = id;
    return items;
}

void testInsert() {
    Items inGame = listOf({0, 1, 2, 3, 5, 7, 8, 9});
    check(withCoopItem(inGame, 8) == 9, "in game: one item more");
    check(inGame == listOf({0, 1, 2, 3, 5, 7, 8, 6, 9}), "in game: Co-op before Back, the rest in order");

    Items title = listOf({0, 1, 2, 3, 4, 5, 9});
    check(withCoopItem(title, 7) == 8, "title: one item more");
    check(title == listOf({0, 1, 2, 3, 4, 5, 6, 9}), "title: Co-op before Back");

    Items again = inGame;
    check(withCoopItem(again, 9) == 9 && again == inGame, "a list that holds Co-op is left alone");

    Items full = listOf({0, 1, 2, 3, 4, 5, 7, 8, 8, 9});
    const Items fullBefore = full;
    check(withCoopItem(full, kItemCapacity) == kItemCapacity && full == fullBefore, "a full list is left alone");

    Items noBack = listOf({0, 1, 2});
    check(withCoopItem(noBack, 3) == 3 && noBack == listOf({0, 1, 2}), "a list without Back is left alone");

    Items empty{};
    check(withCoopItem(empty, 0) == 0, "an empty list is left alone");
}

void testSelected() {
    const Items items = listOf({0, 1, 2, 3, 5, 7, 8, 6, 9});
    check(selectedItem(false, items, 9, 0, 0) == 0, "first row");
    check(selectedItem(false, items, 9, 0, 5) == 7, "sixth row");
    check(selectedItem(false, items, 9, 3, 4) == kCoopItem, "scrolled by three, fifth row is Co-op");
    check(selectedItem(false, items, 9, 3, 5) == kBackItem, "scrolled to the end, last row is Back");
    check(selectedItem(true, items, 9, 3, 4) == kBackItem, "a cancel selects Back whatever the cursor");
    check(selectedItem(false, items, 9, 4, 5) == -1, "past the count selects nothing");
    check(selectedItem(false, items, 9, 0xffffffffu, 5) == -1, "a wild scroll selects nothing");
}

RouterView coopChosen() { return {kStateTopWait, false, false, kTopClosed, kCoopItem}; }

void testOpens() {
    check(opensCoopPage(coopChosen()), "Co-op chosen, top closed, router waiting: the page opens");
    RouterView view = coopChosen();
    view.selected = 0;
    check(!opensCoopPage(view), "another item does not");
    view = coopChosen();
    view.topState = kTopOpen;
    check(!opensCoopPage(view), "the top still open does not");
    view = coopChosen();
    view.state = kStateReopenTop;
    check(!opensCoopPage(view), "another router state does not");
    view = coopChosen();
    view.creating = true;
    check(!opensCoopPage(view), "pages still being built does not");
    view = coopChosen();
    view.dialog = true;
    check(!opensCoopPage(view), "a dialog up does not");
    view = coopChosen();
    view.state = kStateParked;
    check(!opensCoopPage(view), "a parked router does not open a second page");
}

void testParking() {
    check(stillParked(kStateParked), "the park state is ours");
    check(!stillParked(0), "a closed router is not ours");
    check(!stillParked(kStateTopWait), "the router moved on");
    check(kStateParked > kRouterLastOwnState, "the park state is outside the router's own 1..0x16");
}

void testCloses() {
    check(closesPage(kConfirm, kConfirm, kCancel), "confirm closes");
    check(closesPage(kCancel | kUp, kConfirm, kCancel), "cancel with another bit closes");
    check(!closesPage(kUp, kConfirm, kCancel), "a direction does not");
    check(!closesPage(0, kConfirm, kCancel), "no input does not");
    check(!closesPage(0xffff, 0, 0), "unreadable masks never close");
}

}  // namespace

int main() {
    testInsert();
    testSelected();
    testOpens();
    testParking();
    testCloses();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
