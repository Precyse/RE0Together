#include "event_sync.h"

#include <intrin.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "door_sync.h"
#include "door_travel.h"
#include "event_rule.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "party_mode.h"
#include "protocol.h"
#include "scene.h"

namespace {

using Clock = std::chrono::steady_clock;
using character_owner::Character;
using event_rule::OpRole;
using event_rule::TriggerKind;
using event_sync::EventStart;
using event_sync::EventStep;

using StartTriggerFunction = uintptr_t(__fastcall*)(void* self, void* edx, uint32_t key, uint32_t index, uint32_t type);
using DispatchFunction = int(__fastcall*)(void* self, void* edx, uintptr_t thread);
using UpdateFunction = void(__fastcall*)(void* self, void* edx);
using OperandSizeFunction = int(__cdecl*)(const char* signature);

constexpr size_t kMaxQueuedSteps = 4096;  // a follower this far behind is released to run on its own
constexpr uint8_t kNoSlot = 0xff;
// Some triggers fire every frame while they hold (a thread that only sets a flag): their lines are logged this rarely.
constexpr auto kRepeatLogInterval = std::chrono::seconds(5);
constexpr uint32_t kSerialMineBit = 1u << 16;  // queue ids: serials this machine allocated

NetClient* g_net = nullptr;
StartTriggerFunction g_originalStart = nullptr;
DispatchFunction g_originalDispatch = nullptr;
UpdateFunction g_originalUpdate = nullptr;

enum class Role : uint8_t { None, Leads, Follows };

// Game thread: what this adapter knows about each sEventScript thread slot.
struct Slot {
    Role role = Role::None;
    uint16_t serial = 0;
    bool serialMine = false;  // this machine fired the thread and allocated its serial
    uint32_t key = 0;
    uint16_t index = 0;
    uint8_t type = 0;
    TriggerKind kind = TriggerKind::Local;
    bool announced = false;               // Leads: the peer has the thread, so it gets the steps too
    Character subject = Character::Unknown;  // the character the script treats as the player
    bool partnerFollows = true;           // no TraceOff in this thread yet
    uint32_t ran = 0, skipped = 0, waitsSkipped = 0, branches = 0, forced = 0, jumps = 0;
    int64_t maxLagMs = 0;
};

std::array<Slot, game::kScriptThreadCount> g_slots;
uint16_t g_nextSerial = 1;
bool g_peerWasHere = false;
std::vector<EventStep> g_outgoing;   // game thread: this frame's steps of announced threads
std::set<uint32_t> g_refusedLogged;  // game thread: (key << 16 | index) of refusals already logged in this room
uint16_t g_refusedScene = scene::kNone;

struct QueuedStep {
    EventStep step;
    Clock::time_point received;
};

struct PendingStart {
    EventStart start;
    Clock::time_point received;
};

std::mutex g_mutex;
std::vector<PendingStart> g_pendingStarts;           // guarded by g_mutex
std::map<uint32_t, std::deque<QueuedStep>> g_steps;  // guarded by g_mutex: per followed (or pending) queue id
std::set<uint32_t> g_overflowed;                     // guarded by g_mutex

uint32_t queueId(bool serialMine, uint16_t serial) { return (serialMine ? kSerialMineBit : 0) | serial; }
uint32_t queueId(const Slot& slot) { return queueId(slot.serialMine, slot.serial); }

int64_t msSince(Clock::time_point then) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - then).count();
}

enum class Line : uint8_t { Fired, Following, Ended, Dropped };

std::map<uint32_t, Clock::time_point> g_lastLogged;  // game thread: per line kind, script key and trigger index

// Whether this kind of line about this trigger is due (not logged in the last kRepeatLogInterval).
bool logDue(Line line, uint32_t key, uint16_t index) {
    constexpr uint32_t kKeyBits = 8;
    constexpr uint32_t kIndexBits = 16;
    constexpr uint32_t kKeyMask = (1u << kKeyBits) - 1;  // scene ids are below 0xaf
    const uint32_t id = static_cast<uint32_t>(line) << (kKeyBits + kIndexBits) | (key & kKeyMask) << kIndexBits | index;
    const auto now = Clock::now();
    auto [it, fresh] = g_lastLogged.try_emplace(id, now);
    if (fresh) return true;
    if (now - it->second < kRepeatLogInterval) return false;
    it->second = now;
    return true;
}

uintptr_t eventScript() { return game::readPointer(game::kEventScriptGlobal); }

uintptr_t slotAddress(uintptr_t script, size_t i) {
    return script + game::kScriptThreadsOffset + i * game::kScriptThreadSize;
}

uint8_t slotOf(uintptr_t thread) {
    const uintptr_t script = eventScript();
    const uintptr_t first = slotAddress(script, 0);
    if (!script || thread < first) return kNoSlot;
    const uintptr_t offset = thread - first;
    const size_t i = offset / game::kScriptThreadSize;
    return offset % game::kScriptThreadSize == 0 && i < game::kScriptThreadCount ? static_cast<uint8_t>(i) : kNoSlot;
}

template <class T>
T threadField(uintptr_t thread, uintptr_t offset) {
    T value{};
    game::readMemory(thread + offset, value);
    return value;
}

uint32_t pcOf(uintptr_t thread) { return threadField<uint32_t>(thread, game::kThreadPcOffset); }

void moveTo(uintptr_t thread, uint32_t pc) {
    game::writeMemory(thread + game::kThreadPcOffset, pc);
    game::writeMemory(thread + game::kThreadSubStateOffset, uint32_t{0});
}

// The big-endian opcode at the thread's pc.
uint16_t opcodeAt(uintptr_t thread) {
    const uintptr_t code = game::readPointer(thread + game::kThreadCodeOffset);
    std::array<uint8_t, 2> bytes{};
    if (!code || !game::readMemory(code + pcOf(thread), bytes)) return 0;
    return static_cast<uint16_t>(bytes[0] << 8 | bytes[1]);
}

// Bytes of the opcode and its operands, sized by the game's own signature reader.
uint32_t opSize(uint16_t opcode) {
    const auto signature = reinterpret_cast<const char*>(
        game::readPointer(game::kOpcodeTable + opcode * game::kOpcodeRowSize + game::kOpcodeSignatureOffset));
    const auto operands = reinterpret_cast<OperandSizeFunction>(game::kOperandSizeFunction)(signature);
    return game::kOpcodeSize + static_cast<uint32_t>(operands);
}

uint32_t activeMask(uintptr_t script) {
    uint32_t mask = 0;
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        if (threadField<uint8_t>(slotAddress(script, i), game::kThreadActiveOffset)) mask |= 1u << i;
    }
    return mask;
}

bool peerInRoom() { return door_travel::peerPlace() == door_travel::PeerPlace::Here; }

uint8_t serialFlags(const Slot& slot) { return slot.serialMine ? 0 : event_sync::kSerialIsReceivers; }

// ---- both roles ----

// Runs the op with the subject as sPlayer's controlled character (and the other one as the partner).
int runAsSubject(void* self, void* edx, uintptr_t thread, Character subject) {
    const uintptr_t sPlayer = game::readPointer(game::kPlayerGlobal);
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    const uintptr_t subjectObject = character_owner::find(subject);
    const bool swap = sPlayer && subjectObject && subjectObject != controlled;
    if (swap) {
        game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(subjectObject));
        game::writeMemory(sPlayer + game::kPlayerPartnerOffset,
                          static_cast<uint32_t>(partner == subjectObject ? controlled : partner));
    }
    const int result = g_originalDispatch(self, edx, thread);
    if (swap) {
        game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(controlled));
        game::writeMemory(sPlayer + game::kPlayerPartnerOffset, static_cast<uint32_t>(partner));
    }
    return result;
}

// TraceOff / TraceOn: the thread's doors leave the partner behind or not, and the host's party mode follows the script.
void setPartnerFollows(Slot& slot, bool follows) {
    slot.partnerFollows = follows;
    party_mode::setByScript(follows ? control_rule::PartyMode::Team : control_rule::PartyMode::LeaveBehind);
}

void forgetQueue(uint32_t id) {
    std::lock_guard lock(g_mutex);
    g_steps.erase(id);
    g_overflowed.erase(id);
}

void openQueue(uint32_t id) {
    std::lock_guard lock(g_mutex);
    g_steps[id].clear();
    g_overflowed.erase(id);
}

// ---- leader ----

void sendStart(Slot& slot, uintptr_t thread) {
    const uint8_t flags = serialFlags(slot) | (slot.partnerFollows ? event_sync::kPartnerFollows : 0);
    const EventStart start{slot.key,  scene::current(), slot.serial, slot.index, static_cast<uint16_t>(pcOf(thread)),
                           slot.type, static_cast<uint8_t>(slot.subject), static_cast<uint8_t>(slot.kind), flags};
    slot.announced = g_net->send(proto::kMsgEventStart, true, proto::kSlotAll, proto::bytesOf(start));
}

void record(const Slot& slot, uint32_t from, uint32_t to, uint8_t result) {
    if (slot.announced) {
        g_outgoing.push_back(
            {slot.serial, static_cast<uint16_t>(from), static_cast<uint16_t>(to), result, serialFlags(slot)});
    }
}

// The script made the peer's character its player: the peer leads from here, this machine follows.
void passLead(Slot& slot) {
    slot.role = Role::Follows;
    openQueue(queueId(slot));
    logger::write("event_sync: serial %u (scene 0x%02x trigger %u) switched to %s, its player leads from here",
                  slot.serial, slot.key, slot.index, character_owner::name(slot.subject));
}

void track(uint8_t i, uintptr_t thread, uint8_t type, TriggerKind kind, Character subject, bool partnerFollows) {
    Slot& slot = g_slots[i];
    if (slot.role == Role::Leads) record(slot, 0, 0, event_rule::kResultKilled);
    slot = {};
    slot.role = Role::Leads;
    slot.serial = g_nextSerial++;
    slot.serialMine = true;
    slot.key = threadField<uint32_t>(thread, game::kThreadKeyOffset);
    slot.index = threadField<uint16_t>(thread, game::kThreadIndexOffset);
    slot.type = type;
    slot.kind = kind;
    slot.subject = subject;
    slot.partnerFollows = partnerFollows;
    const bool here = peerInRoom();
    if (here) sendStart(slot, thread);
    debug_stats::count(debug_stats::Counter::EventsFired);
    if (!logDue(Line::Fired, slot.key, slot.index)) return;
    logger::write("event_sync: fired scene 0x%02x trigger %u (type 0x%02x, %s) as %s at pc 0x%x, serial %u, peer %s",
                  slot.key, slot.index, type, event_rule::name(kind), character_owner::name(subject), pcOf(thread),
                  slot.serial, here ? (slot.announced ? "follows" : "not reached") : "elsewhere");
}

uint32_t triggerParam2(void* script, uint32_t key, uint32_t index) {
    const auto entry = game::callThiscall<uintptr_t>(game::kTriggerEntryFunction, script, key, index);
    uint32_t p2 = 0;
    game::readMemory(entry + game::kTriggerEntryParam2Offset, p2);
    return p2;
}

void logRefusedOnce(uint32_t key, uint32_t index, uint32_t type) {
    if (scene::current() != g_refusedScene) {
        g_refusedLogged.clear();
        g_refusedScene = scene::current();
    }
    if (!g_refusedLogged.insert(key << 16 | index).second) return;
    debug_stats::count(debug_stats::Counter::EventsRefused);
    logger::write("event_sync: scene 0x%02x trigger %u (type 0x%02x) is the room authority's to fire, not fired here",
                  key, index, type);
}

uintptr_t __fastcall startDetour(void* self, void* edx, uint32_t key, uint32_t index, uint32_t type) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (!net_pad::active() || caller == game::kItemProbeStartReturn) return g_originalStart(self, edx, key, index, type);
    const TriggerKind kind = caller == game::kTriggerScanStartReturn
                                 ? event_rule::classify(type, triggerParam2(self, key, index))
                                 : TriggerKind::Local;
    if (!event_rule::mayFire(kind, peerInRoom(), door_travel::enemyAuthority())) {
        logRefusedOnce(key, index, type);
        return 0;
    }
    const uintptr_t thread = g_originalStart(self, edx, key, index, type);
    const uint8_t i = slotOf(thread);
    if (i != kNoSlot) {
        track(i, thread, static_cast<uint8_t>(type), kind, character_owner::localCharacter(),
              party_mode::current() == control_rule::PartyMode::Team);
    }
    return thread;
}

// A fork (EventExec) started by a led thread is led too, as its own thread with the parent's subject.
void trackForks(uintptr_t script, uint32_t before, const Slot& parent) {
    const uint32_t started = activeMask(script) & ~before;
    for (uint8_t i = 0; i < game::kScriptThreadCount; ++i) {
        if (started & 1u << i) {
            track(i, slotAddress(script, i), event_sync::kForkType, parent.kind, parent.subject, parent.partnerFollows);
        }
    }
}

// Switch and Follow ops are not run on either machine: the leader steps over them and records the step.
int stepOver(uintptr_t thread, const Slot& slot, uint32_t pc, uint16_t opcode) {
    const uint32_t next = pc + opSize(opcode);
    moveTo(thread, next);
    record(slot, pc, next, event_rule::kResultNext);
    return event_rule::kResultNext;
}

int runLeading(void* self, void* edx, uintptr_t thread, Slot& slot) {
    const uint32_t pc = pcOf(thread);
    const uint16_t opcode = opcodeAt(thread);
    switch (event_rule::roleOf(opcode)) {
        case OpRole::Switch: {
            slot.subject = character_owner::other(slot.subject);
            const int result = stepOver(thread, slot, pc, opcode);
            if (!event_rule::leadsHere(character_owner::isLocalOwned(slot.subject), slot.announced)) passLead(slot);
            return result;
        }
        case OpRole::Follow:
            setPartnerFollows(slot, event_rule::followsAfter(opcode));
            return stepOver(thread, slot, pc, opcode);
        case OpRole::Run: case OpRole::LeaderOnly: case OpRole::Wait:
            break;
    }
    const uintptr_t script = eventScript();
    const uint32_t before = activeMask(script);
    const bool doorAlone = event_rule::isDoor(opcode) && !slot.partnerFollows;
    if (doorAlone) door_sync::setScriptDoorAlone(true);
    const auto result = static_cast<uint8_t>(runAsSubject(self, edx, thread, slot.subject));
    if (doorAlone) door_sync::setScriptDoorAlone(false);
    if (event_rule::finished(pc, pcOf(thread), result)) record(slot, pc, pcOf(thread), result);
    const Slot parent = slot;
    if (result == event_rule::kResultEnd) slot = {};
    trackForks(script, before, parent);
    return result;
}

// ---- follower ----

void endFollow(Slot& slot, const char* how) {
    if (logDue(Line::Ended, slot.key, slot.index)) {
        logger::write("event_sync: followed serial %u (scene 0x%02x trigger %u) %s: %u ops run, %u leader-only "
                      "skipped, %u waits skipped, %u branches taken from the leader, %u forced, %u jumps, lag up to "
                      "%lld ms", slot.serial, slot.key, slot.index, how, slot.ran, slot.skipped, slot.waitsSkipped,
                      slot.branches, slot.forced, slot.jumps, slot.maxLagMs);
    }
    forgetQueue(queueId(slot));
    slot = {};
}

// The script made this player's character its player: this machine leads from here.
void takeLead(Slot& slot) {
    forgetQueue(queueId(slot));
    slot.role = Role::Leads;
    slot.announced = true;
    logger::write("event_sync: serial %u (scene 0x%02x trigger %u) switched to %s, leading it here", slot.serial,
                  slot.key, slot.index, character_owner::name(slot.subject));
}

bool frontStep(uint32_t id, QueuedStep& out) {
    std::lock_guard lock(g_mutex);
    const auto it = g_steps.find(id);
    if (it == g_steps.end() || it->second.empty()) return false;
    out = it->second.front();
    return true;
}

void popStep(uint32_t id) {
    std::lock_guard lock(g_mutex);
    const auto it = g_steps.find(id);
    if (it != g_steps.end() && !it->second.empty()) it->second.pop_front();
}

// Ends the op where the leader's ended and returns the leader's result.
int takeLeadersStep(uintptr_t thread, Slot& slot, const QueuedStep& queued) {
    moveTo(thread, queued.step.to);
    slot.maxLagMs = std::max(slot.maxLagMs, msSince(queued.received));
    popStep(queueId(slot));
    if (queued.step.result == event_rule::kResultEnd) endFollow(slot, "ended");
    return queued.step.result;
}

int runFollowing(void* self, void* edx, uintptr_t thread, Slot& slot) {
    QueuedStep queued;
    if (!frontStep(queueId(slot), queued)) return event_rule::kResultYield;  // the leader has not finished this op yet
    if (queued.step.result == event_rule::kResultKilled) {
        popStep(queueId(slot));
        endFollow(slot, "ended with the leader's thread (room reset)");
        return event_rule::kResultEnd;
    }
    const uint32_t pc = pcOf(thread);
    if (pc != queued.step.from) {
        logger::write("event_sync: followed serial %u at pc 0x%x, the leader's next op is at 0x%x, moved there",
                      slot.serial, pc, queued.step.from);
        moveTo(thread, queued.step.from);
        ++slot.jumps;
    }
    const uint16_t opcode = opcodeAt(thread);
    const OpRole role = event_rule::roleOf(opcode);
    if (role == OpRole::Switch) {
        slot.subject = character_owner::other(slot.subject);
        const int result = takeLeadersStep(thread, slot, queued);
        if (character_owner::isLocalOwned(slot.subject)) takeLead(slot);
        return result;
    }
    if (role == OpRole::Follow) {
        setPartnerFollows(slot, event_rule::followsAfter(opcode));
        return takeLeadersStep(thread, slot, queued);
    }
    if (event_rule::beforeOp(true, role) == event_rule::Follow::Skip) {
        ++(role == OpRole::Wait ? slot.waitsSkipped : slot.skipped);
        return takeLeadersStep(thread, slot, queued);
    }
    const auto result = static_cast<uint8_t>(runAsSubject(self, edx, thread, slot.subject));
    const uint32_t after = pcOf(thread);
    switch (event_rule::afterRun(event_rule::finished(queued.step.from, after, result), msSince(queued.received))) {
        case event_rule::AfterRun::KeepRunning:
            return event_rule::kResultYield;
        case event_rule::AfterRun::Force:
            ++slot.forced;
            logger::write("event_sync: followed serial %u op %u at pc 0x%x still running %lld ms after the leader "
                          "finished it, ended here", slot.serial, opcode, queued.step.from, event_rule::kStallMs);
            break;
        case event_rule::AfterRun::TakeLeadersBranch:
            ++slot.ran;
            if (after != queued.step.to && result != event_rule::kResultEnd) {
                ++slot.branches;
                logger::write("event_sync: followed serial %u op %u at pc 0x%x went to 0x%x here, took the leader's "
                              "0x%x", slot.serial, opcode, queued.step.from, after, queued.step.to);
            }
            break;
    }
    return takeLeadersStep(thread, slot, queued);
}

int __fastcall dispatchDetour(void* self, void* edx, uintptr_t thread) {
    const uint8_t i = slotOf(thread);
    if (i == kNoSlot) return g_originalDispatch(self, edx, thread);
    Slot& slot = g_slots[i];
    switch (slot.role) {
        case Role::Leads: return runLeading(self, edx, thread, slot);
        case Role::Follows: return runFollowing(self, edx, thread, slot);
        case Role::None: break;
    }
    return g_originalDispatch(self, edx, thread);
}

void follow(uintptr_t thread, const EventStart& start) {
    if (pcOf(thread) != start.pc) moveTo(thread, start.pc);  // the peer arrived after the thread had begun
    Slot& slot = g_slots[slotOf(thread)];
    slot = {};
    slot.role = Role::Follows;
    slot.serial = start.serial;
    slot.serialMine = (start.flags & event_sync::kSerialIsReceivers) != 0;
    slot.key = start.key;
    slot.index = start.index;
    slot.type = start.type;
    slot.subject = static_cast<Character>(start.character);
    slot.partnerFollows = (start.flags & event_sync::kPartnerFollows) != 0;
    debug_stats::count(debug_stats::Counter::EventsFollowed);
    if (logDue(Line::Following, start.key, start.index)) {
        logger::write("event_sync: following scene 0x%02x trigger %u (type 0x%02x, %s) serial %u led by %s on the "
                      "peer, from pc 0x%x", start.key, start.index, start.type,
                      event_rule::name(static_cast<TriggerKind>(start.kind)), start.serial,
                      character_owner::name(slot.subject), start.pc);
    }
    if (character_owner::isLocalOwned(slot.subject)) takeLead(slot);  // the peer handed its thread over on arrival
}

uint32_t queueId(const EventStart& start) {
    return queueId((start.flags & event_sync::kSerialIsReceivers) != 0, start.serial);
}

void dropStart(const EventStart& start, const char* why) {
    forgetQueue(queueId(start));
    debug_stats::count(debug_stats::Counter::EventsDropped);
    if (!logDue(Line::Dropped, start.key, start.index)) return;
    logger::write("event_sync: peer's scene 0x%02x trigger %u serial %u not followed: %s", start.key, start.index,
                  start.serial, why);
}

// A start for a thread this machine already follows (the leader saw this player arrive again): go on from its pc.
bool resume(uintptr_t script, const EventStart& start) {
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        const Slot& slot = g_slots[i];
        if (slot.role != Role::Follows || queueId(slot) != queueId(start)) continue;
        moveTo(slotAddress(script, i), start.pc);
        logger::write("event_sync: followed serial %u resumed from the leader's pc 0x%x", start.serial, start.pc);
        return true;
    }
    return false;
}

void applyStart(uintptr_t script, const EventStart& start) {
    if (resume(script, start)) return;
    void* self = reinterpret_cast<void*>(script);
    const bool fork = start.index == game::kForkThreadIndex;
    const uintptr_t thread = fork ? game::callThiscall<uintptr_t>(game::kStartForkThreadFunction, self, start.key,
                                                                  static_cast<uint32_t>(start.pc))
                                  : g_originalStart(self, nullptr, start.key, start.index, start.type);
    if (slotOf(thread) != kNoSlot) {
        follow(thread, start);
        return;
    }
    const uintptr_t running =
        fork ? 0 : game::callThiscall<uintptr_t>(game::kFindTriggerThreadFunction, self, start.key, start.index);
    const uint8_t i = slotOf(running);
    if (i == kNoSlot) {
        dropStart(start, "the script cannot start it here");
    } else if (g_slots[i].role == Role::Leads) {
        dropStart(start, "both machines fired it, each runs its own");
    } else {
        dropStart(start, "another followed thread of the same trigger runs here");
    }
}

void applyStarts(uintptr_t script) {
    std::vector<PendingStart> starts;
    {
        std::lock_guard lock(g_mutex);
        starts.swap(g_pendingStarts);
    }
    std::vector<PendingStart> waiting;
    for (const PendingStart& pending : starts) {
        switch (event_rule::startFate(pending.start.scene, scene::current(), game_state::doorActive(),
                                      msSince(pending.received))) {
            case event_rule::StartFate::Apply: applyStart(script, pending.start); break;
            case event_rule::StartFate::Wait: waiting.push_back(pending); break;
            case event_rule::StartFate::Drop: dropStart(pending.start, "this player is in another room"); break;
        }
    }
    if (waiting.empty()) return;
    std::lock_guard lock(g_mutex);
    g_pendingStarts.insert(g_pendingStarts.begin(), waiting.begin(), waiting.end());
}

// ---- per frame ----

bool overflowed(uint32_t id) {
    std::lock_guard lock(g_mutex);
    return g_overflowed.count(id) != 0;
}

// Slots whose thread ended or was replaced outside the dispatch (room reset, the item-use test, a reused slot).
void checkSlots(uintptr_t script) {
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        Slot& slot = g_slots[i];
        if (slot.role == Role::None) continue;
        const uintptr_t thread = slotAddress(script, i);
        const bool same = threadField<uint8_t>(thread, game::kThreadActiveOffset) &&
                          threadField<uint32_t>(thread, game::kThreadKeyOffset) == slot.key &&
                          threadField<uint16_t>(thread, game::kThreadIndexOffset) == slot.index;
        if (slot.role == Role::Leads && !same) {
            record(slot, 0, 0, event_rule::kResultKilled);
            slot = {};
        } else if (slot.role == Role::Follows && !same) {
            endFollow(slot, "gone (room reset)");
        } else if (slot.role == Role::Follows && overflowed(queueId(slot))) {
            endFollow(slot, "fell too far behind, runs on by itself");
        }
    }
}

// The peer arrived in this room: it gets every led thread from its current op (one whose script made the peer's
// character its player is handed over). It went to another room: no more steps (its followers ended with its room).
// While either side loads, nothing changes.
void watchPeer(uintptr_t script) {
    const door_travel::PeerPlace place = door_travel::peerPlace();
    if (place == door_travel::PeerPlace::Unknown) return;
    const bool here = place == door_travel::PeerPlace::Here;
    if (here == g_peerWasHere) return;
    g_peerWasHere = here;
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        Slot& slot = g_slots[i];
        if (slot.role != Role::Leads) continue;
        if (!here) {
            slot.announced = false;
            continue;
        }
        if (slot.announced) continue;
        sendStart(slot, slotAddress(script, i));
        logger::write("event_sync: peer arrived, serial %u (scene 0x%02x trigger %u) sent from pc 0x%x", slot.serial,
                      slot.key, slot.index, pcOf(slotAddress(script, i)));
        if (slot.announced && !character_owner::isLocalOwned(slot.subject)) passLead(slot);
    }
}

void releaseAll() {
    for (Slot& slot : g_slots) {
        if (slot.role == Role::Follows) endFollow(slot, "released, the peer left; runs on by itself");
        slot = {};
    }
    g_outgoing.clear();
    g_peerWasHere = false;
    std::lock_guard lock(g_mutex);
    g_pendingStarts.clear();
    g_steps.clear();
    g_overflowed.clear();
}

void flushSteps() {
    std::vector<uint8_t> payload(event_sync::kStepsHeaderSize + g_outgoing.size() * sizeof(EventStep));
    const auto count = static_cast<uint16_t>(g_outgoing.size());
    std::memcpy(payload.data(), &count, sizeof(count));
    std::memcpy(payload.data() + event_sync::kStepsHeaderSize, g_outgoing.data(), g_outgoing.size() * sizeof(EventStep));
    g_net->send(proto::kMsgEventSteps, true, proto::kSlotAll, payload);
    g_outgoing.clear();
}

bool anyState() {
    for (const Slot& slot : g_slots) {
        if (slot.role != Role::None) return true;
    }
    std::lock_guard lock(g_mutex);
    return !g_pendingStarts.empty() || !g_steps.empty();
}

void __fastcall updateDetour(void* self, void* edx) {
    const auto script = reinterpret_cast<uintptr_t>(self);
    if (net_pad::active()) {
        checkSlots(script);
        watchPeer(script);
        applyStarts(script);
    } else if (anyState()) {
        releaseAll();
    }
    g_originalUpdate(self, edx);
    if (!g_outgoing.empty()) flushSteps();
}

}  // namespace

namespace event_sync {

void onFrame(const GameFrame& frame) {
    if (frame.slot != net_pad::peerSlot()) return;
    const auto now = Clock::now();
    if (frame.type == proto::kMsgEventStart && frame.payload.size() == sizeof(EventStart)) {
        EventStart start;
        std::memcpy(&start, frame.payload.data(), sizeof(start));
        const uint32_t id = queueId(start);
        std::lock_guard lock(g_mutex);
        g_pendingStarts.push_back({start, now});
        g_steps[id].clear();
        g_overflowed.erase(id);
        return;
    }
    if (frame.type != proto::kMsgEventSteps || frame.payload.size() < kStepsHeaderSize) return;
    uint16_t count = 0;
    std::memcpy(&count, frame.payload.data(), sizeof(count));
    if (frame.payload.size() != kStepsHeaderSize + count * sizeof(EventStep)) return;
    std::lock_guard lock(g_mutex);
    for (uint16_t i = 0; i < count; ++i) {
        EventStep step;
        std::memcpy(&step, frame.payload.data() + kStepsHeaderSize + i * sizeof(EventStep), sizeof(step));
        const uint32_t id = queueId((step.flags & kSerialIsReceivers) != 0, step.serial);
        const auto it = g_steps.find(id);
        if (it == g_steps.end() || g_overflowed.count(id)) continue;  // not followed here
        if (it->second.size() >= kMaxQueuedSteps) {
            it->second.clear();
            g_overflowed.insert(id);
            continue;
        }
        it->second.push_back({step, now});
    }
}

bool enable(NetClient& net) {
    g_net = &net;
    return hooks::install("sEventScript::startTriggerThread", game::kStartTriggerThreadFunction,
                          reinterpret_cast<void*>(&startDetour), reinterpret_cast<void**>(&g_originalStart)) &&
           hooks::install("script::dispatch", game::kScriptDispatchFunction, reinterpret_cast<void*>(&dispatchDetour),
                          reinterpret_cast<void**>(&g_originalDispatch)) &&
           hooks::install("sEventScript::update", game::kEventScriptUpdateFunction,
                          reinterpret_cast<void*>(&updateDetour), reinterpret_cast<void**>(&g_originalUpdate));
}

void uninstall() {
    hooks::remove(game::kEventScriptUpdateFunction);
    hooks::remove(game::kScriptDispatchFunction);
    hooks::remove(game::kStartTriggerThreadFunction);
}

}  // namespace event_sync
