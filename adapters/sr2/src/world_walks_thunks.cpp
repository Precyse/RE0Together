// Remote poses, the ambient spawn view, the follower bars, the activity warp gate, the crib garage door and the client name for 3 or
// more players (milestone 17 wave 3). The game's code keeps one answer for "the partner"; each thunk here asks every remote.
#include "world_walks_thunks.h"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <mutex>

#include "activity_rules.h"
#include "activity_state.h"
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "party_ai_rules.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "world_walks_rules.h"

namespace {

using diversion_remotes::View;
using partner_rules::Remote;
using partner_sites::Thunk;
using world_walks_rules::CompletionSender;
using world_walks_rules::FollowerBars;
using world_walks_rules::kNone;
using world_walks_rules::kNoPose;

constexpr size_t kPayloadArg = 0;  // a message handler is cdecl(payload, context)
constexpr size_t kContextArg = 1;
constexpr int kSpawnViewArguments = 1;
constexpr uint32_t kLowByte = 0xFF;  // the tests answer in al

bool g_trace = false;

// True when a thunk gives the game's own answer: a pairing that cannot be trusted, or fewer than two remote humans.
bool shippedPath(const View& view) { return party_ai_rules::shippedPath(view.usable(), view.humanCount()); }

// The index of the remote that owns `member`, kNone for any other.
size_t indexOfMember(const View& view, uintptr_t member) {
    for (size_t i = 0; member && i < view.count(); ++i) {
        if (view.items()[i].member == member) return i;
    }
    return kNone;
}

// ---- PD2, PD3: the CPR pose of each remote ----

void* g_poseOriginal = nullptr;
std::mutex g_poseMutex;
diversion_rules::SlotTable<int32_t> g_poses;      // the pose each remote's act 0x4B set
diversion_rules::SlotTable<int32_t> g_heldPoses;  // what PD2 answered last for each remote, for the trace

int32_t currentActionOf(uintptr_t human) {
    return static_cast<int32_t>(guest_call::call(sr2::kHumanCurrentAction, {.esi = static_cast<uint32_t>(human)}));
}

// The game's own answer: the one global pose, while 0x5F7DF0 says it holds.
int32_t shippedAmbulancePose() {
    const bool active = (guest_call::call(sr2::kAmbulancePoseActive, {}) & kLowByte) != 0;
    return active ? static_cast<int32_t>(readDword(sr2::kAmbulanceRemotePose)) : kNoPose;
}

void traceHeldPose(const Remote& remote, int32_t pose) {
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        if (g_heldPoses.valueOr(remote.slot, remote.human, kNoPose) == pose) return;
        g_heldPoses.set(remote.slot, remote.human, pose);
    }
    if (pose != kNoPose) {
        logger::write("trace: remote poses: human 0x%X (slot %u) held in ambulance pose 0x%X", static_cast<unsigned>(remote.human),
                      static_cast<unsigned>(remote.slot), static_cast<unsigned>(pose));
    } else {
        logger::write("trace: remote poses: human 0x%X (slot %u) pose released", static_cast<unsigned>(remote.human),
                      static_cast<unsigned>(remote.slot));
    }
}

// PD2: the anim state select's CPR pose of `human` (a remote copy): the pose stored for that remote while it has no action of its
// own, else kNoPose, which makes the select go on to the normal state.
int32_t __cdecl ambulancePoseOf(uintptr_t human) {
    View view;
    diversion_remotes::take(view);
    if (shippedPath(view)) return shippedAmbulancePose();
    const size_t at = view.indexOf(human);
    if (at == kNone) return kNoPose;
    const Remote& remote = view.items()[at];
    int32_t stored = kNoPose;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        stored = g_poses.valueOr(remote.slot, human, kNoPose);
    }
    const int32_t pose = world_walks_rules::ambulancePose(stored, currentActionOf(human));
    if (g_trace) traceHeldPose(remote, pose);
    return pose;
}

// PD3: act 0x4B, cdecl(payload, context). The game's handler plays the CPR anim on the sender and stores its anim id in the one
// global; the pose is kept for the sender as well.
void __cdecl recordAmbulancePose(GuestFrame* frame) {
    const uint32_t payload = frame->args[kPayloadArg];
    const uint32_t context = frame->args[kContextArg];
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_poseOriginal), frame->regs, {payload, context});
    View view;
    diversion_remotes::take(view);
    if (!view.usable()) return;
    const size_t at = indexOfMember(view, activity_state::senderOf(context));
    if (at == kNone || !view.items()[at].human) return;
    const Remote& sender = view.items()[at];
    const int32_t pose = static_cast<int32_t>(readDword(sr2::kAmbulanceRemotePose));
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        g_poses.set(sender.slot, sender.human, pose);
    }
    if (!g_trace) return;
    char who[world_walks_rules::kTextBytes];
    if (pose != kNoPose) {
        logger::write("trace: remote poses: %s ambulance pose 0x%X", world_walks_rules::slotText(sender.slot, sender.human, who),
                      static_cast<unsigned>(pose));
    } else {
        logger::write("trace: remote poses: %s ambulance pose cleared", world_walks_rules::slotText(sender.slot, sender.human, who));
    }
}

PARTNER_REG_STUB(ambulancePoseOfEdiStub, edi, ambulancePoseOf)
PARTNER_WRAP_STUB(recordAmbulancePoseStub, recordAmbulancePose, 0)

// ---- SW1: the ambient spawn view ----

rate_limit::RateLimit g_spawnLimit;

// The game's seen-by-or-near test (esi = point, cdecl(flag)) for `human`: its eax.
uint32_t spawnSeenBy(const GuestFrame* frame, uintptr_t human) {
    guest_call::Regs regs = frame->regs;
    regs.edi = static_cast<uint32_t>(human);
    guest_call::callWithRegs(sr2::kSpawnSeenByHuman, &regs, frame->args, kSpawnViewArguments);
    return regs.eax;
}

void traceSpawnRejection(const View& view, uintptr_t rejecting, uintptr_t walkFirst) {
    uint32_t triggers = 0;
    if (!g_trace || !g_spawnLimit.allow(GetTickCount64(), triggers)) return;
    logger::write("trace: spawn view: candidate rejected near remote human 0x%X (slot %u), not the first remote 0x%X (%u such)",
                  static_cast<unsigned>(rejecting), static_cast<unsigned>(party_ai_rules::slotOfHuman(view.snapshot.pairing, rejecting)),
                  static_cast<unsigned>(walkFirst), triggers);
}

// SW1: the candidate is rejected when any remote sees it or is near it. The remote the game's walk found is asked first, as the game
// does; the answer is the eax of the first test whose al is set, else of the last.
void __cdecl anyRemoteSeesSpawnPoint(GuestFrame* frame) {
    View view;
    diversion_remotes::take(view);
    const uintptr_t walkFirst = frame->regs.edi;
    uint32_t answer = spawnSeenBy(frame, walkFirst);
    if (!shippedPath(view)) {
        for (size_t i = 0; i < view.count() && !(answer & kLowByte); ++i) {
            const uintptr_t human = view.items()[i].human;
            if (!human || human == walkFirst) continue;
            answer = spawnSeenBy(frame, human);
            if (answer & kLowByte) traceSpawnRejection(view, human, walkFirst);
        }
    }
    frame->regs.eax = answer;
}

PARTNER_WRAP_STUB(anyRemoteSeesSpawnPointStub, anyRemoteSeesSpawnPoint, 0)

// ---- FB1: the follower bars of the local HUD ----

FollowerBars g_tracedBars{};
const uintptr_t g_followerBarsResume = sr2::kFollowerBarsResume;

void traceBars(const FollowerBars& bars, size_t remotes) {
    if (std::equal(std::begin(bars.bar), std::end(bars.bar), std::begin(g_tracedBars.bar))) return;
    g_tracedBars = bars;
    char text[world_walks_rules::kTextBytes];
    logger::write("trace: follower bars: %s for %u remotes", world_walks_rules::barsText(bars, text), static_cast<unsigned>(remotes));
}

// FB1: `frameEsp` is the esp of 0x797CD0 at the jump; the walk before it has written the partner and the three bars into its locals.
void __cdecl adjustFollowerBars(uint32_t frameEsp) {
    View view;
    diversion_remotes::take(view);
    if (shippedPath(view)) return;
    uint32_t& partner = dwordField(frameEsp, sr2::kFollowerBarsPartnerLocal);
    uint32_t* bars = &dwordField(frameEsp, sr2::kFollowerBarsFirstLocal);
    FollowerBars gameBars{partner, {bars[0], bars[1], bars[2]}};
    uintptr_t ordered[world_walks_rules::kMaxBars];
    const size_t remotes = world_walks_rules::orderedRemotes(view.items(), view.count(), view.defaultIndex, ordered);
    const FollowerBars result = world_walks_rules::followerBars(gameBars, ordered, remotes);
    partner = static_cast<uint32_t>(result.partner);
    for (size_t i = 0; i < world_walks_rules::kMaxBars; ++i) bars[i] = static_cast<uint32_t>(result.bar[i]);
    if (g_trace) traceBars(result, remotes);
}

// Replaces `xor esi, esi; mov [esp + 0x20], esi`, the join of every path of the walk.
void __declspec(naked) followerBarsStub() {
    __asm {
        pushad
        lea eax, [esp + 0x20]  // the function's esp at the jump
        push eax
        call adjustFollowerBars
        add esp, 4
        popad
        xor esi, esi
        mov [esp + 0x20], esi
        jmp dword ptr [g_followerBarsResume]
    }
}

// ---- AW1: the activity start warp ----

bool isBusy(const Remote& remote) {
    return (guest_call::call(sr2::kHumanIsBusy, {}, {static_cast<uint32_t>(remote.human)}) & kLowByte) != 0;
}

// AW1: the human whose state decides whether the start warp runs. For the all-players activities it is the default partner when that
// one is free, else the first free remote, so a down partner does not keep the others from being warped.
uintptr_t __cdecl freeRemoteForActivityWarp() {
    const uintptr_t partner = partner_scope::partnerHuman();
    View view;
    diversion_remotes::take(view);
    const int32_t type = activity_state::runningActivityType();
    if (shippedPath(view) || !activity_rules::isAllPlayersType(type)) return partner;
    const size_t at = diversion_rules::findFirst(view.items(), view.count(), view.defaultIndex,
                                                 [](const Remote& remote) { return !isBusy(remote); });
    if (at == kNone) return partner;
    const Remote& freeRemote = view.items()[at];
    if (g_trace && freeRemote.human != view.defaultHuman()) {
        logger::write("trace: activity warp: gate on remote 0x%X (slot %u), the default partner 0x%X is down or dead (activity type %d)",
                      static_cast<unsigned>(freeRemote.human), static_cast<unsigned>(freeRemote.slot),
                      static_cast<unsigned>(view.defaultHuman()), type);
    }
    return freeRemote.human;
}

// ---- GZ1: the crib garage door ----

uintptr_t g_waitingHuman = 0;  // the remote other than the default partner the door waits for, for the trace edges

// The offset of the first garage box the human stands in, 0 for none.
uintptr_t boxHolding(uintptr_t garage, uintptr_t human) {
    for (const uintptr_t offset : sr2::kGarageBoxOffsets) {
        const uint32_t box = readDword(garage + offset);
        const uint32_t point = static_cast<uint32_t>(human + sr2::kObjectPositionOffset);
        if (box != 0 && (guest_call::call(sr2::kPointInGarageBox, {.eax = box}, {point}) & kLowByte) != 0) return offset;
    }
    return 0;
}

void traceGarageDoor(const View& view, size_t at, uintptr_t box) {
    const uintptr_t waiting = at != kNone && at != view.defaultIndex ? view.items()[at].human : 0;
    if (waiting == g_waitingHuman) return;
    g_waitingHuman = waiting;
    if (waiting) {
        logger::write("trace: garage: remote human 0x%X (slot %u) in box 0x%02X, the door waits", static_cast<unsigned>(waiting),
                      static_cast<unsigned>(view.items()[at].slot), static_cast<unsigned>(box));
    } else {
        logger::write("trace: garage: door free of remotes");
    }
}

// GZ1: the human the garage door tests against its boxes: the first remote inside one, else the default partner. The game then
// repeats its own box tests on the answer.
uintptr_t __cdecl remoteInGarageBoxes() {
    FpuGuard guard;
    const uintptr_t partner = partner_scope::partnerHuman();
    View view;
    diversion_remotes::take(view);
    const uintptr_t garage = readDword(sr2::kCurrentGarage);
    if (shippedPath(view) || garage == 0) return partner;
    uintptr_t box = 0;
    const size_t at = diversion_rules::findFirst(view.items(), view.count(), view.defaultIndex, [&](const Remote& remote) {
        box = boxHolding(garage, remote.human);
        return box != 0;
    });
    if (g_trace) traceGarageDoor(view, at, box);
    return at == kNone ? partner : view.items()[at].human;
}

// ---- CN1, CN2: {CLIENT_NAME} ----

void* g_completionOriginal = nullptr;
std::mutex g_senderMutex;
CompletionSender g_completionSender;
rate_limit::OncePerKey g_namesSeen;

// CN1: misc 7, cdecl(payload, context): the sender's completion masks replace the host's. The sender is remembered once the game's
// handler has stored them.
void __cdecl rememberCompletionSender(GuestFrame* frame) {
    const uint32_t payload = frame->args[kPayloadArg];
    const uint32_t context = frame->args[kContextArg];
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_completionOriginal), frame->regs, {payload, context});
    View view;
    diversion_remotes::take(view);
    if (!view.usable()) return;
    const size_t at = indexOfMember(view, activity_state::senderOf(context));
    if (at == kNone || !view.items()[at].human) return;
    const Remote& sender = view.items()[at];
    {
        std::lock_guard<std::mutex> lock(g_senderMutex);
        g_completionSender = {sender.slot, sender.human};
    }
    if (!g_trace) return;
    char who[world_walks_rules::kTextBytes];
    logger::write("trace: client name: completion masks from %s", world_walks_rules::slotText(sender.slot, sender.human, who));
}

// CN2: the human `{CLIENT_NAME}` names, `caller` being the code that asked 0x7EDB60. Inside a message handler or a loop turn the
// getter already answers the right remote; outside one the text labels the completion masks, so it names whose they are.
uintptr_t __cdecl clientNameHuman(uintptr_t caller) {
    const uintptr_t partner = partner_scope::partnerHuman();
    if (partner_scope::overrideActive()) return partner;
    View view;
    diversion_remotes::take(view);
    if (shippedPath(view)) return partner;
    CompletionSender sender;
    {
        std::lock_guard<std::mutex> lock(g_senderMutex);
        sender = g_completionSender;
    }
    const size_t at = world_walks_rules::completionSenderIndex(view.items(), view.count(), sender);
    const uintptr_t named = at == kNone ? partner : view.items()[at].human;
    const uint64_t key = static_cast<uint64_t>(caller) << 32 | static_cast<uint32_t>(named);
    if (g_trace && g_namesSeen.firstTime(key)) {
        logger::write("trace: client name: names 0x%X (slot %u, %s), caller 0x%08X", static_cast<unsigned>(named),
                      static_cast<unsigned>(party_ai_rules::slotOfHuman(view.snapshot.pairing, named)),
                      at == kNone ? "default partner" : "completion sender", static_cast<unsigned>(caller));
    }
    return named;
}

PARTNER_WRAP_STUB(rememberCompletionSenderStub, rememberCompletionSender, 0)

// 0x7EDB60 pushed esi before the call, so its own caller is at [esp+8] in the thunk.
void __declspec(naked) clientNameHumanStub() {
    __asm {
        push dword ptr [esp + 8]
        call clientNameHuman
        add esp, 4
        ret
    }
}

}  // namespace

namespace world_walks_thunks {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::AmbulancePoseOfEdi: return {reinterpret_cast<void*>(&ambulancePoseOfEdiStub), nullptr};
        case Thunk::RecordAmbulancePose: return {reinterpret_cast<void*>(&recordAmbulancePoseStub), &g_poseOriginal};
        case Thunk::AnyRemoteSeesSpawnPoint: return {reinterpret_cast<void*>(&anyRemoteSeesSpawnPointStub), nullptr};
        case Thunk::FollowerBarsEachRemote: return {reinterpret_cast<void*>(&followerBarsStub), nullptr};
        case Thunk::FreeRemoteForActivityWarp: return {reinterpret_cast<void*>(&freeRemoteForActivityWarp), nullptr};
        case Thunk::RemoteInGarageBoxes: return {reinterpret_cast<void*>(&remoteInGarageBoxes), nullptr};
        case Thunk::RememberCompletionSender: return {reinterpret_cast<void*>(&rememberCompletionSenderStub), &g_completionOriginal};
        case Thunk::ClientNameHuman: return {reinterpret_cast<void*>(&clientNameHumanStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace world_walks_thunks
