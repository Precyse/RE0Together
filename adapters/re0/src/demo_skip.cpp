#include "demo_skip.h"

#include <atomic>
#include <mutex>

#include "crash_dump.h"
#include "debug_stats.h"
#include "demo_skip_rule.h"
#include "demo_skip_wire.h"
#include "frame_router.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "state_net.h"

namespace {

using debug_stats::Counter;
using demo_skip_rule::Press;
using demo_skip_wire::DemoSkip;

using SchedulerUpdate = void(__fastcall*)(void* self, void* edx);

SchedulerUpdate g_original = nullptr;
std::mutex g_mutex;
demo_skip_rule::Tracker g_tracker;  // guarded by g_mutex

constexpr uint32_t kPlayingFreshMs = 100;  // a demo counts as playing this long after the scheduler last updated in state 5
std::atomic<bool> g_playingSeen{false};
std::atomic<uint32_t> g_playingMs{0};      // the clock at the scheduler's last update in state 5
std::atomic<bool> g_localSkip{false};      // the rescue asked for the skip branch at the next state-5 update

uint32_t readU32(uintptr_t address) {
    uint32_t value = 0;
    game::readMemory(address, value);
    return value;
}

// The three inputs the engine's skip test reads (0x54b496 .. 0x54b4f9).
bool skipInputDown() {
    const uintptr_t pad = game::readPointer(game::kGamePadGlobal);
    const uintptr_t keyboard = game::readPointer(game::kKeyboardGlobal);
    const uintptr_t mouse = game::readPointer(game::kMouseGlobal);
    const uint32_t padWord =
        pad ? readU32(pad + readU32(pad + game::kPadActiveIndexOffset) * game::kPadDeviceStride + game::kPadTriggerWordOffset)
            : 0;
    uint8_t buttons = 0;
    if (mouse) game::readMemory(mouse + game::kMouseButtonsOffset, buttons);
    return demo_skip_rule::skipInput(padWord, keyboard ? readU32(keyboard + game::kKeyboardWordOffset) : 0, buttons,
                                     game::kPadSkipBit, game::kKeyboardSkipMask, game::kMouseSkipBit);
}

// What the engine's skip branch does in state 5 (0x54b50b .. 0x54b558): the fade-out starts with the duration of the
// demo's fade object, then the scheduler goes to state 6. False when the objects are not there.
bool takeSkipBranch(uintptr_t scheduler) {
    const uintptr_t source = game::readPointer(game::kDemoFadeDurationGlobal);
    const uintptr_t fade = game::fadeObject();
    float duration = 0.0f;
    if (!source || !fade || !game::readMemory(source + game::kDemoFadeDurationOffset, duration)) return false;
    __try {
        game::callThiscall<void>(game::kFadeStartFunction, reinterpret_cast<void*>(fade), duration);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "demo_skip: fade out")) {
        return false;
    }
    return game::writeMemory(scheduler + game::kDemoStateOffset, game::kDemoStateFadingOut);
}

void send(uint16_t type, const demo_skip_rule::Running& running) {
    const DemoSkip skip{running.demoId, running.serial, demo_skip_wire::flagsFor(running.serialMine), 0};
    if (state_net::send(type, true, proto::bytesOf(skip))) {
        debug_stats::count(Counter::DemoSkipsSent);
        logger::write("demo_skip: %s %u sent", type == demo_skip_wire::kMsgDemoSkip ? "skip" : "request", running.demoId);
    }
}

// The scheduler is in state 5 and a skip the peer sent is waiting for this demo: take the skip.
bool applyWaitingSkip(uintptr_t scheduler) {
    demo_skip_rule::Running running;
    {
        std::lock_guard lock(g_mutex);
        if (!g_tracker.take(pose_wire::clockMs())) return false;
        running = g_tracker.current();
    }
    if (!takeSkipBranch(scheduler)) {
        logger::write("demo_skip: skip %u not applied, the demo's fade is not there", running.demoId);
        return false;
    }
    debug_stats::count(Counter::DemoSkipsApplied);
    logger::write("demo_skip: skip %u applied", running.demoId);
    if (running.leads) send(demo_skip_wire::kMsgDemoSkip, running);
    return true;
}

// A press at the skip test while a co-op demo plays: the leader tells the follower, the follower asks the leader.
void reportPress() {
    if (!skipInputDown()) return;
    demo_skip_rule::Running running;
    bool playing = false;
    {
        std::lock_guard lock(g_mutex);
        playing = g_tracker.running();
        running = g_tracker.current();
    }
    switch (demo_skip_rule::onPress(playing, running.leads)) {
        case Press::SendSkip: send(demo_skip_wire::kMsgDemoSkip, running); break;
        case Press::SendRequest: send(demo_skip_wire::kMsgDemoSkipRequest, running); break;
        case Press::Ignore: break;
    }
}

void __fastcall schedulerDetour(void* self, void* edx) {
    const auto scheduler = reinterpret_cast<uintptr_t>(self);
    const uint32_t state = readU32(scheduler + game::kDemoStateOffset);
    if (state == game::kDemoStatePlaying) {
        g_playingMs = pose_wire::clockMs();
        g_playingSeen = true;
        if (g_localSkip.exchange(false) && takeSkipBranch(scheduler)) {
            logger::write("demo_skip: skip applied by the rescue");
            return;
        }
    } else {
        g_localSkip = false;
    }
    if (!net_pad::active() || state > game::kDemoStatePlaying) {
        std::lock_guard lock(g_mutex);
        g_tracker.end();
    } else if (state == game::kDemoStatePlaying) {
        if (applyWaitingSkip(scheduler)) return;
        reportPress();
    }
    g_original(self, edx);
}

// Net thread: the driving peer's DEMO_SKIP_REQUEST or DEMO_SKIP.
void onFrame(const GameFrame& frame) {
    if (static_cast<int>(frame.slot) != net_pad::peerSlot()) return;
    const auto skip = demo_skip_wire::decode(frame.payload);
    if (!skip) {
        logger::writeUnlessRepeated("demo_skip: frame from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_tracker.add(*skip, frame.type == demo_skip_wire::kMsgDemoSkip, pose_wire::clockMs());
}

}  // namespace

namespace demo_skip {

bool enable() {
    return frame_router::add(demo_skip_wire::kMsgDemoSkipRequest, frame_router::SenderPolicy::Any, onFrame) &&
           frame_router::add(demo_skip_wire::kMsgDemoSkip, frame_router::SenderPolicy::Any, onFrame) &&
           hooks::install("uEventDemoScheduler::update", game::kDemoSchedulerUpdateFunction,
                          reinterpret_cast<void*>(&schedulerDetour), reinterpret_cast<void**>(&g_original));
}

void uninstall() { hooks::remove(game::kDemoSchedulerUpdateFunction); }

bool skipLocal() {
    if (!g_playingSeen || pose_wire::clockMs() - g_playingMs > kPlayingFreshMs) return false;
    g_localSkip = true;
    return true;
}

void onDemoStart(uint16_t demoId, uint16_t serial, bool serialMine, bool leads) {
    if (demoId >= demo_skip_wire::kDemoIdLimit) return;
    const demo_skip_rule::Running running{demoId, serial, serialMine, leads};
    std::lock_guard lock(g_mutex);
    const demo_skip_rule::Running& now = g_tracker.current();
    if (g_tracker.running() && now.demoId == demoId && now.serial == serial && now.serialMine == serialMine) return;
    g_tracker.start(running);
    logger::write("demo_skip: demo %u of serial %u %s", demoId, serial, leads ? "led here" : "followed here");
}

}  // namespace demo_skip
