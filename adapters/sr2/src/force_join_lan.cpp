#include "force_join_lan.h"

#include <intrin.h>

#include "force_join_sites.h"
#include "guest_call.h"
#include "guest_join_rules.h"
#include "join_status.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "tunnel.h"
#include "tunnel_rules.h"
#include "version_gate.h"

namespace {

using namespace force_join_sites;
using guest_join_rules::LanEntry;
using guest_join_rules::Step;

constexpr uint32_t kUnusedLuaArgument = 0;

guest_join_rules::Machine g_machine(guest_join_rules::Mode::Lan);
bool g_searchStarted = false;  // by this module, so reset never stops the player's own search
bool g_trace = false;
LanEntry g_firstEntry;
std::optional<uint32_t> g_hostAddress;

struct LanList {
    std::optional<LanEntry> first;
    size_t listed = 0;
};

// The game's list of found LAN games: circular, walked up to its own cap. An entry's address is stored in network order.
LanList readList() {
    LanList list;
    const uintptr_t head = readDword(kLanList);
    uintptr_t entry = head;
    for (size_t i = 0; entry != 0 && i < kMaxLanEntriesWalked; ++i) {
        if (!list.first) {
            list.first = LanEntry{readDword(entry + kLanEntryIdOffset) & kLanServerIdMask,
                                  _byteswap_ulong(readDword(entry + kLanEntryAddressOffset))};
        }
        ++list.listed;
        entry = readDword(entry + kLanEntryNextOffset);
        if (entry == head) break;
    }
    return list;
}

guest_join_rules::Inputs readInputs(uint64_t nowMs) {
    guest_join_rules::Inputs in;
    in.nowMs = nowMs;
    in.sessionActive = readDword(sr2::kSessionPointer) != 0;
    in.dialogOpen = readByte(kCoopDialogOpen) != 0;
    in.frontEndFree = readDword(kStartRequest) == 0 && readByte(kPendingGame) == 0 && !in.dialogOpen && !in.sessionActive;
    in.denyReason = version_gate::lastJoinDeny();
    in.hostAddress = tunnel::hostAddress();
    in.searchRunning = readByte(kLanSearchActive) != 0;
    const LanList list = readList();
    in.lanEntry = list.first;
    in.lanListed = list.listed;
    return in;
}

void stopSearch() {
    if (!g_searchStarted) return;
    guest_call::call(kStopSyslinkServers, {}, {kUnusedLuaArgument});
    g_searchStarted = false;
    if (g_trace) logger::write("force join: guest LAN search stopped");
}

void joinHost(uint32_t serverId) {
    byteField(kRankedJoinByte, 0) = 0;
    version_gate::clearJoinDeny();
    const uint32_t listed = guest_call::call(kJoinSyslinkServer, {}, {serverId});
    if (g_trace) logger::write("force join: guest joins LAN server %u, %s", serverId, listed ? "listed" : "not listed");
}

void logWrongAddress() {
    logger::write("force join: the first LAN game is announced from %s, the host is %s", tunnel_rules::formatAddress(g_firstEntry.address).c_str(),
                  g_hostAddress ? tunnel_rules::formatAddress(*g_hostAddress).c_str() : "unknown");
}

}  // namespace

namespace force_join_lan {

void tick(uint64_t nowMs, bool trace) {
    g_trace = trace;
    const guest_join_rules::Inputs in = readInputs(nowMs);
    g_hostAddress = in.hostAddress;
    if (in.lanEntry) g_firstEntry = *in.lanEntry;
    const guest_join_rules::Decision decision = g_machine.update(in);
    switch (decision.step) {
        case Step::None:
        case Step::Login: break;
        case Step::Search:
            guest_call::call(kFindSyslinkServers, {}, {kUnusedLuaArgument});
            g_searchStarted = true;
            if (g_trace) logger::write("force join: guest LAN search started");
            break;
        case Step::Join: joinHost(static_cast<uint32_t>(decision.index)); break;
        case Step::StopSearch: stopSearch(); break;
        case Step::GiveUp:
            stopSearch();
            if (decision.status.detail == "host_address") logWrongAddress();
            break;
    }
    join_status::publish(decision.status);
}

void reset() {
    if (readDword(sr2::kSessionPointer) == 0) stopSearch();
    g_machine.reset();
}

}  // namespace force_join_lan
