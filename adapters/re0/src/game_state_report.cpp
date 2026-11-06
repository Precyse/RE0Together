#include "game_state_report.h"

#include <string>

#include "game_state.h"
#include "log.h"
#include "protocol.h"
#include "session_slot.h"
#include "title_load.h"

namespace {

using namespace game_state_report_rule;

bool g_coop = false;
Save g_save = Save::Off;
bool g_linkWasUp = false;  // net thread only, like the rest below
std::string g_lastSent;

Report current() {
    Report report;
    report.place = placeOf(game_state::roomPhase(), game_state::doorActive(), title_load::busy());
    report.save = g_save;
    report.slot = session_slot::current();
    report.coop = g_coop;
    report.load = title_load::loadResult();
    return report;
}

}  // namespace

namespace game_state_report {

void configure(bool coop, bool saveRedirectInstalled) {
    g_coop = coop;
    g_save = !coop ? Save::Off : saveRedirectInstalled ? Save::Ok : Save::Refused;
}

void onNetTick(NetClient& net, const SessionSnapshot& session) {
    if (!session.linked) {
        g_linkWasUp = false;
        return;
    }
    const std::string text = format(current());
    if (g_linkWasUp && text == g_lastSent) return;
    const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    if (!net.send(proto::kGameState, true, proto::kSlotAll, bytes)) return;
    g_linkWasUp = true;
    g_lastSent = text;
    logger::write("game_state: %s", text.c_str());
}

}  // namespace game_state_report
