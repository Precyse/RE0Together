#include "hud_remotes.h"

#include <algorithm>
#include <optional>
#include <set>

#include "guest_call.h"
#include "hud_rules.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_rules.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;
constexpr size_t kHumanArg = 0;
constexpr size_t kScaleArg = 1;
constexpr size_t kColorArg = 2;

bool g_trace = false;

// The remote player humans in list order with the session slot of the machine that owns each.
struct RemoteList {
    uintptr_t humans[kMaxRemotes];
    uint8_t slots[kMaxRemotes];
    size_t count;
};

RemoteList remoteList() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    RemoteList list{};
    list.count = snapshot.humanCount;
    for (size_t i = 0; i < list.count; ++i) {
        list.humans[i] = snapshot.humans[i].human;
        list.slots[i] = snapshot.humans[i].ownerSlot;
    }
    return list;
}

uint8_t slotOf(const RemoteList& list, uintptr_t human) {
    for (size_t i = 0; i < list.count; ++i) {
        if (list.humans[i] == human) return list.slots[i];
    }
    return partner_rules::kNoSlot;
}

// One pass of the game's partner arrow block over the remotes, one turn each. A turn runs with the partner getters answering that
// remote, so the block draws the arrow (or the blip) of that remote with the game's own instructions.
class BlockLoop {
public:
    BlockLoop(const char* name, bool countsFollowers) : name_(name), countsFollowers_(countsFollowers) {}

    // Opens the first turn; false when there is no remote, so the game's block is skipped.
    bool begin() {
        scope_.reset();
        list_ = remoteList();
        if (g_trace) traceIfChanged();
        if (!turns_.begin(list_.count)) return false;
        open();
        return true;
    }

    // Closes the turn that ran and opens the next; false after the last turn and when no pass is running.
    bool next() {
        scope_.reset();
        if (!turns_.next()) return false;
        open();
        return true;
    }

private:
    void open() {
        const uintptr_t human = list_.humans[turns_.current()];
        scope_.emplace(partner_scope::Target{human, remote_players::memberOfHuman(human)});
    }

    void traceIfChanged() {
        hud_rules::HumanText texts[kMaxRemotes];
        int32_t followers[kMaxRemotes];
        for (size_t i = 0; i < list_.count; ++i) {
            followers[i] = countsFollowers_ ? static_cast<int32_t>(readDword(list_.humans[i] + sr2::kHumanFollowerCountOffset))
                                            : hud_rules::kNoFollowerCount;
            texts[i] = {list_.humans[i], list_.slots[i], followers[i]};
        }
        const bool same = hud_rules::sameHumans(list_.humans, list_.count, traced_.humans, traced_.count) &&
                          std::equal(followers, followers + list_.count, traced_.followers);
        if (same) return;
        traced_.count = list_.count;
        std::copy(list_.humans, list_.humans + list_.count, traced_.humans);
        std::copy(followers, followers + list_.count, traced_.followers);
        logger::write("trace: hud: %s arrows for %u remote humans %s", name_, static_cast<unsigned>(list_.count),
                      hud_rules::humansLine(texts, list_.count).c_str());
    }

    struct Traced {
        uintptr_t humans[kMaxRemotes];
        int32_t followers[kMaxRemotes];
        size_t count;
    };

    const char* name_;
    bool countsFollowers_;
    hud_rules::RemoteTurns turns_;
    RemoteList list_{};
    std::optional<partner_scope::Override> scope_;
    Traced traced_{};
};

BlockLoop g_minimap("minimap", true);
BlockLoop g_pauseMap("pause map", false);

// Where the stubs of a block jump to next; set by the C++ side of the stub.
uintptr_t g_minimapResume = 0;
uintptr_t g_pauseMapResume = 0;

// What the end stubs run and where they go on: the instructions the exit stub replaced and the address after them.
const uintptr_t g_hudHalfScale = sr2::kHudHalfScale;
const uintptr_t g_minimapAfterExit = sr2::kMinimapArrowAfterExit;
const uintptr_t g_mapViewMode = sr2::kMapViewMode;
const uintptr_t g_pauseMapAfterExit = sr2::kPauseMapArrowAfterExit;

// HM3 replaced `fld dword [esp+0x10]; fmul qword [kHudHalfScale]`, the join of the minimap block.
void __declspec(naked) minimapEndStub() {
    __asm {
        fld dword ptr [esp + 0x10]
        push eax
        mov eax, g_hudHalfScale
        fmul qword ptr [eax]
        pop eax
        jmp dword ptr [g_minimapAfterExit]
    }
}

// HP2 replaced `mov eax, [kMapViewMode]`, the join of the pause map block.
void __declspec(naked) pauseMapEndStub() {
    __asm {
        mov eax, g_mapViewMode
        mov eax, [eax]
        jmp dword ptr [g_pauseMapAfterExit]
    }
}

void __cdecl minimapBegin() { g_minimapResume = g_minimap.begin() ? sr2::kMinimapArrowBody : sr2::kMinimapArrowExit; }
void __cdecl pauseMapBegin() { g_pauseMapResume = g_pauseMap.begin() ? sr2::kPauseMapArrowBody : sr2::kPauseMapArrowExit; }

void __cdecl minimapNext() {
    g_minimapResume = g_minimap.next() ? sr2::kMinimapArrowBody : reinterpret_cast<uintptr_t>(&minimapEndStub);
}

void __cdecl pauseMapNext() {
    g_pauseMapResume = g_pauseMap.next() ? sr2::kPauseMapArrowBody : reinterpret_cast<uintptr_t>(&pauseMapEndStub);
}

// Entered by a jmp from the middle of a function: all registers and the flags survive, then the game goes on at the address `impl`
// chose.
#define BLOCK_LOOP_STUB(name, impl, resume) \
    void __declspec(naked) name() { __asm pushfd __asm pushad __asm call impl __asm popad __asm popfd __asm jmp dword ptr [resume] }

BLOCK_LOOP_STUB(minimapBeginStub, minimapBegin, g_minimapResume)
BLOCK_LOOP_STUB(minimapNextStub, minimapNext, g_minimapResume)
BLOCK_LOOP_STUB(pauseMapBeginStub, pauseMapBegin, g_pauseMapResume)
BLOCK_LOOP_STUB(pauseMapNextStub, pauseMapNext, g_pauseMapResume)

// HM1: the follower blips of every remote, each with the game's own routine; of the given human when there is no remote.
void __cdecl eachRemoteFollowerBlips(GuestFrame* frame) {
    const RemoteList list = remoteList();
    const uint32_t* args = frame->args;
    if (list.count == 0) {
        guest_call::call(sr2::kFollowerBlipsDraw, {}, {args[kHumanArg], args[kScaleArg], args[kColorArg]});
        return;
    }
    for (size_t i = 0; i < list.count; ++i) {
        guest_call::call(sr2::kFollowerBlipsDraw, {}, {static_cast<uint32_t>(list.humans[i]), args[kScaleArg], args[kColorArg]});
    }
}

PARTNER_WRAP_STUB(eachRemoteFollowerBlipsStub, eachRemoteFollowerBlips, 0)

uintptr_t g_backPromptHolder = 0;

void traceBackPrompt(const RemoteList& list, uintptr_t holder) {
    if (holder == g_backPromptHolder) return;
    g_backPromptHolder = holder;
    if (holder) {
        logger::write("trace: hud: back prompt held by remote human 0x%X (slot %u), behaviour 0x%X", static_cast<unsigned>(holder),
                      static_cast<unsigned>(slotOf(list, holder)), static_cast<unsigned>(sr2::kBackPromptBehaviour));
    } else {
        logger::write("trace: hud: back prompt released");
    }
}

// HQ1: the human the back prompt tests for behaviour 0x22: the first remote that has it, else the default partner.
uintptr_t __cdecl remoteInBackPromptBehaviour() {
    const RemoteList list = remoteList();
    uint8_t behaviours[kMaxRemotes];
    for (size_t i = 0; i < list.count; ++i) behaviours[i] = readByte(list.humans[i] + sr2::kHumanBehaviourOffset);
    const size_t held = hud_rules::firstWithBehaviour(behaviours, list.count, sr2::kBackPromptBehaviour);
    if (g_trace) traceBackPrompt(list, held < list.count ? list.humans[held] : 0);
    return held < list.count ? list.humans[held] : partner_scope::partnerHuman();
}

uintptr_t g_lastNearest = 0;

// HQ2 and HQ3: the remote nearest to the vec3 at `point`.
uintptr_t __cdecl nearestForMapDistance(uint32_t point) {
    const uintptr_t nearest = partner_identity::nearest(point);
    if (g_trace && nearest != g_lastNearest) {
        g_lastNearest = nearest;
        if (nearest) {
            const RemoteList list = remoteList();
            logger::write("trace: hud: map distance nearest remote 0x%X (slot %u) of %u", static_cast<unsigned>(nearest),
                          static_cast<unsigned>(slotOf(list, nearest)), static_cast<unsigned>(list.count));
        }
    }
    return nearest;
}

// The point is [esp+0x18] of the call site, which is [esp+0x1C] in the thunk.
void __declspec(naked) nearestRemoteToStackPoint18Stub() {
    __asm {
        lea eax, [esp + 0x1C]
        push eax
        call nearestForMapDistance
        add esp, 4
        ret
    }
}

// HT1: the getter's own answer, and with 2 or more remotes and no message handler running the text that asked, once per caller.
uintptr_t __cdecl clientNameOutsideHandler(uintptr_t caller) {
    static std::set<uintptr_t> callers;
    const uintptr_t named = partner_scope::partnerHuman();
    if (!partner_scope::overrideActive() && remote_players::remoteHumanCount() >= 2 && callers.insert(caller).second) {
        const RemoteList list = remoteList();
        logger::write("trace: hud: CLIENT_NAME outside a message handler names 0x%X (slot %u), caller 0x%08X", static_cast<unsigned>(named),
                      static_cast<unsigned>(slotOf(list, named)), static_cast<unsigned>(caller));
    }
    return named;
}

// 0x7EDB60 pushed esi before the call, so its own caller is at [esp+8] in the thunk.
void __declspec(naked) traceClientNameStub() {
    __asm {
        push dword ptr [esp + 8]
        call clientNameOutsideHandler
        add esp, 4
        ret
    }
}

}  // namespace

namespace hud_remotes {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::EachRemoteFollowerBlips: return {reinterpret_cast<void*>(&eachRemoteFollowerBlipsStub), nullptr};
        case Thunk::MinimapArrowsBegin: return {reinterpret_cast<void*>(&minimapBeginStub), nullptr};
        case Thunk::MinimapArrowsNext: return {reinterpret_cast<void*>(&minimapNextStub), nullptr};
        case Thunk::PauseMapArrowsBegin: return {reinterpret_cast<void*>(&pauseMapBeginStub), nullptr};
        case Thunk::PauseMapArrowsNext: return {reinterpret_cast<void*>(&pauseMapNextStub), nullptr};
        case Thunk::RemoteInBackPromptBehaviour: return {reinterpret_cast<void*>(&remoteInBackPromptBehaviour), nullptr};
        case Thunk::NearestRemoteToStackPoint18: return {reinterpret_cast<void*>(&nearestRemoteToStackPoint18Stub), nullptr};
        case Thunk::TraceClientName: return {reinterpret_cast<void*>(&traceClientNameStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace hud_remotes
