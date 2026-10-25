#include "native_menu.h"

#include <windows.h>

#include <string>
#include <vector>

#include "crash_dump.h"
#include "log.h"

namespace {

using native_menu::Handler;
using native_menu::ViewSource;
using native_menu_rule::Reply;

struct Action {
    std::string id;
    Handler handler;
    bool disabled = false;  // the handler faulted once
};

struct Menu {
    const native_menu_rule::MenuDef* def;
    ViewSource view;
};

std::vector<Action> g_actions;  // filled at start, read on the game thread
std::vector<Menu> g_menus;
std::vector<std::string> g_unknownLogged;  // game thread
std::vector<std::function<void()>> g_afterClose;  // game thread

Action* actionNamed(std::string_view id) {
    for (Action& action : g_actions) {
        if (action.id == id) return &action;
    }
    return nullptr;
}

void logUnknownOnce(std::string_view id) {
    for (const std::string& logged : g_unknownLogged) {
        if (logged == id) return;
    }
    g_unknownLogged.emplace_back(id);
    logger::write("native_menu: no action registered as \"%.*s\"", static_cast<int>(id.size()), id.data());
}

bool guardedCall(Handler handler, const native_menu_rule::Event& event, Reply& reply) {
    __try {
        reply = handler(event);
        return true;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "native_menu: action")) {
        return false;
    }
}

}  // namespace

namespace native_menu {

void registerAction(std::string_view id, Handler handler) { g_actions.push_back({std::string(id), handler}); }

void registerMenu(const MenuDef& menu, ViewSource view) { g_menus.push_back({&menu, view}); }

const MenuDef* find(std::string_view id) {
    for (const Menu& menu : g_menus) {
        if (menu.def->id == id) return menu.def;
    }
    return nullptr;
}

PageView view(const MenuDef& menu) {
    for (const Menu& entry : g_menus) {
        if (entry.def != &menu) continue;
        PageView page = entry.view();
        page.header = native_menu_rule::sanitized(page.header);
        for (native_menu_rule::RowView& row : page.rows) {
            row.label = native_menu_rule::sanitized(row.label);
            row.value = native_menu_rule::sanitized(row.value);
        }
        return page;
    }
    return {};
}

Reply dispatch(const MenuDef& menu, const PageView& view, const Event& event) {
    if (!native_menu_rule::allowed(menu, view, event)) return Reply::Reject;
    const std::string_view id = native_menu_rule::actionOf(menu, event);
    if (id.empty()) return Reply::Stay;
    Action* action = actionNamed(id);
    if (!action) {
        logUnknownOnce(id);
        return Reply::Reject;
    }
    if (action->disabled) return Reply::Reject;
    Reply reply = Reply::Stay;
    if (guardedCall(action->handler, event, reply)) return reply;
    action->disabled = true;
    logger::write("native_menu: action \"%s\" faulted, off for this session", action->id.c_str());
    return Reply::Reject;
}

void afterClose(std::function<void()> work) { g_afterClose.push_back(std::move(work)); }

void runAfterClose() {
    std::vector<std::function<void()>> work;
    work.swap(g_afterClose);
    for (const auto& step : work) step();
}

void dropAfterClose() { g_afterClose.clear(); }

}  // namespace native_menu
