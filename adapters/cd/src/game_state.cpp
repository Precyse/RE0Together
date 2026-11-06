#include "game_state.h"

#include <string>

#include "game_state_rule.h"
#include "link.h"
#include "log.h"
#include "presence_recv.h"
#include "presence_send.h"
#include "protocol.h"
#include "world_gate.h"

namespace {

bool g_presenceConfigured = true;
bool g_namesFound = true;
std::string g_lastSent;  // empty: nothing sent on this link yet

game_state_rule::Report current() {
    game_state_rule::Report report;
    report.place = game_state_rule::placeOf(world_gate::state());
    report.copy = game_state_rule::copyOf({g_presenceConfigured, g_namesFound, presence_send::recipesDiffer(), presence_recv::copyProblem()});
    report.peers = link::peerLinks();
    return report;
}

}  // namespace

namespace game_state {

void setPresence(bool configured, bool namesFound) {
    g_presenceConfigured = configured;
    g_namesFound = namesFound;
}

void tick() {
    if (!link::isLinked()) {
        g_lastSent.clear();
        return;
    }
    const std::string text = game_state_rule::format(current());
    if (text == g_lastSent) return;
    if (!link::send(proto::kGameState, true, {reinterpret_cast<const uint8_t*>(text.data()), text.size()})) return;
    g_lastSent = text;
    logger::write("game_state: %s", text.c_str());
}

}  // namespace game_state
