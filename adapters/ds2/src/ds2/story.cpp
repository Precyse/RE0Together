// DEATH STRANDING 2: missions and story stages. Every mission transition is a request (start 0x1413ef8b0, success
// 0x1413efc00, fail 0x1413efda0) queued on the mission system's controller and applied by one drain through the
// appliers StartMission 0x141395cc0, CompleteMission 0x141396730 and FailMission 0x1413973c0; a story stage changes
// through SectionManager::RequestSectionActive 0x1413bdad0. The host reports what its appliers did and which sections
// switched; a guest's own requests are vetoed and the host's events are replayed through the same request calls
// (their own conditions and side effects run), with the order cargo flag preset so the guest creates no cargo
// (docs/DS2_NOTES.md, "Story sync").
#include "ds2/story.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "story_wire.h"

namespace {

constexpr uintptr_t kMissionSystemGlobal = 0x14623EB30;  // DSMissionSystem; +0x10 = Impl, Impl +0x210 = request controller
constexpr uintptr_t kSectionManagerGlobal = 0x14623EB40;
constexpr uintptr_t kImplOffset = 0x10, kControllerOffset = 0x210;
constexpr uintptr_t kMissionById = 0x1413a0dd0;     // (DSMissionSystem*, u64 id) -> mission
constexpr uintptr_t kSectionByUuid = 0x1413be3e0;   // (SectionManager*, const GGUUID*) -> section
constexpr uintptr_t kRequestStart = 0x1413ef8b0;    // (controller, mission, const {u32, i32}*, const GGUUID* section, bool reserve)
constexpr uintptr_t kRequestSuccess = 0x1413efc00;  // (controller, mission, flag)
constexpr uintptr_t kRequestFail = 0x1413efda0;     // (controller, mission, reason)
constexpr uintptr_t kRequestSection = 0x1413bdad0;  // (SectionManager*, section, bool active)
constexpr uintptr_t kApplyStart = 0x141395cc0;      // (controller, mission, request)
constexpr uintptr_t kApplySuccess = 0x141396730;
constexpr uintptr_t kApplyFail = 0x1413973c0;
constexpr uintptr_t kMissionId = 0x28, kMissionFlags = 0x24;
constexpr uint32_t kCargoPreparedFlag = 1u << 18;    // order cargo already prepared: the guest creates none
constexpr uintptr_t kRequestA = 10, kRequestB = 14;  // the 18-byte request {u64 id, u16 kind, u32 a, u32 b}
constexpr uintptr_t kSectionUuid = 0x10;             // the section's GGUUID (to be checked live)
constexpr size_t kMaxQueued = 256;

using Fn8 = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Fn8 g_requestStart = nullptr, g_requestSuccess = nullptr, g_requestFail = nullptr, g_requestSection = nullptr;
Fn8 g_applyStart = nullptr, g_applySuccess = nullptr, g_applyFail = nullptr;

std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};
thread_local bool t_replaying = false;  // a host event is being replayed: its requests are let through

std::mutex g_mutex;
std::vector<story_wire::Event> g_outgoing;  // host: to send
std::vector<story_wire::Event> g_incoming;  // guest: to replay on the simulation thread

uintptr_t controller() {
    const uintptr_t system = decima::readPointer(ds2::at(kMissionSystemGlobal));
    const uintptr_t impl = system ? decima::readPointer(system + kImplOffset) : 0;
    return impl ? decima::readPointer(impl + kControllerOffset) : 0;
}

void report(const story_wire::Event& event) {
    std::lock_guard lock(g_mutex);
    if (g_outgoing.size() < kMaxQueued) g_outgoing.push_back(event);
}

story_wire::Event missionEvent(story_wire::Kind kind, uintptr_t mission, uint32_t a, int32_t b) {
    story_wire::Event event{};
    event.kind = static_cast<uint8_t>(kind);
    event.a = a;
    event.b = b;
    decima::safeRead(mission + kMissionId, event.missionId);
    return event;
}

// Guest: the game's own story requests are refused, except the ones replaying the host.
bool vetoed() { return g_guest.load() && !t_replaying; }

uint64_t requestStartDetour(uintptr_t c, uintptr_t mission, uintptr_t args, uintptr_t section, uintptr_t reserve,
                            uintptr_t f, uintptr_t g, uintptr_t h) {
    if (vetoed()) return 0;
    return g_requestStart(c, mission, args, section, reserve, f, g, h);
}

uint64_t requestSuccessDetour(uintptr_t c, uintptr_t mission, uintptr_t flag, uintptr_t d, uintptr_t e, uintptr_t f,
                              uintptr_t g, uintptr_t h) {
    if (vetoed()) return 0;
    return g_requestSuccess(c, mission, flag, d, e, f, g, h);
}

uint64_t requestFailDetour(uintptr_t c, uintptr_t mission, uintptr_t reason, uintptr_t d, uintptr_t e, uintptr_t f,
                           uintptr_t g, uintptr_t h) {
    if (vetoed()) return 0;
    return g_requestFail(c, mission, reason, d, e, f, g, h);
}

uint64_t requestSectionDetour(uintptr_t manager, uintptr_t section, uintptr_t active, uintptr_t d, uintptr_t e,
                              uintptr_t f, uintptr_t g, uintptr_t h) {
    if (vetoed()) return 0;
    if (g_host.load() && section) {
        story_wire::Event event{};
        event.kind = static_cast<uint8_t>(static_cast<uint8_t>(active) ? story_wire::Kind::SectionActive
                                                                        : story_wire::Kind::SectionInactive);
        decima::safeCopy(event.section, section + kSectionUuid, sizeof(event.section));
        report(event);
    }
    return g_requestSection(manager, section, active, d, e, f, g, h);
}

// The appliers run only from the drain, after the request's own conditions passed: the host reports what really happened.
uint64_t applyStartDetour(uintptr_t c, uintptr_t mission, uintptr_t request, uintptr_t d, uintptr_t e, uintptr_t f,
                          uintptr_t g, uintptr_t h) {
    const uint64_t result = g_applyStart(c, mission, request, d, e, f, g, h);
    if (g_host.load()) {
        uint32_t a = 0;
        int32_t b = 0;
        decima::safeRead(request + kRequestA, a);
        decima::safeRead(request + kRequestB, b);
        report(missionEvent(story_wire::Kind::MissionStart, mission, a, b));
    }
    return result;
}

uint64_t applySuccessDetour(uintptr_t c, uintptr_t mission, uintptr_t request, uintptr_t d, uintptr_t e, uintptr_t f,
                            uintptr_t g, uintptr_t h) {
    const uint64_t result = g_applySuccess(c, mission, request, d, e, f, g, h);
    if (g_host.load()) report(missionEvent(story_wire::Kind::MissionSuccess, mission, 0, 0));
    return result;
}

uint64_t applyFailDetour(uintptr_t c, uintptr_t mission, uintptr_t request, uintptr_t d, uintptr_t e, uintptr_t f,
                         uintptr_t g, uintptr_t h) {
    const uint64_t result = g_applyFail(c, mission, request, d, e, f, g, h);
    if (g_host.load()) report(missionEvent(story_wire::Kind::MissionFail, mission, 0, 0));
    return result;
}

void replayMission(const story_wire::Event& event) {
    const auto kind = static_cast<story_wire::Kind>(event.kind);
    const uintptr_t system = decima::readPointer(ds2::at(kMissionSystemGlobal));
    const uintptr_t mission =
        system ? reinterpret_cast<uintptr_t (*)(uintptr_t, uint64_t)>(ds2::at(kMissionById))(system, event.missionId) : 0;
    const uintptr_t c = controller();
    if (!mission || !c) {
        logger::write("story: mission %llx not found here, event dropped", static_cast<unsigned long long>(event.missionId));
        return;
    }
    if (kind == story_wire::Kind::MissionStart) {
        ds2::field<uint32_t>(mission, kMissionFlags) |= kCargoPreparedFlag;
        struct StartArgs {
            uint32_t a;
            int32_t b;
        } args{event.a, event.b};
        static const uint8_t noSection[story_wire::kUuidSize] = {};
        reinterpret_cast<uint64_t (*)(uintptr_t, uintptr_t, const void*, const void*, bool)>(ds2::at(kRequestStart))(
            c, mission, &args, noSection, false);
        logger::write("story: replayed the start of mission %llx", static_cast<unsigned long long>(event.missionId));
    } else if (kind == story_wire::Kind::MissionSuccess) {
        reinterpret_cast<uint64_t (*)(uintptr_t, uintptr_t, uint32_t)>(ds2::at(kRequestSuccess))(c, mission, event.a);
        logger::write("story: replayed the success of mission %llx", static_cast<unsigned long long>(event.missionId));
    } else {
        reinterpret_cast<uint64_t (*)(uintptr_t, uintptr_t, uint32_t)>(ds2::at(kRequestFail))(c, mission, event.a);
        logger::write("story: replayed the failure of mission %llx", static_cast<unsigned long long>(event.missionId));
    }
}

void replaySection(const story_wire::Event& event) {
    const uintptr_t manager = decima::readPointer(ds2::at(kSectionManagerGlobal));
    alignas(16) uint8_t uuid[story_wire::kUuidSize];
    std::memcpy(uuid, event.section, sizeof(uuid));
    const uintptr_t section =
        manager ? reinterpret_cast<uintptr_t (*)(uintptr_t, const void*)>(ds2::at(kSectionByUuid))(manager, uuid) : 0;
    if (!section) {
        logger::write("story: a section of the host's story is not found here, event dropped");
        return;
    }
    const bool active = static_cast<story_wire::Kind>(event.kind) == story_wire::Kind::SectionActive;
    reinterpret_cast<uint64_t (*)(uintptr_t, uintptr_t, bool)>(ds2::at(kRequestSection))(manager, section, active);
    logger::write("story: replayed a section %s", active ? "activation" : "deactivation");
}

// Guest, simulation thread, ahead of the engine's object update.
void applyIncoming() {
    std::vector<story_wire::Event> events;
    {
        std::lock_guard lock(g_mutex);
        if (g_incoming.empty()) return;
        events.swap(g_incoming);
    }
    t_replaying = true;
    for (const story_wire::Event& event : events) {
        if (story_wire::isMission(static_cast<story_wire::Kind>(event.kind))) {
            replayMission(event);
        } else {
            replaySection(event);
        }
    }
    t_replaying = false;
}

}  // namespace

namespace story {

void installEarly() {
    hooks::install("story request start", ds2::at(kRequestStart), reinterpret_cast<void*>(&requestStartDetour),
                   reinterpret_cast<void**>(&g_requestStart));
    hooks::install("story request success", ds2::at(kRequestSuccess), reinterpret_cast<void*>(&requestSuccessDetour),
                   reinterpret_cast<void**>(&g_requestSuccess));
    hooks::install("story request fail", ds2::at(kRequestFail), reinterpret_cast<void*>(&requestFailDetour),
                   reinterpret_cast<void**>(&g_requestFail));
    hooks::install("story request section", ds2::at(kRequestSection), reinterpret_cast<void*>(&requestSectionDetour),
                   reinterpret_cast<void**>(&g_requestSection));
    hooks::install("story apply start", ds2::at(kApplyStart), reinterpret_cast<void*>(&applyStartDetour),
                   reinterpret_cast<void**>(&g_applyStart));
    hooks::install("story apply success", ds2::at(kApplySuccess), reinterpret_cast<void*>(&applySuccessDetour),
                   reinterpret_cast<void**>(&g_applySuccess));
    hooks::install("story apply fail", ds2::at(kApplyFail), reinterpret_cast<void*>(&applyFailDetour),
                   reinterpret_cast<void**>(&g_applyFail));
    sim_tick::add(&applyIncoming);
}

}  // namespace story

namespace game {

void setStoryRole(bool host, bool guest) {
    g_host = host;
    g_guest = guest;
    if (!host) {
        std::lock_guard lock(g_mutex);
        g_outgoing.clear();
    }
}

std::vector<story_wire::Event> takeStoryEvents() {
    std::lock_guard lock(g_mutex);
    std::vector<story_wire::Event> out;
    out.swap(g_outgoing);
    return out;
}

void replayStoryEvent(const story_wire::Event& event) {
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < kMaxQueued) g_incoming.push_back(event);
}

}  // namespace game
