#include "force_join_guest.h"

#include <algorithm>
#include <vector>

#include "browser_match.h"
#include "force_join_names.h"
#include "force_join_sites.h"
#include "game_text.h"
#include "guest_call.h"
#include "guest_join_rules.h"
#include "join_status.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "version_gate.h"

namespace {

using namespace force_join_sites;
using guest_join_rules::Browser;
using guest_join_rules::Step;

guest_join_rules::Machine g_machine;
bool g_queryStarted = false;
uintptr_t g_matchedEntry = 0;
bool g_trace = false;

// The browser module: the ring entry of type 4, or 0.
uintptr_t browserModule() {
    const uintptr_t head = readDword(kModuleRing);
    uintptr_t module = head;
    for (size_t i = 0; module != 0 && i < kMaxModulesWalked; ++i) {
        if (readDword(module + kModuleTypeOffset) == kModuleTypeBrowser) return module;
        module = readDword(module + kModuleNextOffset);
        if (module == head) break;
    }
    return 0;
}

Browser browserState(uintptr_t module) {
    return static_cast<Browser>(std::min<uint32_t>(readDword(module + kBrowserStateOffset),
                                                   static_cast<uint32_t>(Browser::Error)));
}

// online_name of a browser server, empty when it has none.
std::string serverName(uintptr_t server) {
    const uintptr_t text = guest_call::call(kServerString, {.ecx = static_cast<uint32_t>(kOnlineNameKey),
                                                           .eax = static_cast<uint32_t>(server)},
                                            {0});
    return text ? game_text::read(text, kTextBytes) : std::string();
}

std::vector<browser_match::Entry> readEntries(uintptr_t module) {
    std::vector<browser_match::Entry> entries;
    const uintptr_t first = readDword(module + kBrowserEntriesOffset);
    uintptr_t entry = first;
    for (size_t i = 0; entry != 0 && i < kMaxEntriesWalked; ++i) {
        browser_match::Entry item{entry, readByte(entry + kEntryListedOffset) != 0, {}};
        if (item.listed) item.name = serverName(readDword(entry + kEntryServerOffset));
        entries.push_back(item);
        entry = readDword(entry + kEntryNextOffset);
        if (entry == first) break;
    }
    return entries;
}

guest_join_rules::Inputs readInputs(const join_file::JoinRequest& request, uint64_t nowMs) {
    guest_join_rules::Inputs in;
    in.nowMs = nowMs;
    in.loggedIn = readDword(kOnlineState) == kOnlineStateConnected;
    in.sessionActive = readDword(sr2::kSessionPointer) != 0;
    in.denyReason = version_gate::lastJoinDeny();
    in.dialogOpen = readByte(kCoopDialogOpen) != 0;
    in.frontEndFree = readDword(kStartRequest) == 0 && readByte(kPendingGame) == 0 && !in.dialogOpen && !in.sessionActive;
    g_matchedEntry = 0;
    const uintptr_t module = browserModule();
    if (!module) return in;
    in.browser = browserState(module);
    if (in.browser == Browser::Idle) return in;
    const std::vector<browser_match::Entry> entries = readEntries(module);
    in.listed = browser_match::listedCount(entries);
    if (const auto match = browser_match::find(entries, request.token)) {
        in.match = match->index;
        g_matchedEntry = match->address;
    }
    return in;
}

void startQuery(const join_file::JoinRequest& request) {
    const std::string filter = force_join_names::tagOf(request.token);
    game_text::write(kBrowserNameFilter, kTextBytes, filter);
    dwordField(kCustomGameFilter, 0) = kCustomGameAny;
    dwordField(kNameToUse, 0) = kNameToUseOnline;
    guest_call::call(kStartMatchmaking, {});
    g_queryStarted = true;
    if (g_trace) logger::write("force join: guest query started, name filter \"%s\"", filter.c_str());
}

void joinHost(const join_file::JoinRequest& request, size_t index) {
    game_text::write(kJoinPassword, kTextBytes, request.password);
    byteField(g_matchedEntry, kEntryPasswordFlagOffset) = 0;
    byteField(kRankedJoinByte, 0) = 0;
    version_gate::clearJoinDeny();
    if (g_trace) logger::write("force join: guest joins entry 0x%X at index %zu", static_cast<unsigned>(g_matchedEntry), index);
    guest_call::call(kJoinByIndex, {.eax = static_cast<uint32_t>(index)});
}

void stopQuery() {
    if (!g_queryStarted) return;
    guest_call::call(kStopMatchmaking, {});
    game_text::write(kBrowserNameFilter, kTextBytes, {});
    g_queryStarted = false;
    if (g_trace) logger::write("force join: guest query stopped");
}

}  // namespace

namespace force_join_guest {

void tick(const join_file::JoinRequest& request, uint64_t nowMs, bool trace) {
    g_trace = trace;
    const guest_join_rules::Decision decision = g_machine.update(readInputs(request, nowMs));
    switch (decision.step) {
        case Step::None: break;
        case Step::Login:
            guest_call::call(kLogin, {});
            if (g_trace) logger::write("force join: guest logs in to the online service");
            break;
        case Step::Search: startQuery(request); break;
        case Step::Join: joinHost(request, decision.index); break;
        case Step::GiveUp: stopQuery(); break;
    }
    join_status::publish(decision.status);
}

void reset() {
    if (readDword(sr2::kSessionPointer) == 0) stopQuery();
    g_machine.reset();
    g_queryStarted = false;
}

}  // namespace force_join_guest
