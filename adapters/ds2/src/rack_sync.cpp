#include "rack_sync.h"

#include <chrono>
#include <map>
#include <vector>

#include "cargo_transfer.h"
#include "game.h"
#include "log.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCheckInterval = std::chrono::milliseconds(500);
constexpr auto kSettle = std::chrono::seconds(2);  // the game serves create and delete requests on its next update

// Net thread only.
Clock::time_point g_lastCheck;
Clock::time_point g_lastChange;
std::map<uint32_t, size_t> g_lastWanted;

// Brings the body's backpack to the partner's kinds: extra pieces deleted, missing kinds created.
void follow(uint64_t ownerKey, const std::vector<game::Cargo>& wanted, Clock::time_point now) {
    std::map<uint32_t, size_t> want;
    for (const game::Cargo& piece : wanted) ++want[piece.type];
    // Only a target that has not changed since the previous check is applied (see equip_sync).
    const bool steady = want == g_lastWanted;
    g_lastWanted = want;
    if (!steady) return;
    std::map<uint32_t, std::vector<uint64_t>> have;
    for (const game::Cargo& piece : game::backpackCargo(ownerKey)) have[piece.type].push_back(piece.handle);
    bool same = true;
    for (const auto& [type, count] : want) same = same && have[type].size() == count;
    for (const auto& [type, handles] : have) same = same && want[type] == handles.size();
    if (same || now - g_lastChange < kSettle) return;
    g_lastChange = now;
    int removed = 0, added = 0;
    for (const auto& [type, handles] : have) {
        for (size_t i = want[type]; i < handles.size(); ++i) removed += game::removeCargo(handles[i]);
    }
    for (const auto& [type, count] : want) {
        for (size_t i = have[type].size(); i < count; ++i) added += game::addBackpackCargo(ownerKey, type);
    }
    logger::write("rack_sync: the body's rack: %d pieces removed, %d added (%zu wanted)", removed, added, wanted.size());
}

}  // namespace

namespace rack_sync {

void tick(NetClient&, const SessionSnapshot& session) {
    const auto now = Clock::now();
    if (!session.linked || now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    const std::optional<uint64_t> body = remote_body::ownerKey();
    const auto partner = cargo_transfer::partner();
    if (body) game::setOwnerActive(*body, false);  // the game may switch it back on
    if (body && partner && partner->slot == remote_body::slot()) follow(*body, partner->cargo, now);
}

}  // namespace rack_sync
