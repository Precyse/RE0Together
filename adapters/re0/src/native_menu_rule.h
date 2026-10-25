#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Pure rules of the data-driven native menus (native_menu.cpp, native_page.cpp; unit tested): a menu is a table of rows,
// each row names the registered action that handles it; the page turns the game's menu input into one event per frame
// and the rules here say which event it is, where the cursor goes and which rows may act. docs/RE0_NOTES.md "Menus:
// the native Co-op page".
namespace native_menu_rule {

constexpr size_t kMaxRows = 8;        // the PC Setup layout's rows
constexpr size_t kMaxTextBytes = 47;  // a row's label or value; longer text is cut (the value box is fixed)
constexpr char kFirstPrintable = ' ';
constexpr char kLastPrintable = '~';
constexpr char kSubstitute = '?';

enum class RowKind : uint8_t {
    Info,    // text only, never under the cursor
    Choice,  // left / right change its value
    Action,  // confirm runs it
    Link,    // confirm opens the menu named by its action (no handler)
};

// A row of a menu definition: its id and the id of the registered action that receives its events (empty: none); a Link
// row's action is the id of the menu it opens.
struct RowDef {
    std::string_view id;
    RowKind kind;
    std::string_view action;
};

struct MenuDef {
    std::string_view id;
    std::string_view action;  // receives Opened, Cancel and Closed
    std::array<RowDef, kMaxRows> rows;
    size_t count;
    std::string_view parent = {};  // the menu cancel returns to (empty: cancel closes the page)
};

// What a row shows this frame, from the menu's view source.
struct RowView {
    std::string label;
    std::string value;
    bool visible = true;
    bool locked = false;  // shown, but its change or confirm is refused (a host-only row on a guest)

    bool operator==(const RowView&) const = default;
};

struct PageView {
    std::string header;
    std::array<RowView, kMaxRows> rows;

    bool operator==(const PageView&) const = default;
};

enum class EventKind : uint8_t { Opened, Select, Change, Confirm, Cancel, Closed };

struct Event {
    EventKind kind;
    int row = -1;  // Select, Change, Confirm
    int step = 0;  // Change: -1 left, +1 right
};

enum class Reply : uint8_t {
    Stay,    // nothing more
    Close,   // the page fades out and closes
    Reject,  // the buzzer: the event did nothing
};

// One frame of menu input, decoded the way the PC Setup page reads it (0x5c6540): cancel, decide, up, down, left, right,
// then the mouse. `clickedRow` is the row whose touch polygon a left click hit (-1 none); `clickedArrow` -1 / +1 for the
// left / right arrow.
struct Input {
    bool cancel = false;
    bool decide = false;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    int clickedRow = -1;
    int clickedArrow = 0;
};

constexpr bool rowExists(const MenuDef& menu, int row) { return row >= 0 && static_cast<size_t>(row) < menu.count; }

inline bool selectable(const MenuDef& menu, const PageView& view, int row) {
    return rowExists(menu, row) && menu.rows[static_cast<size_t>(row)].kind != RowKind::Info &&
           view.rows[static_cast<size_t>(row)].visible;
}

// The cursor's next row in `direction` (-1 up, +1 down), wrapping and skipping rows that cannot be selected; the same row
// when no other row can.
inline int stepCursor(const MenuDef& menu, const PageView& view, int cursor, int direction) {
    const int count = static_cast<int>(menu.count);
    if (count == 0) return cursor;
    for (int i = 1; i <= count; ++i) {
        const int row = ((cursor + direction * i) % count + count) % count;
        if (selectable(menu, view, row)) return row;
    }
    return cursor;
}

// Where the cursor starts: the first selectable row, 0 when there is none.
inline int firstCursor(const MenuDef& menu, const PageView& view) {
    for (int row = 0; static_cast<size_t>(row) < menu.count; ++row) {
        if (selectable(menu, view, row)) return row;
    }
    return 0;
}

// The cursor after a view change: the same row while it can still be selected, else the next one down.
inline int keepCursor(const MenuDef& menu, const PageView& view, int cursor) {
    return selectable(menu, view, cursor) ? cursor : stepCursor(menu, view, cursor, 1);
}

// The one event of this frame, in the vanilla order (cancel, decide, up, down, left, right, mouse); none when nothing
// was pressed. A click on another row selects it, a click on the cursor row confirms it.
inline std::optional<Event> decode(const MenuDef& menu, const PageView& view, int cursor, const Input& input) {
    if (input.cancel) return Event{EventKind::Cancel};
    if (input.decide) return Event{EventKind::Confirm, cursor};
    if (input.up) return Event{EventKind::Select, stepCursor(menu, view, cursor, -1)};
    if (input.down) return Event{EventKind::Select, stepCursor(menu, view, cursor, 1)};
    if (input.left) return Event{EventKind::Change, cursor, -1};
    if (input.right) return Event{EventKind::Change, cursor, 1};
    if (input.clickedArrow != 0) return Event{EventKind::Change, cursor, input.clickedArrow};
    if (selectable(menu, view, input.clickedRow)) {
        return Event{input.clickedRow == cursor ? EventKind::Confirm : EventKind::Select, input.clickedRow};
    }
    return std::nullopt;
}

// Whether an event may reach the row's action: a change only on an unlocked choice, a confirm only on an unlocked
// action, a select only to a selectable row. Menu events (Opened, Cancel, Closed) always may.
inline bool allowed(const MenuDef& menu, const PageView& view, const Event& event) {
    switch (event.kind) {
        case EventKind::Opened:
        case EventKind::Cancel:
        case EventKind::Closed: return true;
        case EventKind::Select: return selectable(menu, view, event.row);
        case EventKind::Change:
        case EventKind::Confirm: {
            if (!selectable(menu, view, event.row)) return false;
            const RowKind kind = menu.rows[static_cast<size_t>(event.row)].kind;
            const bool fits = event.kind == EventKind::Change ? kind == RowKind::Choice : kind == RowKind::Action || kind == RowKind::Link;
            return fits && !view.rows[static_cast<size_t>(event.row)].locked;
        }
    }
    return false;
}

// The menu an event leads to, or empty: a confirm on a Link row opens the menu it names, a cancel on a menu with a parent
// returns to it. Such an event is the page's own: no action receives it.
inline std::string_view navigation(const MenuDef& menu, const PageView& view, const Event& event) {
    if (event.kind == EventKind::Cancel) return menu.parent;
    if (event.kind != EventKind::Confirm || !allowed(menu, view, event)) return {};
    const RowDef& row = menu.rows[static_cast<size_t>(event.row)];
    return row.kind == RowKind::Link ? row.action : std::string_view{};
}

// The row of `menu` that links to the menu `target`, or -1: the cursor's place when that menu is left for this one.
inline int linkRow(const MenuDef& menu, std::string_view target) {
    for (size_t row = 0; row < menu.count; ++row) {
        if (menu.rows[row].kind == RowKind::Link && menu.rows[row].action == target) return static_cast<int>(row);
    }
    return -1;
}

// The action that receives an event: the row's for Change and Confirm, the menu's for Opened, Cancel and Closed, none
// for Select (the page moves the cursor itself) and none for a Link row.
inline std::string_view actionOf(const MenuDef& menu, const Event& event) {
    switch (event.kind) {
        case EventKind::Change:
        case EventKind::Confirm: {
            if (!rowExists(menu, event.row)) return "";
            const RowDef& row = menu.rows[static_cast<size_t>(event.row)];
            return row.kind == RowKind::Link ? std::string_view{} : row.action;
        }
        case EventKind::Select: return "";
        default: return menu.action;
    }
}

// The arrows are drawn on the cursor row when its value can change.
inline bool arrowsShown(const MenuDef& menu, const PageView& view, int cursor) {
    return allowed(menu, view, Event{EventKind::Change, cursor, 1});
}

// The text a row object gets: printable ASCII only (the game font of every language has it), cut to the box.
inline std::string sanitized(std::string_view text) {
    std::string out;
    for (const char c : text.substr(0, kMaxTextBytes)) out.push_back(c >= kFirstPrintable && c <= kLastPrintable ? c : kSubstitute);
    return out;
}

}  // namespace native_menu_rule
