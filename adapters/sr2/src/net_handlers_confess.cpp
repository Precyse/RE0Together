#include "net_handlers_confess.h"

#include <cmath>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "net_handlers.h"
#include "net_handlers_rules.h"
#include "partner_rules.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "session_kick_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using session_kick_rules::Entry;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;

// A remote that has a human.
struct Remote {
    uintptr_t human;
    Entry entry;
};

// The remotes that failed the status of the last evaluation, for the notice the host sends: too far (code 2), too poor (code 6).
struct Recorded {
    Entry entries[kMaxRemotes];
    size_t count;
};

bool g_trace = false;
Recorded g_tooFar{};
Recorded g_short{};
net_handlers_rules::ConfessHolders g_holders;

uint32_t& lock() { return dwordField(sr2::kConfessLock, 0); }

double distanceLimit() {
    double limit = 0;
    readMemory(sr2::kConfessDistanceLimit, &limit, sizeof(limit));
    return limit;
}

int32_t cashOf(uintptr_t human) { return static_cast<int32_t>(readDword(human + sr2::kHumanCashOffset)); }

size_t remotesWithHuman(Remote (&out)[kMaxRemotes]) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (remote.human) out[count++] = {remote.human, {remote.member, remote.slot}};
    }
    return count;
}

std::string slotsOf(const Recorded& recorded) { return session_kick_rules::slotsText(recorded.entries, recorded.count); }

void forget(Recorded& recorded, uintptr_t member) {
    size_t kept = 0;
    for (size_t i = 0; i < recorded.count; ++i) {
        if (recorded.entries[i].member != member) recorded.entries[kept++] = recorded.entries[i];
    }
    recorded.count = kept;
}

// CF1, CF3: "the partner" of the too far status and of its text is the remote farthest from the confessional; the remotes beyond the
// limit are recorded for the notice. The price is on the x87 stack at CF1.
uintptr_t __cdecl confessFarthestRemote() {
    FpuGuard guard;
    g_tooFar.count = 0;
    Remote remotes[kMaxRemotes];
    const size_t count = remotesWithHuman(remotes);
    const uintptr_t object = readDword(sr2::kConfessLocal);
    if (!object || count == 0) return partner_scope::partnerHuman();
    const partner_rules::Vec3 point = remote_players::positionOf(object);
    partner_rules::Vec3 positions[kMaxRemotes];
    for (size_t i = 0; i < count; ++i) positions[i] = remote_players::positionOf(remotes[i].human);
    const size_t farthest = partner_rules::farthestIndex(positions, count, point);
    const double limit = distanceLimit();
    for (size_t i = 0; i < count; ++i) {
        if (net_handlers_rules::tooFar(partner_rules::distanceSquared(positions[i], point), limit)) g_tooFar.entries[g_tooFar.count++] = remotes[i].entry;
    }
    if (g_trace) {
        logger::write("trace: confess: farthest slot %u at %.1f m (limit %.0f m), too far: slots {%s}", remotes[farthest].entry.slot,
                      std::sqrt(partner_rules::distanceSquared(positions[farthest], point)), std::sqrt(limit), slotsOf(g_tooFar).c_str());
    }
    return remotes[farthest].human;
}

// The remote with the least cash, 0 without a remote human.
Remote poorestRemote(const Remote* remotes, size_t count, int32_t (&cash)[kMaxRemotes]) {
    for (size_t i = 0; i < count; ++i) cash[i] = cashOf(remotes[i].human);
    const size_t poorest = net_handlers_rules::poorestIndex(cash, count);
    return poorest == partner_rules::kNone ? Remote{} : remotes[poorest];
}

// CF2: "the partner" of the cannot pay status is the remote with the least cash; the remotes that cannot pay `price` are recorded for
// the notice.
uintptr_t __cdecl confessPoorestRemoteAt(int32_t price) {
    g_short.count = 0;
    Remote remotes[kMaxRemotes];
    const size_t count = remotesWithHuman(remotes);
    int32_t cash[kMaxRemotes];
    const Remote poorest = poorestRemote(remotes, count, cash);
    if (!poorest.human) return partner_scope::partnerHuman();
    for (size_t i = 0; i < count; ++i) {
        if (net_handlers_rules::shortOf(cash[i], price)) g_short.entries[g_short.count++] = remotes[i].entry;
    }
    if (g_trace) {
        logger::write("trace: confess: poorest slot %u cash %d, price %d, short: slots {%s}", poorest.entry.slot, cashOf(poorest.human), price,
                      slotsOf(g_short).c_str());
    }
    return poorest.human;
}

// CF4: the name for the cannot pay text.
uintptr_t __cdecl confessPoorestRemote() {
    Remote remotes[kMaxRemotes];
    const size_t count = remotesWithHuman(remotes);
    int32_t cash[kMaxRemotes];
    const Remote poorest = poorestRemote(remotes, count, cash);
    return poorest.human ? poorest.human : partner_scope::partnerHuman();
}

// CF5: the host's notice (the packet in `buffer`) of a failed status goes to the recorded remotes, else to everyone as the game does.
void __cdecl confessNotice(uint32_t buffer, uint32_t length, uint32_t code) {
    const uint8_t notice = static_cast<uint8_t>(code);
    const Recorded& recorded = notice == sr2::kConfessNoticePoor ? g_short : g_tooFar;
    if (recorded.count == 0) {
        guest_call::call(sr2::kSendBroadcast, {.edi = readDword(sr2::kSessionPointer)}, {buffer, length});
        if (g_trace) logger::write("trace: confess: notice %u broadcast, no recorded set", notice);
        return;
    }
    for (size_t i = 0; i < recorded.count; ++i) {
        guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(recorded.entries[i].member)}, {buffer, length, 0, 0});
    }
    if (g_trace) logger::write("trace: confess: notice %u sent to slots {%s}", notice, slotsOf(recorded).c_str());
}

void traceNotShown(uint8_t notice, const char* reason) {
    if (g_trace) logger::write("trace: confess: notice %u from slot %u not shown, %s", notice, net_handlers::memberSlot(net_handlers::senderMember()), reason);
}

// CF8: a guest's "a partner is too far" notice (code 2). It is shown only when the local player is too far from that guest's confessional.
uintptr_t __cdecl confessFarNotice(uint32_t code) {
    if (static_cast<uint8_t>(code) != sr2::kConfessNoticeFar) return sr2::kConfessNotCodeFar;
    FpuGuard guard;
    const uintptr_t object = readDword(sr2::kConfessLock);
    const uintptr_t local = remote_players::localHuman();
    const bool fails = !object || !local ||
                       net_handlers_rules::tooFar(partner_rules::distanceSquared(remote_players::positionOf(local), remote_players::positionOf(object)), distanceLimit());
    if (net_handlers_rules::showsNotice(remote_players::remoteHumanCount(), fails)) return sr2::kConfessShowFar;
    traceNotShown(sr2::kConfessNoticeFar, "local near");
    return sr2::kConfessReturn;
}

// CF9: a guest's "a partner cannot pay" notice (code 6), whose price is `priceBits`. It is shown only when the local player cannot pay it.
uintptr_t __cdecl confessPoorNotice(uint32_t code, uint32_t priceBits) {
    if (static_cast<uint8_t>(code) != sr2::kConfessNoticePoor) return sr2::kConfessNotCodePoor;
    FpuGuard guard;
    int32_t price = 0;
    guest_call::call(sr2::kPriceToCents, {.esi = reinterpret_cast<uint32_t>(&price)}, {priceBits});
    const uintptr_t local = remote_players::localHuman();
    const bool fails = !local || net_handlers_rules::shortOf(cashOf(local), price);
    if (net_handlers_rules::showsNotice(remote_players::remoteHumanCount(), fails)) return sr2::kConfessShowPoor;
    traceNotShown(sr2::kConfessNoticePoor, "local can pay");
    return sr2::kConfessReturn;
}

void traceLock(const char* event, uintptr_t member) {
    if (!g_trace) return;
    Entry holders[kMaxRemotes];
    const size_t count = g_holders.size();
    for (size_t i = 0; i < count; ++i) holders[i] = {g_holders.at(i).member, net_handlers::memberSlot(g_holders.at(i).member)};
    logger::write("trace: confess: lock slot %u %s, holders {%s}, lock 0x%08X", net_handlers::memberSlot(member), event,
                  session_kick_rules::slotsText(holders, count).c_str(), static_cast<unsigned>(lock()));
}

// CF6: sub 0, a remote got into the confessional `object` (the store of the lock).
void __cdecl confessLockSet(uintptr_t object) {
    const uintptr_t sender = net_handlers::senderMember();
    lock() = static_cast<uint32_t>(g_holders.set(lock(), sender, object));
    traceLock("set", sender);
}

// CF7: sub 3, the remote got out (the clear of the lock).
void __cdecl confessLockClear() {
    const uintptr_t sender = net_handlers::senderMember();
    lock() = static_cast<uint32_t>(g_holders.clear(lock(), sender));
    traceLock("cleared", sender);
}

PARTNER_RESUME_EBX_STUB(confessFarNoticeStub, confessFarNotice)
PARTNER_BLOCK_STUB(confessLockClearStub, confessLockClear)

// Replaces `call 0x9d7bd0` after the price was written over its argument slot: the price is at [esp + 0x1C] inside the thunk.
void __declspec(naked) confessPoorestAtPriceStub() {
    __asm {
        push dword ptr [esp + 0x1C]
        call confessPoorestRemoteAt
        add esp, 4
        ret
    }
}

// Replaces `call 0x814740` (EDI = session, cdecl(buffer, length), the caller pops 8): the notice code is in ebx.
void __declspec(naked) confessNoticeStub() {
    __asm {
        push ebx
        push dword ptr [esp + 0xC]
        push dword ptr [esp + 0xC]
        call confessNotice
        add esp, 0xC
        ret
    }
}

// Replaces `cmp bl, 6`: the handler's price float is at [esp + 0xC] of the handler, [esp + 0x30] under the saved registers.
void __declspec(naked) confessPoorNoticeStub() {
    __asm {
        sub esp, 4
        pushad
        push dword ptr [esp + 0x30]
        push ebx
        call confessPoorNotice
        add esp, 8
        mov [esp + 0x20], eax
        popad
        ret
    }
}

// Replaces `mov [lock], eax`: eax is the confessional object.
void __declspec(naked) confessLockSetStub() {
    __asm {
        pushfd
        pushad
        push eax
        call confessLockSet
        add esp, 4
        popad
        popfd
        ret
    }
}

}  // namespace

namespace net_handlers_confess {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::ConfessFarthest: return {reinterpret_cast<void*>(&confessFarthestRemote), nullptr};
        case Thunk::ConfessPoorestAtPrice: return {reinterpret_cast<void*>(&confessPoorestAtPriceStub), nullptr};
        case Thunk::ConfessPoorest: return {reinterpret_cast<void*>(&confessPoorestRemote), nullptr};
        case Thunk::ConfessNotice: return {reinterpret_cast<void*>(&confessNoticeStub), nullptr};
        case Thunk::ConfessFarNotice: return {reinterpret_cast<void*>(&confessFarNoticeStub), nullptr};
        case Thunk::ConfessPoorNotice: return {reinterpret_cast<void*>(&confessPoorNoticeStub), nullptr};
        case Thunk::ConfessLockSet: return {reinterpret_cast<void*>(&confessLockSetStub), nullptr};
        case Thunk::ConfessLockClear: return {reinterpret_cast<void*>(&confessLockClearStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void setTrace(bool trace) { g_trace = trace; }

void memberLeft(uintptr_t leaver) {
    forget(g_tooFar, leaver);
    forget(g_short, leaver);
    if (!g_holders.has(leaver)) return;
    lock() = static_cast<uint32_t>(g_holders.clear(lock(), leaver));
    traceLock("left", leaver);
}

}  // namespace net_handlers_confess
