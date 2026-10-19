#include "waypoint_scope.h"

#include <string>
#include <string_view>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "waypoint_scope_rules.h"

namespace {

using partner_sites::Thunk;
using waypoint_scope_rules::kMaxControlBytes;
using waypoint_scope_rules::kMaxNameBytes;

constexpr size_t kLuaStateArg = 0;
constexpr size_t kBufferArg = 0;
constexpr size_t kLengthArg = 1;

bool g_trace = false;
void* g_missionDebugOriginal = nullptr;  // the trampoline of mission_debug
waypoint_scope_rules::Scope g_scope;

// The first argument of the running Lua native as text; false when the native got none or it is not a string. One character more
// than a control can hold is read, so a longer text is not mistaken for one.
bool firstArgumentText(uintptr_t lua, char* out, size_t capacity) {
    const uint32_t base = readDword(lua + sr2::kLuaBaseOffset);
    const uint32_t top = readDword(lua + sr2::kLuaTopOffset);
    if (!waypoint_scope_rules::hasArgument(base, top) || !waypoint_scope_rules::isLuaString(readDword(base))) return false;
    const uintptr_t string = readDword(base + sr2::kLuaValueOffset);
    if (!string) return false;
    readCString(string + sr2::kLuaStringCharsOffset, out, capacity);
    return true;
}

// GP1: the control text of a script opens or closes the scope; the game's mission_debug then runs as it is.
void __cdecl waypointScopeControl(GuestFrame* frame) {
    char text[kMaxControlBytes + 2];
    if (!firstArgumentText(frame->args[kLuaStateArg], text, sizeof(text))) return;
    const waypoint_scope_rules::Control control = waypoint_scope_rules::parseControl(text);
    if (!control.isControl) return;
    if (!control.open) {
        g_scope.close();
        return;
    }
    const std::string previous(g_scope.name());
    if (g_scope.open(control.name) && g_trace) {
        logger::write("trace: waypoint: scope %s was still open, now %.*s", previous.c_str(), static_cast<int>(control.name.size()),
                      control.name.data());
    }
}

// The remote player human named `name` ("#PLAYER3#", the text the scripts know it by): its owner slot and the member that owns it.
struct Recipient {
    uintptr_t member;
    uint8_t slot;
};

Recipient recipientNamed(std::string_view name) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    char texts[partner_rules::kMaxWalked][kMaxNameBytes + 1];
    std::string_view names[partner_rules::kMaxWalked];
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        readCString(readDword(snapshot.humans[i].human + sr2::kHumanScriptNameOffset), texts[i], sizeof(texts[i]));
        names[i] = texts[i];
    }
    const size_t found = waypoint_scope_rules::findName(names, snapshot.humanCount, name);
    if (found == snapshot.humanCount) return {0, partner_rules::kNoSlot};
    return {remote_players::memberOfHuman(snapshot.humans[found].human), snapshot.humans[found].ownerSlot};
}

// GP2 to GP4: the broadcast of a waypoint packet, cdecl(buffer, length) with edi = the session. While the scope names a remote
// that has a member the packet goes to that member only.
void __cdecl sendWaypointToScope(GuestFrame* frame) {
    const uint32_t buffer = frame->args[kBufferArg];
    const uint32_t length = frame->args[kLengthArg];
    if (g_scope.active()) {
        const std::string name(g_scope.name());
        const Recipient recipient = recipientNamed(name);
        if (recipient.member) {
            if (g_trace) {
                logger::write("trace: waypoint: to %s only (slot %u, member 0x%X), %u bytes", name.c_str(), static_cast<unsigned>(recipient.slot),
                              static_cast<unsigned>(recipient.member), length);
            }
            frame->regs.eax = guest_call::call(sr2::kSendToMember, {.esi = recipient.member}, {buffer, length, 0, 0});
            return;
        }
        if (g_trace) logger::write("trace: waypoint: %s names no remote member, sent to every remote", name.c_str());
    }
    frame->regs.eax = guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {buffer, length});
}

TRACE_ENTRY_STUB(waypointScopeControlStub, waypointScopeControl, g_missionDebugOriginal)
PARTNER_WRAP_STUB(sendWaypointToScopeStub, sendWaypointToScope, 0)

}  // namespace

namespace waypoint_scope {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::WaypointScopeControl: return {reinterpret_cast<void*>(&waypointScopeControlStub), &g_missionDebugOriginal};
        case Thunk::SendWaypointToScope: return {reinterpret_cast<void*>(&sendWaypointToScopeStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace waypoint_scope
