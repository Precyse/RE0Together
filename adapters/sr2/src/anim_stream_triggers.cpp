#include "anim_stream_triggers.h"

#include <mutex>
#include <vector>

#include "activity_state.h"
#include "anim_stream_rules.h"
#include "anim_stream_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using anim_stream_rules::kNoSlot;
using anim_stream_rules::SlotPreset;
using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;

// The cleanup loop (0x6886E0) counts edi in bytes through two slots: 0 the local player's, 4 the partner's.
constexpr uint32_t kCleanupAfterLocal = 0;
constexpr uint32_t kCleanupPartner = 4;
constexpr uint32_t kCleanupEnd = 8;

void* g_setOriginal = nullptr;
void* g_tickOriginal = nullptr;
void* g_resetOriginal = nullptr;
const uintptr_t g_cleanupLoop = sr2::kTriggerCleanupLoop;
const uintptr_t g_cleanupDone = sr2::kTriggerCleanupDone;
bool g_trace = false;
std::mutex g_mutex;
anim_stream_rules::TriggerSlots g_slots;

// The passes the cleanup still has to make for the members other than the default partner.
struct CleanupPasses {
    std::vector<SlotPreset> passes;
    size_t next = 0;
};
CleanupPasses g_cleanup;

uint32_t& partnerPreset() { return dwordField(sr2::kTriggerPartnerPreset, 0); }

// The slot of the default partner (the one the game's own code keeps in [0x2527B68]), kNoSlot for none. False when the pairing of
// humans and members cannot be trusted: the game's own answers stand then and nothing here changes [0x2527B68].
bool defaultSlot(const remote_players::Snapshot& snapshot, uint8_t& slot) {
    const remote_players::Partner partner = remote_players::defaultPartner(snapshot);
    slot = anim_stream_state::slotOfMember(partner.member);
    return partner.trusted;
}

// The slots of the remote members that are in the session, as slot bits.
uint8_t presentSlots(const remote_players::Snapshot& snapshot) {
    uint8_t present = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (!snapshot.members[i].local) present |= anim_stream_rules::slotBit(snapshot.members[i].slot);
    }
    return present;
}

uintptr_t memberOfSlot(const remote_players::Snapshot& snapshot, uint8_t slot) {
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (!snapshot.members[i].local && snapshot.members[i].slot == slot) return snapshot.members[i].address;
    }
    return 0;
}

// ---- TS1: a remote enters or leaves a trigger ----

// TS1: misc sub 0x16, cdecl(payload, context). The handler's Set_Player_Character and Restore_Player_Character work on [0x2527B68]; it
// holds the sender's preset while they run, and the default partner's again afterwards.
void __cdecl triggerSetPerSender(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const uint8_t sender = anim_stream_state::slotOfMember(activity_state::senderOf(context));
    const auto runHandler = [&] {
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_setOriginal), frame->regs,
                                           {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
    };
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    uint8_t standing = kNoSlot;
    if (sender >= anim_stream_rules::TriggerSlots::kSlots || !defaultSlot(snapshot, standing)) {
        runHandler();
        return;
    }
    const uint32_t shipped = partnerPreset();
    uint32_t before = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_slots.syncDefault(standing, shipped);
        before = g_slots.swapIn(sender);
        partnerPreset() = before;
    }
    runHandler();
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t after = partnerPreset();
    g_slots.swapOut(sender, after);
    partnerPreset() = g_slots.swapIn(standing);
    if (g_trace && after != before) logger::write("trace: trigger sets: slot %u preset 0x%X -> 0x%X", sender, before, after);
}

// ---- TS2: the trigger tick ----

// Releases the set of a preset whose member left, as the game's tick releases the partner's (0xA79240: edx = the set,
// cdecl(category, key, isHost)). The category is the one the game's own partner load uses; any other value is not trusted.
void releaseGone(const SlotPreset& gone) {
    const uint8_t category = anim_stream_state::triggerLoadCategory();
    if (!anim_stream_rules::releasableCategory(category)) {
        logger::write("trigger sets: category byte 0x%02X at 0x%X is neither %u nor %u, release skipped", category,
                      static_cast<unsigned>(sr2::kTriggerPartnerLoadCategory), anim_stream_rules::kPartnerCategoryVanilla,
                      anim_stream_rules::kPartnerCategoryJuiced);
        return;
    }
    const uint32_t set = readDword(readDword(gone.preset + sr2::kPresetSetOffset) + sr2::kPresetSetIdOffset);
    guest_call::call(sr2::kStreamRelease, {.edx = set}, {category, 0, activity_state::isHost() ? 1u : 0u});
    if (g_trace) logger::write("trace: trigger sets: slot %u left, preset 0x%X released (category %u)", gone.slot, gone.preset, category);
}

// TS2: runs before the game's tick. The preset the game's code left in [0x2527B68] goes to the default partner it belonged to, the
// presets of members that left are released, and [0x2527B68] holds the new default partner's preset.
void __cdecl triggerTick(GuestFrame*) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    uint8_t standing = kNoSlot;
    if (!defaultSlot(snapshot, standing)) return;
    std::vector<SlotPreset> gone;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const uint8_t previous = g_slots.lastDefault();
        g_slots.syncDefault(previous, partnerPreset());
        gone = g_slots.gone(presentSlots(snapshot));
        if (g_slots.defaultChanged(standing) && g_trace) logger::write("trace: trigger sets: default partner slot %u -> %u", previous, standing);
        partnerPreset() = g_slots.swapIn(standing);
    }
    for (const SlotPreset& entry : gone) releaseGone(entry);
}

// ---- TS3: the trigger cleanup ----

// TS3: the cleanup's loop (0x6886E0) runs its body for the local player and the partner. After the partner's pass, one more pass of the
// same body is made for each other member that holds a preset: its human and preset are set where the body reads them. `index` is the
// loop counter (edi), `human` the stack slot of the partner's human. Returns the counter to go on with: 4 for another pass, 8 to leave.
uint32_t __cdecl cleanupPass(uint32_t index, uint32_t* human) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (index == kCleanupAfterLocal) {
        uint8_t standing = kNoSlot;
        defaultSlot(snapshot, standing);
        g_cleanup.passes = g_slots.cleanupPasses(standing);
        g_cleanup.next = 0;
        return kCleanupPartner;
    }
    if (g_cleanup.next >= g_cleanup.passes.size()) return kCleanupEnd;
    const SlotPreset pass = g_cleanup.passes[g_cleanup.next++];
    const uintptr_t member = memberOfSlot(snapshot, pass.slot);
    *human = member ? static_cast<uint32_t>(remote_players::humanOfMember(member)) : 0;
    partnerPreset() = pass.preset;
    if (g_trace) logger::write("trace: trigger sets: cleanup released slot %u preset 0x%X", pass.slot, pass.preset);
    return kCleanupPartner;
}

// Replaces `add edi, 4; cmp edi, 8; jl loop` at the end of the cleanup loop. The only registers live after the loop are none.
void __declspec(naked) triggerCleanupStub() {
    __asm {
        pushad
        lea eax, [esp + 0x2C]  // the partner's human: [esp + 0xC] of the function
        push eax
        push edi
        call cleanupPass
        add esp, 8
        mov [esp], eax  // edi
        popad
        cmp edi, 8
        jl again
        jmp dword ptr [g_cleanupDone]
    again:
        jmp dword ptr [g_cleanupLoop]
    }
}

// ---- TS4: the trigger system reset ----

// TS4: the game zeroed [0x2527B64] and [0x2527B68] without releasing anything; the table is emptied the same way.
void __cdecl triggerReset(GuestFrame* frame) {
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_resetOriginal), frame->regs);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_slots.clear();
}

PARTNER_WRAP_STUB(triggerSetStub, triggerSetPerSender, 0)
TRACE_ENTRY_STUB(triggerTickStub, triggerTick, g_tickOriginal)
PARTNER_WRAP_STUB(triggerResetStub, triggerReset, 0)

}  // namespace

namespace anim_stream_triggers {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::TriggerSetPerSender: return {reinterpret_cast<void*>(&triggerSetStub), &g_setOriginal};
        case Thunk::TriggerTick: return {reinterpret_cast<void*>(&triggerTickStub), &g_tickOriginal};
        case Thunk::TriggerCleanupPass: return {reinterpret_cast<void*>(&triggerCleanupStub), nullptr};
        case Thunk::TriggerReset: return {reinterpret_cast<void*>(&triggerResetStub), &g_resetOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace anim_stream_triggers
