// DEATH STRANDING 2: missions and story stages. Every mission transition is a request (start 0x1413ef8b0, success
// 0x1413efc00, fail 0x1413efda0) queued on the mission system's controller and applied by one drain; a story stage
// changes through SectionManager::RequestSectionActive 0x1413bdad0, a story teleport through RequestChangeArea
// 0x140709cc0. The host polls the mission map and reports every
// state change whichever path made it, plus the sections it switches; a guest's own requests are vetoed and the host's
// events are replayed through the same request calls (their own conditions and side effects run), with the order
// cargo flag preset so the guest creates no cargo (docs/DS2_NOTES.md, "Story sync").
#include "ds2/story.h"

#include <atomic>
#include <windows.h>

#include <cstring>
#include <mutex>
#include <unordered_map>
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
constexpr uintptr_t kMissionId = 0x28, kMissionFlags = 0x24, kMissionState = 0x22;
constexpr uintptr_t kMissionMap = 0x08;  // Impl: {entries*, +0x0C capacity}; entry {u64 id, mission*, u32 hash}
constexpr uintptr_t kMapCapacity = 0x0C, kEntryMission = 0x08, kEntryHash = 0x10;
constexpr size_t kMapEntrySize = 0x18;
constexpr uint16_t kStateProgress = 20, kStateFailed = 30, kStateSuccess = 40;  // EDSMissionState
constexpr ULONGLONG kPollIntervalMs = 500;
constexpr uint32_t kNoRow = 0xFFFFFFFF;  // start request argument: no terminal list row
constexpr uint32_t kCargoPreparedFlag = 1u << 18;    // order cargo already prepared: the guest creates none
constexpr uintptr_t kSectionUuid = 0x10;             // the DSMissionSectionResource's GGUUID
constexpr uintptr_t kRequestChangeArea = 0x140709cc0;  // (unused, u16 EDSArea, bool, WorldTransform*, i32 constructionId, bool)
constexpr uint32_t kFlagFirstBool = 1, kFlagLastBool = 2;
constexpr uintptr_t kGameStateStack = 0x14623E338;  // GameModule: +0x340 count, +0x348 array of states, state +0x10 type
constexpr uintptr_t kStackCount = 0x340, kStackArray = 0x348, kStateType = 0x10;
constexpr uint32_t kFirstBlockingState = 5, kLastBlockingState = 19;  // menus and other states an area change must not cut
constexpr double kGuestOffsetMeters = 2.0;  // the guest lands beside the host's destination, not on it
constexpr size_t kMaxQueued = 256;

using Fn8 = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Fn8 g_changeArea = nullptr;
Fn8 g_requestStart = nullptr, g_requestSuccess = nullptr, g_requestFail = nullptr, g_requestSection = nullptr;

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
    logger::write("story: host event kind %u mission %llx", event.kind, static_cast<unsigned long long>(event.missionId));
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

uintptr_t missionImpl() {
    const uintptr_t system = decima::readPointer(ds2::at(kMissionSystemGlobal));
    return system ? decima::readPointer(system + kImplOffset) : 0;
}

// Every mission's id and state.
std::unordered_map<uint64_t, uint16_t> readMissions() {
    std::unordered_map<uint64_t, uint16_t> states;
    const uintptr_t impl = missionImpl();
    const uintptr_t entries = impl ? decima::readPointer(impl + kMissionMap) : 0;
    if (!entries) return states;
    const uint32_t capacity = ds2::field<uint32_t>(impl, kMissionMap + kMapCapacity);
    for (uint32_t i = 0; i < capacity; ++i) {
        const uintptr_t entry = entries + i * kMapEntrySize;
        const uintptr_t mission = decima::readPointer(entry + kEntryMission);
        if (!mission || ds2::field<uint32_t>(entry, kEntryHash) == 0) continue;
        states[ds2::field<uint64_t>(mission, kMissionId)] = ds2::field<uint16_t>(mission, kMissionState);
    }
    return states;
}

void reportState(uint64_t id, uint16_t state) {
    story_wire::Kind kind;
    if (state == kStateProgress) {
        kind = story_wire::Kind::MissionStart;
    } else if (state == kStateFailed) {
        kind = story_wire::Kind::MissionFail;
    } else if (state == kStateSuccess) {
        kind = story_wire::Kind::MissionSuccess;
    } else {
        return;
    }
    story_wire::Event event{};
    event.kind = static_cast<uint8_t>(kind);
    event.a = kind == story_wire::Kind::MissionStart ? kNoRow : 0;
    event.missionId = id;
    report(event);
}

std::atomic<bool> g_snapshotRequested{true};

// Host, simulation thread: reports each mission whose state changed since the last poll; a requested snapshot also
// reports every mission in progress.
void pollMissions() {
    static std::unordered_map<uint64_t, uint16_t> known;
    static ULONGLONG lastPoll = 0;
    static bool baselined = false;
    if (!g_host.load()) {
        known.clear();
        baselined = false;
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - lastPoll < kPollIntervalMs) return;
    lastPoll = now;
    const std::unordered_map<uint64_t, uint16_t> current = readMissions();
    if (current.empty()) return;
    const bool snapshot = g_snapshotRequested.exchange(false);
    for (const auto& [id, state] : current) {
        const auto before = known.find(id);
        const bool changed = baselined && (before == known.end() || before->second != state);
        if (changed || (snapshot && state == kStateProgress)) reportState(id, state);
    }
    known = current;
    baselined = true;
}

// A teleport the host makes is replayed on the guests; a guest's own is let through (voluntary fast travel stays
// per-player).
uint64_t changeAreaDetour(uintptr_t unused, uintptr_t area, uintptr_t first, uintptr_t transform, uintptr_t construction,
                          uintptr_t last, uintptr_t g, uintptr_t h) {
    if (g_host.load() && transform) {
        story_wire::Event event{};
        event.kind = static_cast<uint8_t>(story_wire::Kind::AreaChange);
        event.a = static_cast<uint16_t>(area);
        event.b = static_cast<int32_t>(construction);
        event.flags = (static_cast<uint8_t>(first) ? kFlagFirstBool : 0) | (static_cast<uint8_t>(last) ? kFlagLastBool : 0);
        decima::safeCopy(event.transform, transform, sizeof(event.transform));
        report(event);
    }
    return g_changeArea(unused, area, first, transform, construction, last, g, h);
}

bool blockingStateUp() {
    const uintptr_t module = decima::readPointer(ds2::at(kGameStateStack));
    const int32_t count = module ? ds2::field<int32_t>(module, kStackCount) : 0;
    const uintptr_t states = module ? decima::readPointer(module + kStackArray) : 0;
    for (int32_t i = 0; states && i < count; ++i) {
        const uintptr_t state = decima::readPointer(states + i * sizeof(uintptr_t));
        const uint32_t type = state ? ds2::field<uint32_t>(state, kStateType) : 0;
        if (type >= kFirstBlockingState && type <= kLastBlockingState) return true;
    }
    return false;
}

// False while a blocking state is up: the event waits for a later tick.
bool replayAreaChange(const story_wire::Event& event) {
    if (blockingStateUp()) return false;
    alignas(16) uint8_t transform[story_wire::kTransformSize];
    std::memcpy(transform, event.transform, sizeof(transform));
    double x;
    std::memcpy(&x, transform, sizeof(x));
    x += kGuestOffsetMeters;
    std::memcpy(transform, &x, sizeof(x));
    reinterpret_cast<uint64_t (*)(uintptr_t, uint16_t, bool, const void*, int32_t, bool)>(ds2::at(kRequestChangeArea))(
        0, static_cast<uint16_t>(event.a), event.flags & kFlagFirstBool, transform, event.b, event.flags & kFlagLastBool);
    logger::write("story: replayed an area change to %u", event.a);
    return true;
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
    const uint16_t state = ds2::field<uint16_t>(mission, kMissionState);
    const bool starts = kind == story_wire::Kind::MissionStart;
    if ((starts && state >= kStateProgress) || (!starts && state != kStateProgress)) return;
    if (starts) {
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
    std::vector<story_wire::Event> waiting;
    t_replaying = true;
    for (const story_wire::Event& event : events) {
        if (story_wire::isMission(static_cast<story_wire::Kind>(event.kind))) {
            replayMission(event);
        } else if (story_wire::isAreaChange(static_cast<story_wire::Kind>(event.kind))) {
            if (!replayAreaChange(event)) waiting.push_back(event);
        } else {
            replaySection(event);
        }
    }
    t_replaying = false;
    if (waiting.empty()) return;
    std::lock_guard lock(g_mutex);
    g_incoming.insert(g_incoming.begin(), waiting.begin(), waiting.end());
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
    hooks::install("story change area", ds2::at(kRequestChangeArea), reinterpret_cast<void*>(&changeAreaDetour),
                   reinterpret_cast<void**>(&g_changeArea));
    sim_tick::add(&pollMissions);
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

void requestStorySnapshot() { g_snapshotRequested = true; }

void replayStoryEvent(const story_wire::Event& event) {
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < kMaxQueued) g_incoming.push_back(event);
}

}  // namespace game
