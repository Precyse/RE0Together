#include "browser_tag.h"

#include "force_join_names.h"
#include "force_join_sites.h"
#include "game_text.h"
#include "log.h"

namespace {

using namespace force_join_sites;

std::string g_tag;  // empty = not tagging

}  // namespace

namespace browser_tag {

void enable(const std::string& tag) { g_tag = tag; }

void tick(bool trace) {
    if (g_tag.empty()) return;
    const std::string name = game_text::read(kHostSessionName, kTextBytes);
    const std::string tagged = force_join_names::withTag(name, g_tag, kTextBytes);
    if (tagged != name) {
        game_text::write(kHostSessionName, kTextBytes, tagged);
        if (trace) logger::write("browser tag: session name \"%s\"", tagged.c_str());
    }
    if (game_text::read(kBrowserNameFilter, kTextBytes).empty()) {
        game_text::write(kBrowserNameFilter, kTextBytes, g_tag);
        if (trace) logger::write("browser tag: name filter \"%s\"", g_tag.c_str());
    }
}

}  // namespace browser_tag
