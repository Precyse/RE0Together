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
#include "door_travel.h"
#include "event_rule.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "scene.h"

namespace {

using Clock = std::chrono::steady_clock;
using character_owner::Character;
using event_rule::TriggerKind;
using event_sync::EventStart;
using event_sync::EventStep;

using StartTriggerFunction = uintptr_t(__fastcall*)(void* self, void* edx, uint32_t key, uint32_t index, uint32_t type);
using DispatchFunction = int(__fastcall*)(void* self, void* edx, uintptr_t thread);
using UpdateFunction = void(__fastcall*)(void* self, void* edx);

constexpr size_t kMaxQueuedSteps = 4096;  // a follower this far behind is released to run on its own
constexpr uint8_t kNoSlot = 0xff;
// Some triggers fire every frame while they hold (a thread that only sets a flag): their lines are logged this rarely.
constexpr auto kRepeatLogInterval = std::chrono::seconds(5);

NetClient* g_net = nullptr;
StartTriggerFunction g_originalStart = nullptr;
DispatchFunction g_originalDispatch = nullptr;
UpdateFunction g_originalUpdate = nullptr;

enum class Role : uint8_t { None, Fired, Followed };

// Game thread: what this adapter knows about each sEventScript thread slot.
struct Slot {
    Role role = Role::None;
    uint16_t serial = 0;
    uint32_t key = 0;
    uint16_t index = 0;
    uint8_t type = 0;
    TriggerKind kind = TriggerKind::Local;
    bool announced = false;           // Fired: the peer was sent the start, so it gets the steps too
    Character firer = Character::Unknown;  // Followed: the peer's character, controlled while its ops run
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
std::map<uint16_t, std::deque<QueuedStep>> g_steps;  // guarded by g_mutex: per known serial (pending or followed)
std::set<uint16_t> g_overflowed;                     // guarded by g_mutex

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

uint32_t activeMask(uintptr_t script) {
    uint32_t mask = 0;
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        if (threadField<uint8_t>(slotAddress(script, i), game::kThreadActiveOffset)) mask |= 1u << i;
    }
    return mask;
}

bool peerInRoom() { return door_travel::peerPlace() == door_travel::PeerPlace::Here; }

// ---- firer ----

void sendStart(Slot& slot, uintptr_t thread) {
    const EventStart start{slot.key,  scene::current(), slot.serial, slot.index, static_cast<uint16_t>(pcOf(thread)),
                           slot.type, static_cast<uint8_t>(character_owner::localCharacter()),
                           static_cast<uint8_t>(slot.kind), 0};
    slot.announced = g_net->send(proto::kMsgEventStart, true, proto::kSlotAll, proto::bytesOf(start));
}

void record(const Slot& slot, uint32_t from, uint32_t to, uint8_t result) {
    if (slot.announced) {
        g_outgoing.push_back({slot.serial, static_cast<uint16_t>(from), static_cast<uint16_t>(to), result, 0});
    }
}

void track(uint8_t i, uintptr_t thread, uint8_t type, TriggerKind kind) {
    Slot& slot = g_slots[i];
    if (slot.role == Role::Fired) record(slot, 0, 0, event_rule::kResultKilled);
    slot = {};
    slot.role = Role::Fired;
    slot.serial = g_nextSerial++;
    slot.key = threadField<uint32_t>(thread, game::kThreadKeyOffset);
    slot.index = threadField<uint16_t>(thread, game::kThreadIndexOffset);
    slot.type = type;
    slot.kind = kind;
    const bool here = peerInRoom();
    if (here) sendStart(slot, thread);
    debug_stats::count(debug_stats::Counter::EventsFired);
    if (!logDue(Line::Fired, slot.key, slot.index)) return;
    logger::write("event_sync: fired scene 0x%02x trigger %u (type 0x%02x, %s) as %s at pc 0x%x, serial %u, peer %s",
                  slot.key, slot.index, type, event_rule::name(kind),
                  character_owner::name(character_owner::localCharacter()), pcOf(thread), slot.serial,
                  here ? (slot.announced ? "follows" : "not reached") : "elsewhere");
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
    if (i != kNoSlot) track(i, thread, static_cast<uint8_t>(type), kind);
    return thread;
}

// A fork (EventExec) started by a fired thread is fired too, as its own thread.
void trackForks(uintptr_t script, uint32_t before, const Slot& parent) {
    const uint32_t started = activeMask(script) & ~before;
    for (uint8_t i = 0; i < game::kScriptThreadCount; ++i) {
        if (started & 1u << i) track(i, slotAddress(script, i), event_sync::kForkType, parent.kind);
    }
}

int runFired(void* self, void* edx, uintptr_t thread, Slot& slot) {
    const uintptr_t script = eventScript();
    const uint32_t before = activeMask(script);
    const uint32_t pc = pcOf(thread);
    const auto result = static_cast<uint8_t>(g_originalDispatch(self, edx, thread));
    if (event_rule::finished(pc, pcOf(thread), result)) record(slot, pc, pcOf(thread), result);
    const Slot parent = slot;
    if (result == event_rule::kResultEnd) slot = {};
    trackForks(script, before, parent);
    return result;
}

// ---- follower ----

void forgetSerial(uint16_t serial) {
    std::lock_guard lock(g_mutex);
    g_steps.erase(serial);
    g_overflowed.erase(serial);
}

void endFollow(Slot& slot, const char* how) {
    if (logDue(Line::Ended, slot.key, slot.index)) {
        logger::write("event_sync: followed serial %u (scene 0x%02x trigger %u) %s: %u ops run, %u firer-only "
                      "skipped, %u waits skipped, %u branches taken from the firer, %u forced, %u jumps, lag up to "
                      "%lld ms", slot.serial, slot.key, slot.index, how, slot.ran, slot.skipped, slot.waitsSkipped,
                      slot.branches, slot.forced, slot.jumps, slot.maxLagMs);
    }
    forgetSerial(slot.serial);
    slot = {};
}

bool frontStep(uint16_t serial, QueuedStep& out) {
    std::lock_guard lock(g_mutex);
    const auto it = g_steps.find(serial);
    if (it == g_steps.end() || it->second.empty()) return false;
    out = it->second.front();
    return true;
}

void popStep(uint16_t serial) {
    std::lock_guard lock(g_mutex);
    const auto it = g_steps.find(serial);
    if (it != g_steps.end() && !it->second.empty()) it->second.pop_front();
}

// Runs the op with the firer's character as sPlayer's controlled one (and this machine's own as the partner).
int runAsFirer(void* self, void* edx, uintptr_t thread, Character firer) {
    const uintptr_t sPlayer = game::readPointer(game::kPlayerGlobal);
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    const uintptr_t firerObject = character_owner::find(firer);
    const bool swap = sPlayer && firerObject && firerObject != controlled;
    if (swap) {
        game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(firerObject));
        game::writeMemory(sPlayer + game::kPlayerPartnerOffset,
                          static_cast<uint32_t>(partner == firerObject ? controlled : partner));
    }
    const int result = g_originalDispatch(self, edx, thread);
    if (swap) {
        game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(controlled));
        game::writeMemory(sPlayer + game::kPlayerPartnerOffset, static_cast<uint32_t>(partner));
    }
    return result;
}

// Ends the op where the firer's ended and returns the firer's result.
int takeFirersStep(uintptr_t thread, Slot& slot, const QueuedStep& queued) {
    moveTo(thread, queued.step.to);
    slot.maxLagMs = std::max(slot.maxLagMs, msSince(queued.received));
    popStep(slot.serial);
    if (queued.step.result == event_rule::kResultEnd) endFollow(slot, "ended");
    return queued.step.result;
}

int runFollowed(void* self, void* edx, uintptr_t thread, Slot& slot) {
    QueuedStep queued;
    if (!frontStep(slot.serial, queued)) return event_rule::kResultYield;  // the firer has not finished this op yet
    if (queued.step.result == event_rule::kResultKilled) {
        popStep(slot.serial);
        endFollow(slot, "ended with the firer's thread (room reset)");
        return event_rule::kResultEnd;
    }
    const uint32_t pc = pcOf(thread);
    if (pc != queued.step.from) {
        logger::write("event_sync: followed serial %u at pc 0x%x, the firer's next op is at 0x%x, moved there",
                      slot.serial, pc, queued.step.from);
        moveTo(thread, queued.step.from);
        ++slot.jumps;
    }
    const uint16_t opcode = opcodeAt(thread);
    const event_rule::OpRole role = event_rule::roleOf(opcode);
    if (event_rule::beforeOp(true, role) == event_rule::Follow::Skip) {
        ++(role == event_rule::OpRole::Wait ? slot.waitsSkipped : slot.skipped);
        return takeFirersStep(thread, slot, queued);
    }
    const auto result = static_cast<uint8_t>(runAsFirer(self, edx, thread, slot.firer));
    const uint32_t after = pcOf(thread);
    switch (event_rule::afterRun(event_rule::finished(queued.step.from, after, result), msSince(queued.received))) {
        case event_rule::AfterRun::KeepRunning:
            return event_rule::kResultYield;
        case event_rule::AfterRun::Force:
            ++slot.forced;
            logger::write("event_sync: followed serial %u op %u at pc 0x%x still running %lld ms after the firer "
                          "finished it, ended here", slot.serial, opcode, queued.step.from, event_rule::kStallMs);
            break;
        case event_rule::AfterRun::TakeFirersBranch:
            ++slot.ran;
            if (after != queued.step.to && result != event_rule::kResultEnd) {
                ++slot.branches;
                logger::write("event_sync: followed serial %u op %u at pc 0x%x went to 0x%x here, took the firer's 0x%x",
                              slot.serial, opcode, queued.step.from, after, queued.step.to);
            }
            break;
    }
    return takeFirersStep(thread, slot, queued);
}

int __fastcall dispatchDetour(void* self, void* edx, uintptr_t thread) {
    const uint8_t i = slotOf(thread);
    if (i == kNoSlot) return g_originalDispatch(self, edx, thread);
    Slot& slot = g_slots[i];
    switch (slot.role) {
        case Role::Fired: return runFired(self, edx, thread, slot);
        case Role::Followed: return runFollowed(self, edx, thread, slot);
        case Role::None: break;
    }
    return g_originalDispatch(self, edx, thread);
}

void follow(uintptr_t thread, const EventStart& start) {
    if (pcOf(thread) != start.pc) moveTo(thread, start.pc);  // the peer arrived after the thread had begun
    Slot& slot = g_slots[slotOf(thread)];
    slot = {};
    slot.role = Role::Followed;
    slot.serial = start.serial;
    slot.key = start.key;
    slot.index = start.index;
    slot.type = start.type;
    slot.firer = static_cast<Character>(start.character);
    debug_stats::count(debug_stats::Counter::EventsFollowed);
    if (!logDue(Line::Following, start.key, start.index)) return;
    logger::write("event_sync: following scene 0x%02x trigger %u (type 0x%02x, %s) serial %u fired by %s on the peer, "
                  "from pc 0x%x", start.key, start.index, start.type,
                  event_rule::name(static_cast<TriggerKind>(start.kind)), start.serial,
                  character_owner::name(slot.firer), start.pc);
}

void dropStart(const EventStart& start, const char* why) {
    forgetSerial(start.serial);
    debug_stats::count(debug_stats::Counter::EventsDropped);
    if (!logDue(Line::Dropped, start.key, start.index)) return;
    logger::write("event_sync: peer's scene 0x%02x trigger %u serial %u not followed: %s", start.key, start.index,
                  start.serial, why);
}

// A start for a thread this machine already follows (the firer saw this player arrive again): go on from its pc.
bool resume(uintptr_t script, const EventStart& start) {
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        const Slot& slot = g_slots[i];
        if (slot.role != Role::Followed || slot.serial != start.serial) continue;
        moveTo(slotAddress(script, i), start.pc);
        logger::write("event_sync: followed serial %u resumed from the firer's pc 0x%x", start.serial, start.pc);
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
    } else if (g_slots[i].role == Role::Fired) {
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

bool overflowed(uint16_t serial) {
    std::lock_guard lock(g_mutex);
    return g_overflowed.count(serial) != 0;
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
        if (slot.role == Role::Fired && !same) {
            record(slot, 0, 0, event_rule::kResultKilled);
            slot = {};
        } else if (slot.role == Role::Followed && !same) {
            endFollow(slot, "gone (room reset)");
        } else if (slot.role == Role::Followed && overflowed(slot.serial)) {
            endFollow(slot, "fell too far behind, runs on by itself");
        }
    }
}

// The peer arrived in this room: it follows every fired thread from its current op. It went to another room: no more
// steps (its followers ended with its room). While either side loads, nothing changes.
void watchPeer(uintptr_t script) {
    const door_travel::PeerPlace place = door_travel::peerPlace();
    if (place == door_travel::PeerPlace::Unknown) return;
    const bool here = place == door_travel::PeerPlace::Here;
    if (here == g_peerWasHere) return;
    g_peerWasHere = here;
    for (size_t i = 0; i < game::kScriptThreadCount; ++i) {
        Slot& slot = g_slots[i];
        if (slot.role != Role::Fired) continue;
        if (!here) {
            slot.announced = false;
        } else if (!slot.announced) {
            sendStart(slot, slotAddress(script, i));
            logger::write("event_sync: peer arrived, serial %u (scene 0x%02x trigger %u) sent from pc 0x%x",
                          slot.serial, slot.key, slot.index, pcOf(slotAddress(script, i)));
        }
    }
}

void releaseAll() {
    for (Slot& slot : g_slots) {
        if (slot.role == Role::Followed) endFollow(slot, "released, the peer left; runs on by itself");
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
        std::lock_guard lock(g_mutex);
        g_pendingStarts.push_back({start, now});
        g_steps[start.serial].clear();
        g_overflowed.erase(start.serial);
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
        const auto it = g_steps.find(step.serial);
        if (it == g_steps.end() || g_overflowed.count(step.serial)) continue;  // not followed here
        if (it->second.size() >= kMaxQueuedSteps) {
            it->second.clear();
            g_overflowed.insert(step.serial);
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
