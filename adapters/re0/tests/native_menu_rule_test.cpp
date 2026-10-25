// Checks the native menu rules: the cursor's steps over rows that cannot be selected, the one event a frame of input
// gives, which events reach a row's action, and the text a row object gets (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/native_menu_rule.h"

namespace {

using namespace native_menu_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// info, choice, info, action, choice
constexpr MenuDef kMenu{"test",
                        "test.menu",
                        {{{"a", RowKind::Info, ""},
                          {"b", RowKind::Choice, "test.b"},
                          {"c", RowKind::Info, ""},
                          {"d", RowKind::Action, "test.d"},
                          {"e", RowKind::Choice, "test.e"}}},
                        5};

PageView plainView() {
    PageView view;
    for (RowView& row : view.rows) row = {"label", "value", true, false};
    return view;
}

void testCursor() {
    PageView view = plainView();
    check(firstCursor(kMenu, view) == 1, "the cursor starts on the first selectable row");
    check(stepCursor(kMenu, view, 1, 1) == 3, "down skips an info row");
    check(stepCursor(kMenu, view, 4, 1) == 1, "down from the last row wraps over the info row");
    check(stepCursor(kMenu, view, 1, -1) == 4, "up from the first wraps to the last");
    view.rows[3].visible = false;
    check(stepCursor(kMenu, view, 1, 1) == 4, "a hidden row is skipped");
    check(keepCursor(kMenu, view, 3) == 4, "a cursor on a row that went hidden moves on");
    check(keepCursor(kMenu, view, 1) == 1, "a cursor on a row still there stays");
    view.rows[1].locked = true;
    check(stepCursor(kMenu, view, 4, 1) == 1, "a locked row can still be selected");
}

void testDecode() {
    const PageView view = plainView();
    check(!decode(kMenu, view, 1, Input{}), "no input, no event");
    Input both;
    both.cancel = true;
    both.decide = true;
    check(decode(kMenu, view, 1, both)->kind == EventKind::Cancel, "cancel wins over decide (the vanilla order)");
    Input decide;
    decide.decide = true;
    decide.down = true;
    const auto confirm = decode(kMenu, view, 3, decide);
    check(confirm->kind == EventKind::Confirm && confirm->row == 3, "decide confirms the cursor row before a move");
    Input down;
    down.down = true;
    const auto select = decode(kMenu, view, 1, down);
    check(select->kind == EventKind::Select && select->row == 3, "down selects the next selectable row");
    Input left;
    left.left = true;
    const auto change = decode(kMenu, view, 4, left);
    check(change->kind == EventKind::Change && change->row == 4 && change->step == -1, "left changes the cursor row down one");
    Input arrow;
    arrow.clickedArrow = 1;
    check(decode(kMenu, view, 1, arrow)->step == 1, "a click on the right arrow changes up one");
    Input click;
    click.clickedRow = 3;
    check(decode(kMenu, view, 1, click)->kind == EventKind::Select, "a click on another row selects it");
    check(decode(kMenu, view, 3, click)->kind == EventKind::Confirm, "a click on the cursor row confirms it");
    click.clickedRow = 2;
    check(!decode(kMenu, view, 1, click), "a click on an info row does nothing");
}

void testAllowed() {
    PageView view = plainView();
    check(allowed(kMenu, view, {EventKind::Change, 1, 1}), "a choice changes");
    check(!allowed(kMenu, view, {EventKind::Change, 3, 1}), "an action does not change");
    check(!allowed(kMenu, view, {EventKind::Confirm, 1}), "a choice does not confirm");
    check(allowed(kMenu, view, {EventKind::Confirm, 3}), "an action confirms");
    check(!allowed(kMenu, view, {EventKind::Change, 2, 1}), "an info row never acts");
    check(!allowed(kMenu, view, {EventKind::Change, 7, 1}), "a row past the menu never acts");
    // The guard of a host-only row on a guest: shown, but its change is refused before any handler runs.
    view.rows[1].locked = true;
    check(!allowed(kMenu, view, {EventKind::Change, 1, 1}), "a locked choice does not change");
    check(!arrowsShown(kMenu, view, 1), "a locked choice shows no arrows");
    check(arrowsShown(kMenu, view, 4) && !arrowsShown(kMenu, view, 3), "arrows only on a choice that may change");
    check(allowed(kMenu, view, {EventKind::Cancel}) && allowed(kMenu, view, {EventKind::Opened}), "menu events always reach the menu");
}

void testActionOf() {
    check(actionOf(kMenu, {EventKind::Change, 1, 1}) == "test.b", "a change goes to its row's action");
    check(actionOf(kMenu, {EventKind::Confirm, 3}) == "test.d", "a confirm goes to its row's action");
    check(actionOf(kMenu, {EventKind::Cancel}) == "test.menu" && actionOf(kMenu, {EventKind::Closed}) == "test.menu",
          "menu events go to the menu's action");
    check(actionOf(kMenu, {EventKind::Select, 3}).empty(), "a select is the page's own");
}

void testSanitized() {
    check(sanitized("Billy") == "Billy", "plain text stays");
    check(sanitized("Pr\xc3\xa9" "cis\t") == "Pr??cis?", "bytes outside the printable range become ?");
    check(sanitized(std::string(80, 'x')).size() == kMaxTextBytes, "long text is cut to the box");
}

}  // namespace

int main() {
    testCursor();
    testDecode();
    testAllowed();
    testActionOf();
    testSanitized();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
