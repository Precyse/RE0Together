#include "force_join.h"

#include <windows.h>

#include <atomic>
#include <optional>
#include <string>

#include "browser_tag.h"
#include "code_patch.h"
#include "force_join_guest.h"
#include "force_join_lan.h"
#include "force_join_names.h"
#include "force_join_sites.h"
#include "game_text.h"
#include "hooks.h"
#include "join_file.h"
#include "join_status.h"
#include "log.h"
#include "memory.h"
#include "paths.h"
#include "player_name.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "version_gate.h"

namespace {

using namespace force_join_sites;
using join_file::JoinRequest;
using join_file::Role;

constexpr uint32_t kFilePollTicks = 60;  // the front end ticks about 60 times a second
constexpr DWORD kMonitorIntervalMs = 1000;

enum class ActiveRole : int { None, Host, Guest };

void* g_frontEndTick = nullptr;
bool g_trace = false;
Transport g_transport = Transport::Native;
std::optional<join_file::Watcher> g_watcher;
std::optional<JoinRequest> g_request;
uint32_t g_ticks = 0;
bool g_failed = false;
std::atomic<ActiveRole> g_role{ActiveRole::None};

const char* roleName(Role role) { return role == Role::Host ? "host" : "guest"; }

// What a guest started is stopped and its progress forgotten.
void resetGuest() {
    if (g_transport == Transport::Steam) {
        force_join_lan::reset();
    } else {
        force_join_guest::reset();
    }
}

// A new, changed or removed join file.
void onRequestChanged() {
    const bool hadRequest = g_request.has_value();
    g_request = g_watcher->request();
    player_name::set(g_request ? g_request->name : std::string());
    resetGuest();
    join_status::writeNext();
    g_role = !g_request ? ActiveRole::None : g_request->role == Role::Host ? ActiveRole::Host : ActiveRole::Guest;
    const bool checksPassword = g_role == ActiveRole::Host && g_transport == Transport::Native;
    version_gate::setJoinPassword(checksPassword ? g_request->password : std::string());
    if (g_trace) {
        if (g_request) {
            logger::write("force join: request role %s, token %s", roleName(g_request->role), g_request->token.c_str());
        } else {
            logger::write("force join: no valid request");
        }
    }
    if (!g_request && hadRequest) join_status::publish({"stopped", ""});
}

// The native host advertises on the online browser: the session name carries the session tag (the browser tag and the
// token; Juiced writes the name once after the login, so the tag is put back whenever it is missing) and the join password
// is the launcher's (the version gate's host check refuses a join_req without it).
void advertiseOnBrowser(const JoinRequest& request) {
    const std::string name = game_text::read(kHostSessionName, kTextBytes);
    const std::string tagged =
        force_join_names::withTag(name, force_join_names::sessionTag(version_gate::browserTag(), request.token), kTextBytes);
    if (tagged != name) {
        game_text::write(kHostSessionName, kTextBytes, tagged);
        if (g_trace) logger::write("force join: host session name \"%s\"", tagged.c_str());
    }
    if (game_text::read(kCoopPassword, kTextBytes) != request.password) {
        game_text::write(kCoopPassword, kTextBytes, request.password);
        if (g_trace) logger::write("force join: host co-op password set");
    }
}

// The host's part, every front end tick: the session is public (else QR2 never lists it, and a LAN pong is refused when
// the join type is friends).
void hostTick(const JoinRequest& request) {
    if (g_transport == Transport::Native) advertiseOnBrowser(request);
    if (dwordField(kJoinType, 0) != kJoinTypePublic) {
        dwordField(kJoinType, 0) = kJoinTypePublic;
        if (g_trace) logger::write("force join: host join type set to public");
    }
    if (readDword(sr2::kSessionPointer) == 0) join_status::publish({"advertising", ""});
}

// Follows the join file and does the host's or the guest's part of the request; nothing without one.
void followRequest() {
    if (g_ticks++ % kFilePollTicks == 0 && g_watcher->poll()) onRequestChanged();
    if (!g_request) return;
    if (g_request->role == Role::Host) {
        hostTick(*g_request);
    } else if (g_transport == Transport::Steam) {
        force_join_lan::tick(GetTickCount64(), g_trace);
    } else {
        force_join_guest::tick(*g_request, GetTickCount64(), g_trace);
    }
}

void frontEndTick() {
    if (g_failed) return;
    try {
        followRequest();
        browser_tag::tick(g_trace);
    } catch (...) {
        g_failed = true;
        logger::write("force join: the tick failed with an exception, force join is stopped");
    }
}

void __cdecl onFrontEndTick(GuestFrame*) { frontEndTick(); }

// Runs before the front end tick as the game has it.
TRACE_ENTRY_STUB(frontEndTickStub, onFrontEndTick, g_frontEndTick)

// True when the session was started from Co-op > LAN (its info's LAN flag): an online session cannot be found by the LAN
// browser the guests search.
bool isLanSession(uintptr_t session) {
    return readByte(readDword(session + kSessionInfoOffset) + kInfoLanFlagOffset) != 0;
}

// In a session the front end tick does not run: this thread reports the host's member count, and whether the guest got
// in, once a second. It only reads the game.
void reportSession() {
    const ActiveRole role = g_role.load();
    const uintptr_t session = readDword(sr2::kSessionPointer);
    if (role == ActiveRole::None || session == 0) return;
    if (role == ActiveRole::Host) {
        const bool onlineSession = g_transport == Transport::Steam && !isLanSession(session);
        join_status::publish(onlineSession ? join_status::Status{"wrong_mode", ""}
                                           : join_status::Status{"in_session", std::to_string(readDword(session + sr2::kSessionMemberCountOffset))});
    } else {
        const bool in = remote_players::anyRemoteMemberAtLeast(session, sr2::kMemberStateReady);
        join_status::publish({in ? "joined" : "joining", ""});
    }
}

DWORD WINAPI monitorThread(LPVOID) {
    for (;;) {
        Sleep(kMonitorIntervalMs);
        try {
            reportSession();
        } catch (...) {
            logger::writeUnlessRepeated("force join: the session report failed with an exception");
        }
    }
}

}  // namespace

namespace force_join {

bool install(bool trace, Transport transport) {
    if (!codeMatches(force_join_sites::checks())) {
        logger::write("force join: sites differ, nothing installed");
        return false;
    }
    g_trace = trace;
    g_transport = transport;
    if (transport == Transport::Native) browser_tag::enable(version_gate::browserTag());
    join_status::open(trace);
    g_watcher.emplace(coopDirectory() + L"\\" + join_file::kJoinFileName);
    if (!hooks::install("force_join_front_end_tick", kFrontEndTick, reinterpret_cast<void*>(&frontEndTickStub),
                        &g_frontEndTick)) {
        return false;
    }
    if (HANDLE thread = CreateThread(nullptr, 0, monitorThread, nullptr, 0, nullptr)) CloseHandle(thread);
    logger::write("force join: %zu sites checked, the front end tick is hooked, %s transport", kSiteCount,
                  transportName(transport));
    return true;
}

}  // namespace force_join
