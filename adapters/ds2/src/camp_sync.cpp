#include "camp_sync.h"

#include <chrono>

#include "camp_wire.h"
#include "game.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kFullTableEvery = std::chrono::seconds(5);

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
Clock::time_point g_lastFull;

}  // namespace

namespace camp_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != camp_wire::kMsgCampAlert || !g_guest || frame.slot != g_hostSlot) return;
    std::vector<camp_wire::CampPhase> camps;
    if (camp_wire::decode(frame.payload, camps)) {
        game::applyCampPhases(camps);
    } else {
        logger::write("camp_sync: dropped a malformed CAMP_ALERT (%zu bytes)", frame.payload.size());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::shareCamps(host);
    if (!host) return;
    const auto now = Clock::now();
    const bool full = now - g_lastFull >= kFullTableEvery;
    if (full) g_lastFull = now;
    const std::vector<camp_wire::CampPhase> camps = game::takeCampPhases(full);
    if (!camps.empty() && !net.send(camp_wire::kMsgCampAlert, true, proto::kSlotAll, camp_wire::encode(camps))) {
        logger::write("camp_sync: could not send CAMP_ALERT");
    }
}

}  // namespace camp_sync
