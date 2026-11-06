#include "game_state.h"

#include <string>

#include "load_slot.h"
#include "log.h"
#include "profile.h"
#include "protocol.h"
#include "save_redirect.h"
#include "session.h"
#include "world_gate.h"

namespace {

using game_state_rule::Report;
using game_state_rule::Save;

constexpr uint64_t kRedirectGraceMs = 10000;  // how long a redirect may stay unset before it counts as refused

uint64_t g_nowMs = 0;
uint64_t g_firstMs = 0;
std::string g_lastSent;  // empty: nothing sent on this link yet

std::optional<Save> saveState() {
    switch (save_redirect::state()) {
        case save_redirect::State::Off: return Save::Off;
        case save_redirect::State::Verified: return Save::Ok;
        case save_redirect::State::Refused: return Save::Refused;
        default: return g_nowMs - g_firstMs >= kRedirectGraceMs ? std::optional(Save::Refused) : std::nullopt;
    }
}

}  // namespace

namespace game_state {

std::optional<Report> current() {
    const auto save = saveState();
    if (!save) return std::nullopt;
    Report report;
    report.place = game_state_rule::placeOf(world_gate::place());
    report.character = profile::spliceState();
    report.save = *save;
    report.slot = load_slot::current();
    for (const session::PeerView& peer : session::peers()) report.peers.push_back({peer.steamId, peer.stale});
    return report;
}

void tick(uint64_t nowMs) {
    g_nowMs = nowMs;
    if (!g_firstMs) g_firstMs = nowMs;
    const auto report = current();
    if (!report || !session::hostSlot()) {
        g_lastSent.clear();
        return;
    }
    const std::string text = game_state_rule::format(*report);
    if (text == g_lastSent) return;
    const bool sent = session::send(proto::kGameState, true, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    if (!sent) return;
    g_lastSent = text;
    logger::write("game_state: %s", text.c_str());
}

}  // namespace game_state
