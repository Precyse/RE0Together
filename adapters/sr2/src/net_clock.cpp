#include "net_clock.h"

#include <windows.h>

#include <mutex>

#include "activity_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "net_clock_rules.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;

void* g_clockOriginal = nullptr;
void* g_tickOriginal = nullptr;
bool g_trace = false;

std::mutex g_mutex;
uintptr_t g_debtSession = 0;  // the session the debt was measured in
uint32_t g_debt = 0;
rate_limit::RateLimit g_eventLimit;

uint32_t& netTime(uintptr_t session) { return *reinterpret_cast<uint32_t*>(session + sr2::kSessionNetTimeOffset); }

bool governed(uintptr_t session) {
    const bool isHost = readByte(session + sr2::kSessionHostFlagOffset) != 0;
    return net_clock_rules::governs(isHost, remote_players::remoteMemberCount());
}

// The debt of `session`; a new session starts without one. The caller holds the lock.
uint32_t& debtOf(uintptr_t session) {
    if (g_debtSession != session) {
        g_debtSession = session;
        g_debt = 0;
    }
    return g_debt;
}

struct ClockMessage {
    uintptr_t sender;
    uintptr_t session;
    uint32_t payload;
    uint32_t halfRtt;
    uint32_t target;  // the payload plus half the round trip time: what the game sets T to
    uint32_t ours;
};

ClockMessage readClock(const uint32_t* args, uintptr_t sender, uintptr_t session) {
    const uint32_t payload = readDword(args[kPayloadArg]);
    const uint32_t halfRtt = readDword(sender + sr2::kMemberHalfRttOffset);
    return {sender, session, payload, halfRtt, payload + halfRtt, netTime(session)};
}

void traceClock(const ClockMessage& clock, uint32_t debt) {
    logger::write("trace: net clock: from slot %u, payload %u + half rtt %u = target %u, ours %u, step %d ms, debt %u",
                  readByte(clock.sender + sr2::kMemberSlotOffset), clock.payload, clock.halfRtt, clock.target, clock.ours,
                  static_cast<int32_t>(clock.target - clock.ours), debt);
}

void logEvent(const ClockMessage& clock, net_clock_rules::Outcome outcome) {
    uint32_t events = 0;
    if (!g_eventLimit.allow(GetTickCount64(), events)) return;
    if (outcome == net_clock_rules::Outcome::Ignored) {
        logger::write("net clock: ignored a clock from slot %u, which is not the host", readByte(clock.sender + sr2::kMemberSlotOffset));
    } else {
        logger::write("net clock: resync, ours was %u ms ahead of the host", clock.ours - clock.target);
    }
}

// The rule in place of the game's assignment `[session+8] = payload + half rtt`.
void applyClockRule(const ClockMessage& clock) {
    const bool fromHost = clock.sender == readDword(clock.session + sr2::kSessionHostMemberOffset);
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t& debt = debtOf(clock.session);
    const net_clock_rules::Clock result = net_clock_rules::onClock(clock.ours, clock.target, fromHost, debt);
    netTime(clock.session) = result.time;
    debt = result.debt;
    if (g_trace) traceClock(clock, debt);
    if (net_clock_rules::needsLog(result.outcome)) logEvent(clock, result.outcome);
}

// CK1: cdecl(payload, context); the payload starts with the sender's clock. A governed guest applies the rule, every other
// case runs the game's handler.
void __cdecl clockMessage(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t sender = activity_state::senderOf(args[kContextArg]);
    const uintptr_t session = sender ? readDword(sender + sr2::kMemberSessionOffset) : 0;
    if (session) {
        const ClockMessage clock = readClock(args, sender, session);
        if (governed(session)) {
            applyClockRule(clock);
            return;
        }
        if (g_trace) traceClock(clock, 0);
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_clockOriginal), frame->regs, {args[kPayloadArg], args[kContextArg]});
}

// CK2: cdecl(), no arguments. The game adds the frame time to T of every session; a governed guest then pays its debt
// with up to half of what was added.
void __cdecl clockTick(GuestFrame* frame) {
    const uintptr_t session = readDword(sr2::kSessionPointer);
    const uint32_t before = session ? netTime(session) : 0;
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_tickOriginal), frame->regs);
    if (!session || readDword(sr2::kSessionPointer) != session || !governed(session)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t& debt = debtOf(session);
    if (debt == 0) return;
    const net_clock_rules::Tick paid = net_clock_rules::onTick(netTime(session), debt, net_clock_rules::tickAdvance(before, netTime(session)));
    netTime(session) = paid.time;
    debt = paid.debt;
}

PARTNER_WRAP_STUB(clockMessageStub, clockMessage, 0)
PARTNER_WRAP_STUB(clockTickStub, clockTick, 0)

}  // namespace

namespace net_clock {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::NetClockMessage: return {reinterpret_cast<void*>(&clockMessageStub), &g_clockOriginal};
        case Thunk::NetClockTick: return {reinterpret_cast<void*>(&clockTickStub), &g_tickOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace net_clock
