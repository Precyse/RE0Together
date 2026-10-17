#include "player_trace.h"

#include <windows.h>

#include <mutex>
#include <unordered_map>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_rules.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sender_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr uintptr_t kHumanWeaponOffset = 0x10CC;     // the weapon instance in hand
constexpr uintptr_t kWeaponInfoOffset = 0xC4;        // the weapon's table entry
constexpr uintptr_t kWeaponInfoIdOffset = 0xEE;      // word
constexpr uintptr_t kWeaponFiredOffset = 0xC;        // byte, bit 0: the shot has been spawned
constexpr uintptr_t kWeaponNextShotOffset = 0x24;    // dword: the earliest time of the next shot
constexpr uintptr_t kRecordLocalBitsOffset = 0x67C;  // byte in a net record, tested by the owned check
constexpr uintptr_t kOwnedCheck = 0x806720;          // EAX = human, AL out

void* g_shotOriginal = nullptr;
void* g_playbackOriginal = nullptr;
void* g_buffersOriginal = nullptr;
void* g_bundleOriginal = nullptr;
std::mutex g_bundleMutex;
rate_limit::OncePerKey g_bundleReported;
std::unordered_map<uintptr_t, uint64_t> g_lastShotMs;
std::unordered_map<uintptr_t, uint64_t> g_lastWeaponState;

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint16_t wordAt(uintptr_t address) {
    uint16_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint8_t slotOfRecordOf(uintptr_t human) {
    const uintptr_t record = readDword(human + sr2::kObjectNetRecordOffset);
    return record ? byteAt(record) : partner_rules::kNoSlot;
}

uint8_t localSlot() {
    const uintptr_t session = readDword(sr2::kSessionPointer);
    const uintptr_t local = session ? readDword(session + sr2::kSessionLocalMemberOffset) : 0;
    return local ? byteAt(local + sr2::kMemberSlotOffset) : partner_rules::kNoSlot;
}

uint64_t nowMs() { return GetTickCount64(); }

// TF1: cdecl(human) at the entry of the bullet spawn. A fire rate above the weapon's is a gap between shots below its refire.
void __cdecl traceShot(GuestFrame* frame) {
    const uintptr_t human = frame->args[0];
    const uintptr_t record = readDword(human + sr2::kObjectNetRecordOffset);
    const uintptr_t weapon = readDword(human + kHumanWeaponOffset);
    const uintptr_t info = weapon ? readDword(weapon + kWeaponInfoOffset) : 0;
    const uint64_t now = nowMs();
    uint64_t& last = g_lastShotMs[human];
    const uint64_t since = last ? now - last : 0;
    last = now;
    const uint32_t ownedHere = guest_call::call(kOwnedCheck, {.eax = static_cast<uint32_t>(human)}) & 0xFF;
    logger::write("trace: shot: human 0x%X local player %u npc %u owner slot %u (local slot %u) owned-check %u record bits 0x%02X weapon id %u, %llu ms since its last shot",
                  static_cast<unsigned>(human), human == remote_players::localHuman() ? 1 : 0,
                  partner_rules::isPlayerPoolMember(human) ? 0 : 1, slotOfRecordOf(human), localSlot(), ownedHere,
                  record ? byteAt(record + kRecordLocalBitsOffset) : 0, info ? wordAt(info + kWeaponInfoIdOffset) : 0,
                  static_cast<unsigned long long>(since));
}

// TF2: the player bundle's update for the handle at [esp+4]. A remote player's weapon is logged when its fired bit or its next
// shot time changes between two updates (the history entry the update plays is chosen inside and is not visible here).
void __cdecl tracePlayback(GuestFrame* frame) {
    const uintptr_t handle = frame->args[0];
    const uintptr_t human = guest_call::call(sr2::kIsHumanHandle, {.ecx = static_cast<uint32_t>(handle)});
    if (!human || !remote_players::isRemotePlayerHuman(human)) return;
    const uintptr_t weapon = readDword(human + kHumanWeaponOffset);
    if (!weapon) return;
    const uint32_t fired = byteAt(weapon + kWeaponFiredOffset) & 1;
    const uint32_t nextShot = readDword(weapon + kWeaponNextShotOffset);
    const uint64_t state = (static_cast<uint64_t>(nextShot) << 1) | fired;
    uint64_t& previous = g_lastWeaponState[human];
    if (previous == state) return;
    previous = state;
    const uintptr_t session = readDword(sr2::kSessionPointer);
    logger::write("trace: playback: human 0x%X owner slot %u net time %u, weapon fired bit %u, next shot at %u", static_cast<unsigned>(human),
                  slotOfRecordOf(human), session ? readDword(session + sr2::kSessionNetTimeOffset) : 0, fired, nextShot);
}

// TL2: the coop_player buffers are built for [0xE8C438] - 1 players; the game should see the session maximum here.
void __cdecl traceBuffers(GuestFrame*) {
    logger::write("trace: coop_player buffers: the game's player limit is %u", readDword(sr2::kMaxPlayers));
}

// BUT: the player bundle unpack, thiscall(handle, ...). A bundle packet updates the object the handle names whoever sent it;
// it is logged once per handle and sender when the sender is not the object's owner.
void __cdecl traceBundleSender(GuestFrame* frame) {
    const uintptr_t sender = sender_scope::currentSender();
    if (!sender) return;
    const uint32_t handle = frame->args[0];
    const uintptr_t object = remote_players::objectOfHandle(handle);
    const uintptr_t record = object ? readDword(object + sr2::kObjectNetRecordOffset) : 0;
    if (!record) return;
    const uint8_t owner = byteAt(record + sr2::kRecordOwnerOffset);
    const uint8_t senderSlot = byteAt(sender + sr2::kMemberSlotOffset);
    if (owner == senderSlot) return;
    {
        std::lock_guard<std::mutex> lock(g_bundleMutex);
        if (!g_bundleReported.firstTime((static_cast<uint64_t>(handle) << 8) | senderSlot)) return;
    }
    logger::write("trace: bundle: handle 0x%X owner slot %u updated by slot %u", handle, owner, senderSlot);
}

TRACE_ENTRY_STUB(shotStub, traceShot, g_shotOriginal)
TRACE_ENTRY_STUB(playbackStub, tracePlayback, g_playbackOriginal)
TRACE_ENTRY_STUB(buffersStub, traceBuffers, g_buffersOriginal)
TRACE_ENTRY_STUB(bundleStub, traceBundleSender, g_bundleOriginal)

}  // namespace

namespace player_trace {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::TraceShot: return {reinterpret_cast<void*>(&shotStub), &g_shotOriginal};
        case Thunk::TracePlayerPlayback: return {reinterpret_cast<void*>(&playbackStub), &g_playbackOriginal};
        case Thunk::TraceCoopBuffers: return {reinterpret_cast<void*>(&buffersStub), &g_buffersOriginal};
        case Thunk::TraceBundleSender: return {reinterpret_cast<void*>(&bundleStub), &g_bundleOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace player_trace
