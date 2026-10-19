// Streaking (the flashing diversion) for every remote. The game keeps one byte "the remote streaks" and one coat entry for "the
// remote" (index 1 of four arrays); it dresses the partner. Here the byte is a mask of streaking slots, each remote's coat is
// kept per slot, and the game's code runs once per streaking remote with that remote's coat in the game's entry.
#include "activity_state.h"
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_stubs.h"
#include "diversion_swap.h"
#include "diversion_thunks.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "sender_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr uint8_t kStreakOn = 1;

struct CoatCell {
    uint32_t morph;
    uint32_t size;
    uint32_t buffer;
    uint8_t loaded;
};

// The game's coat entry of "the remote" (index 1 of each array).
struct CoatAccess {
    static uintptr_t at(uintptr_t array) { return array + sr2::kStreakRemoteEntry * sizeof(uint32_t); }
    CoatCell read() const {
        return {readDword(at(sr2::kStreakCoatMorph)), readDword(at(sr2::kStreakCoatSize)), readDword(at(sr2::kStreakCoatBuffer)),
                readByte(sr2::kStreakCoatLoaded + sr2::kStreakRemoteEntry)};
    }
    void write(const CoatCell& cell) const {
        dwordField(at(sr2::kStreakCoatMorph), 0) = cell.morph;
        dwordField(at(sr2::kStreakCoatSize), 0) = cell.size;
        dwordField(at(sr2::kStreakCoatBuffer), 0) = cell.buffer;
        byteField(sr2::kStreakCoatLoaded, sr2::kStreakRemoteEntry) = cell.loaded;
    }
};

using Turn = diversion_swap::RemoteTurn<CoatCell, CoatAccess>;

diversion_rules::StreakMask g_mask;
diversion_rules::SwapTable<CoatCell> g_coats;
const CoatAccess g_access{};

void* g_messageOriginal = nullptr;
void* g_tickOriginal = nullptr;
void* g_endOriginal = nullptr;
void* g_initOriginal = nullptr;

bool tracing() { return activity_state::tracing(); }

void setGameByte() { byteField(sr2::kStreakActiveByte, 0) = g_mask.gameByte(); }

// The function a hook replaced, run with the registers it was entered with.
void runOriginal(void* original, GuestFrame* frame) { guest_call::call(reinterpret_cast<uintptr_t>(original), frame->regs); }

// The message handler cdecl(payload, ctx).
void runMessage(GuestFrame* frame) {
    guest_call::call(reinterpret_cast<uintptr_t>(g_messageOriginal), frame->regs, {frame->args[0], frame->args[1]});
}

bool hasCoat(const diversion_remotes::View& view, size_t index) {
    const partner_rules::Remote& remote = view.items()[index];
    return g_coats.valueOf(remote.slot, remote.human, g_access.read()).loaded != 0;
}

// DVS1: the streaking message names its sender: its bit goes on or off, and an off takes that remote's coat off.
void __cdecl streakMessage(GuestFrame* frame) {
    const uintptr_t sender = sender_scope::currentSender();
    diversion_remotes::View view;
    diversion_remotes::take(view);
    size_t index = partner_rules::kNone;
    for (size_t i = 0; sender && view.usable() && i < view.count(); ++i) {
        if (view.items()[i].member == sender) index = i;
    }
    if (index == partner_rules::kNone) {
        runMessage(frame);
        return;
    }
    const partner_rules::Remote& remote = view.items()[index];
    const bool on = readByte(frame->args[0]) == kStreakOn;
    if (tracing()) logger::write("trace: streaking: slot %u %s", remote.slot, on ? "on" : "off");
    if (on) {
        g_mask.set(remote.slot, true);
        runMessage(frame);
        return;
    }
    if (!g_mask.isOn(remote.slot)) return;
    {
        Turn turn(g_coats, g_access, view, index);
        byteField(sr2::kStreakActiveByte, 0) = kStreakOn;
        runMessage(frame);
    }
    g_mask.set(remote.slot, false);
    setGameByte();
    if (tracing()) logger::write("trace: streaking: coat for slot %u human 0x%X removed", remote.slot, static_cast<unsigned>(remote.human));
}

// DVS2: the tick loads and dresses the coat of every streaking remote. Without a remote human it is the game's own tick.
void __cdecl streakTick(GuestFrame* frame) {
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable() || view.humanCount() == 0) {
        runOriginal(g_tickOriginal, frame);
        return;
    }
    for (size_t i = 0; i < view.count(); ++i) {
        const partner_rules::Remote& remote = view.items()[i];
        if (!remote.human || !g_mask.isOn(remote.slot)) continue;
        const bool hadCoat = hasCoat(view, i);
        {
            Turn turn(g_coats, g_access, view, i);
            byteField(sr2::kStreakActiveByte, 0) = kStreakOn;
            runOriginal(g_tickOriginal, frame);
        }
        if (tracing() && !hadCoat && hasCoat(view, i)) {
            logger::write("trace: streaking: coat for slot %u human 0x%X loaded", remote.slot, static_cast<unsigned>(remote.human));
        }
    }
    setGameByte();
}

// DVS4: the end of the diversion takes the coat off every remote that streaks or still holds one.
void __cdecl streakEnd(GuestFrame* frame) {
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable() || view.humanCount() == 0) {
        runOriginal(g_endOriginal, frame);
        return;
    }
    for (size_t i = 0; i < view.count(); ++i) {
        const partner_rules::Remote& remote = view.items()[i];
        if (!remote.human || !(g_mask.isOn(remote.slot) || hasCoat(view, i))) continue;
        Turn turn(g_coats, g_access, view, i);
        byteField(sr2::kStreakActiveByte, 0) = kStreakOn;
        runOriginal(g_endOriginal, frame);
        if (tracing()) logger::write("trace: streaking: coat for slot %u human 0x%X removed", remote.slot, static_cast<unsigned>(remote.human));
    }
    g_mask.clear();
    setGameByte();
}

// DVS5: a new diversion starts without streakers.
void __cdecl streakInit(GuestFrame* frame) {
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_initOriginal), frame->regs);
    g_mask.clear();
    g_coats.forget();
    setGameByte();
}

// DVS6: the character composer asks for the coat buffer of a character that is not the local player's. The coat is the one of
// the remote the character was built for (its character instance has the id of the built record), loaded now when it is not.
uint32_t coatBufferOfBuilt(uintptr_t built, uint32_t emptyBuffer) {
    const uint32_t id = readDword(built + sr2::kCharacterIdOffset);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    size_t index = partner_rules::kNone;
    for (size_t i = 0; view.usable() && i < view.count(); ++i) {
        const uintptr_t human = view.items()[i].human;
        const uintptr_t instance = human ? readDword(human + sr2::kHumanCharacterOffset) : 0;
        if (instance && readDword(instance + sr2::kCharacterIdOffset) == id) index = i;
    }
    const uintptr_t human = index != partner_rules::kNone ? view.items()[index].human : partner_scope::partnerHuman();
    auto bufferWithLoad = [&]() {
        uint32_t buffer = g_access.read().buffer;
        if (buffer == emptyBuffer) {
            guest_call::call(sr2::kStreakCoatLoad, {}, {static_cast<uint32_t>(human)});
            buffer = g_access.read().buffer;
        }
        return buffer;
    };
    uint32_t buffer = 0;
    if (index == partner_rules::kNone) {
        buffer = bufferWithLoad();
    } else {
        Turn turn(g_coats, g_access, view, index);
        buffer = bufferWithLoad();
    }
    if (tracing()) {
        logger::write("trace: streaking: coat buffer for built character of human 0x%X", static_cast<unsigned>(human));
    }
    return buffer;
}

void __cdecl streakCoatBufferOfBuilt(BlockFrame* frame) {
    frame->regs.eax = coatBufferOfBuilt(readDword(frame->regs.ebp + sizeof(uint32_t)), frame->regs.edi);
}

PARTNER_WRAP_STUB(streakMessageStub, streakMessage, 0)
PARTNER_WRAP_STUB(streakTickStub, streakTick, 0)
PARTNER_WRAP_STUB(streakEndStub, streakEnd, 0)
PARTNER_WRAP_STUB(streakInitStub, streakInit, 0)
DIVERSION_BLOCK_STUB(streakCoatBufferStub, streakCoatBufferOfBuilt)

}  // namespace

namespace diversion_thunks {

partner_thunks::Binding bindStreak(Thunk thunk) {
    switch (thunk) {
        case Thunk::StreakMessage: return {reinterpret_cast<void*>(&streakMessageStub), &g_messageOriginal};
        case Thunk::StreakTick: return {reinterpret_cast<void*>(&streakTickStub), &g_tickOriginal};
        case Thunk::StreakEnd: return {reinterpret_cast<void*>(&streakEndStub), &g_endOriginal};
        case Thunk::StreakInit: return {reinterpret_cast<void*>(&streakInitStub), &g_initOriginal};
        case Thunk::StreakCoatBufferOfBuilt: return {reinterpret_cast<void*>(&streakCoatBufferStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace diversion_thunks
