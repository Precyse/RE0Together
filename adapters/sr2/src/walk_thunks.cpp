// Milestone 17 wave 3 thunks for the inline walks of the first remote (spec M17_W3_WALKS.md): the partner getter in place of a walk,
// the Fraud start block once per remote, the per-remote calls of the Fraud and Snatch ends, and the Snatch delivery stat that goes to
// the player who delivered. The Mayhem counters are mayhem_blocks.cpp.
#include <optional>

#include "activity_state.h"
#include "diversion_stubs.h"
#include "diversion_trace.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "hud_rules.h"
#include "log.h"
#include "memory.h"
#include "partner_rules.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "walk_rules.h"
#include "walk_thunks.h"

namespace {

using partner_sites::Thunk;

constexpr uintptr_t kCallRel32Bytes = 5;  // a CallBlock row calls its thunk with a 5 byte call
constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;
constexpr uint32_t kRagdollEndFlag = 0;

const uintptr_t g_resumeFraudRagdoll = sr2::kResumeFraudRagdoll;
const uintptr_t g_resumeSnatchCleanup = sr2::kResumeSnatchCleanup;
const uintptr_t g_fraudBlockAfter = sr2::kFraudStartBlockAfter;

bool tracing() { return activity_state::tracing(); }

// Runs fn(human) once per remote player human, with the getters answering that remote. Nothing runs without a remote: the callees
// test the human first, so a null human did nothing in the shipped game either.
template <typename Fn>
void eachHuman(Fn fn) {
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (human) fn(human);
    });
}

// InlineWalkGetter: the answer of the partner getter in place of the game's inline copy of it. Under a sender scope or an each-remote
// turn that is the remote of the scope, else the default partner (rule PIN included).
rate_limit::OncePerKey g_walkSeen;

uintptr_t __cdecl inlineWalkAnswer(uintptr_t returnAddress) {
    const uintptr_t human = partner_scope::partnerHuman();
    const uint64_t key = (static_cast<uint64_t>(returnAddress) << 32) | human;
    if (diversion_trace::firstTime(g_walkSeen, key)) {
        FpuGuard guard;
        logger::write("trace: walks: walk at 0x%X answers human 0x%X", static_cast<unsigned>(returnAddress - kCallRel32Bytes),
                      static_cast<unsigned>(human));
    }
    return human;
}

// The walk's return address is the thunk's own, so it is handed on as the argument.
void __declspec(naked) inlineWalkGetterStub() {
    __asm {
        push dword ptr [esp]
        call inlineWalkAnswer
        add esp, 4
        ret
    }
}

// The instruction DWF2 replaced, then the code after the Fraud start block.
void __declspec(naked) fraudBlockEndStub() {
    __asm {
        mov edx, [esp + 0xA58]
        jmp dword ptr [g_fraudBlockAfter]
    }
}

// DWF1, DWF2: the game's start block of Fraud for the first remote runs once per remote. A turn runs with the partner getters answering
// that remote and the block's remote slot (the game's [esp + 0x10]) holding it. The block ends by leaving a pointer in that slot; after
// the last turn it holds what the first turn left, as the game's block left it with one remote.
class FraudStartBlock {
public:
    // The start of the block: the address the game goes on at. The slot holds the first remote the game's walk found, 0 for none.
    uintptr_t begin(uint32_t& slot) {
        scope_.reset();
        count_ = slot ? remote_players::remoteHumans(humans_, kMaxRemotes) : 0;
        if (!turns_.begin(count_)) return sr2::kFraudStartBlockExit;
        open(slot);
        return sr2::kFraudStartBlockBody;
    }

    // The end of a turn: the next turn, or the end of the block. Without a pass running it is the end.
    uintptr_t next(uint32_t& slot) {
        scope_.reset();
        const bool running = turns_.active();
        if (running && turns_.current() == 0) firstTurnSlot_ = slot;
        if (turns_.next()) {
            open(slot);
            return sr2::kFraudStartBlockBody;
        }
        if (running) slot = firstTurnSlot_;
        return reinterpret_cast<uintptr_t>(&fraudBlockEndStub);
    }

private:
    void open(uint32_t& slot) {
        const uintptr_t human = humans_[turns_.current()];
        slot = static_cast<uint32_t>(human);
        scope_.emplace(partner_scope::Target{human, remote_players::memberOfHuman(human)});
        if (tracing()) {
            logger::write("trace: fraud: start block for remote 0x%X (turn %u of %u)", static_cast<unsigned>(human),
                          static_cast<unsigned>(turns_.current() + 1), static_cast<unsigned>(count_));
        }
    }

    hud_rules::RemoteTurns turns_;
    uintptr_t humans_[kMaxRemotes] = {};
    size_t count_ = 0;
    uint32_t firstTurnSlot_ = 0;
    std::optional<partner_scope::Override> scope_;
};

FraudStartBlock g_fraudBlock;
uintptr_t g_fraudBlockResume = 0;  // where the stubs of the block go on; set by their C++ side

void __cdecl fraudBlockBegin(JumpFrame* frame) {
    g_fraudBlockResume = g_fraudBlock.begin(*gameSlot(frame, sr2::kFraudBlockHumanSlot));
}

void __cdecl fraudBlockNext(JumpFrame* frame) {
    g_fraudBlockResume = g_fraudBlock.next(*gameSlot(frame, sr2::kFraudBlockHumanSlot));
}

DIVERSION_JUMP_STUB(fraudBlockBeginStub, fraudBlockBegin, g_fraudBlockResume)
DIVERSION_JUMP_STUB(fraudBlockNextStub, fraudBlockNext, g_fraudBlockResume)

// DWF3: the damage multiplier goes back to 1 for every remote (stdcall(human, &value); the value is the caller's).
void __cdecl damageResetEach(GuestFrame* frame) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kHumanDamageMultiplier, {}, {static_cast<uint32_t>(human), frame->args[1]});
        if (tracing()) logger::write("trace: fraud: damage multiplier of remote 0x%X reset", static_cast<unsigned>(human));
    });
}

PARTNER_WRAP_STUB(damageResetStub, damageResetEach, 8)

// DWF6: the ragdoll of every remote that is in one ends, as the game ends the local player's.
void __cdecl ragdollEndEach(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        if (!walk_rules::ragdollEnds(readByte(human + sr2::kHumanRagdollOffset), readByte(human + sr2::kHumanRagdollFlagsOffset))) return;
        guest_call::call(sr2::kRagdollEnd, {}, {static_cast<uint32_t>(human), kRagdollEndFlag});
        if (tracing()) logger::write("trace: fraud: ragdoll of remote 0x%X ended", static_cast<unsigned>(human));
    });
}

DIVERSION_JUMP_STUB(ragdollEndStub, ragdollEndEach, g_resumeFraudRagdoll)

// DWP1: the Snatch setup of every remote is undone at the end (the twin of DVP3).
void __cdecl snatchCleanupEach(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        guest_call::call(sr2::kSnatchSetupBoth, {.ecx = static_cast<uint32_t>(human)}, {sr2::kSnatchCleanupArg});
        guest_call::call(sr2::kSnatchSetupLook, {}, {static_cast<uint32_t>(human), sr2::kSnatchCleanupLookFlag});
        if (tracing()) logger::write("trace: snatch: cleanup for remote 0x%X", static_cast<unsigned>(human));
    });
}

DIVERSION_JUMP_STUB(snatchCleanupStub, snatchCleanupEach, g_resumeSnatchCleanup)

// DWP3, DWP4: the stat of a Snatch delivery. The game sends every remote stat to every member (0x6B4F20 ends with the broadcast
// 0x814740); the delivery's goes to the member of the player who delivered. The member is held for the length of the call, and only
// the broadcast inside that call reads it, so the other callers of 0x6B4F20 are not touched.
thread_local uintptr_t t_statTarget = 0;

class StatTarget {
public:
    explicit StatTarget(uintptr_t member) { t_statTarget = member; }
    ~StatTarget() { t_statTarget = 0; }
    StatTarget(const StatTarget&) = delete;
    StatTarget& operator=(const StatTarget&) = delete;
};

// ECX = the value, ESI = the stat id; the player who delivers is the delivery's own [esp + 0x8C], which the call did not move.
void __cdecl snatchStatToDeliverer(BlockFrame* frame) {
    const uintptr_t deliverer = *callerSlot(frame, sr2::kSnatchDelivererSlot);
    const uintptr_t member = remote_players::memberOfHuman(deliverer);
    if (member && tracing()) {
        logger::write("trace: snatch: delivery stat %u to remote 0x%X member 0x%X", frame->regs.esi, static_cast<unsigned>(deliverer),
                      static_cast<unsigned>(member));
    }
    const StatTarget target(member);
    guest_call::call(sr2::kRemoteStat, {.esi = frame->regs.esi, .ecx = frame->regs.ecx});
}

DIVERSION_BLOCK_STUB(snatchStatStub, snatchStatToDeliverer)

// The broadcast inside 0x6B4F20 (EDI = the session, cdecl(buffer, length), the caller pops) goes to the held member alone.
void __cdecl sendStatToTarget(GuestFrame* frame) {
    if (t_statTarget) {
        guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(t_statTarget)}, {frame->args[0], frame->args[1], 0, 0});
    } else {
        guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {frame->args[0], frame->args[1]});
    }
}

PARTNER_WRAP_STUB(sendStatStub, sendStatToTarget, 0)

}  // namespace

namespace walk_thunks {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::InlineWalkGetter: return {reinterpret_cast<void*>(&inlineWalkGetterStub), nullptr};
        case Thunk::FraudStartBlockBegin: return {reinterpret_cast<void*>(&fraudBlockBeginStub), nullptr};
        case Thunk::FraudStartBlockNext: return {reinterpret_cast<void*>(&fraudBlockNextStub), nullptr};
        case Thunk::EachRemoteDamageReset: return {reinterpret_cast<void*>(&damageResetStub), nullptr};
        case Thunk::EachRemoteRagdollEnd: return {reinterpret_cast<void*>(&ragdollEndStub), nullptr};
        case Thunk::EachRemoteSnatchCleanup: return {reinterpret_cast<void*>(&snatchCleanupStub), nullptr};
        case Thunk::SnatchStatToDeliverer: return {reinterpret_cast<void*>(&snatchStatStub), nullptr};
        case Thunk::SendStatToTarget: return {reinterpret_cast<void*>(&sendStatStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace walk_thunks
