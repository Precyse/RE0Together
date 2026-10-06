// DEATH STRANDING 2: cutscenes watched together (docs/CONTRACT.md "Cutscenes", analysis/STORY_SYNC_PLAN.md).
// A story cutscene is a Sequence whose start worker 0x1403f30f0 the engine calls (and calls again every update while the
// start is still pending), so a detour that returns without calling the original holds it. The host holds its own start
// until every guest has its copy held too, then both are released together. A guest holds every shared cutscene its own
// story graph starts until the host has announced it; when the host announces one the guest has not started by itself,
// the guest starts the owning SequenceNetwork (or adopts the Sequence entity by UUID). The host's stop (0x1403f4750) is
// sent as END: a stop before the end frame (a skip) cuts the guests' copies short. The guest cannot skip.
#include "ds2/cutscene.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "cutscene_wire.h"
#include "decima/safe_read.h"
#include "ds2/cutscene_log.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "ds2/sequence_info.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kSequenceStart = 0x1403F30F0;  // (Sequence*)
constexpr uintptr_t kSequenceStop = 0x1403F4750;   // (Sequence*, int reason)
constexpr uintptr_t kSkipSelected = 0x14182E390;   // DSUISkipMenuFunction_OnSkip's body, no arguments
constexpr ULONGLONG kRetryGraceMs = 250;           // released but the engine did not call the start again: start it ourselves
constexpr ULONGLONG kOwnStartGraceMs = 1500;       // a guest waits this long for its own graph before starting the network
constexpr ULONGLONG kAdoptWaitMs = 2000;           // ... and this long after that before adopting the Sequence entity
constexpr ULONGLONG kLifetimeMs = 10 * 60 * 1000;  // a playback nobody ended is forgotten
constexpr int32_t kEndSlackFrames = 24;            // a stop this close to the end frame counts as the normal end
constexpr size_t kMaxPlaybacks = 32;

using StartFn = void (*)(uintptr_t);
using StopFn = void (*)(uintptr_t, int32_t);
using SkipFn = void (*)();
StartFn g_start = nullptr;
StopFn g_stop = nullptr;
SkipFn g_skip = nullptr;

enum class Phase { Announced, Held, Released, Playing };

struct Playback {
    cutscene_wire::Start start;
    Phase phase = Phase::Announced;
    uintptr_t sequence = 0;
    uint8_t ownEntity[cutscene_wire::kUuidSize] = {};  // this machine's Sequence entity (the guest's differs from the host's)
    ULONGLONG createdMs = 0;
    ULONGLONG releaseAtMs = 0;
    ULONGLONG networkStartedMs = 0;
    bool goEarly = false;       // guest: GO arrived before the Sequence was held
    bool adoptTried = false;
};

struct Orphan {  // guest: a shared Sequence its own graph started before the host announced it
    uintptr_t sequence;
    sequence_info::Info info;
};

std::atomic<bool> g_sync{false};
std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};
std::atomic<size_t> g_guests{0};
std::atomic<bool> g_playing{false};

std::mutex g_mutex;
std::vector<Playback> g_playbacks;
std::vector<Orphan> g_orphans;
uint32_t g_nextId = 0;
std::vector<cutscene_wire::Start> g_outStarts;
std::vector<cutscene_wire::End> g_outEnds;
std::vector<uint32_t> g_outReady;
std::vector<cutscene_wire::End> g_inEnds;

bool sameUuid(const uint8_t* a, const uint8_t* b) { return std::memcmp(a, b, cutscene_wire::kUuidSize) == 0; }

bool isZero(const uint8_t* uuid) {
    static const uint8_t zero[cutscene_wire::kUuidSize] = {};
    return sameUuid(uuid, zero);
}

Playback* bySequence(uintptr_t sequence) {
    for (Playback& p : g_playbacks) {
        if (p.sequence == sequence) return &p;
    }
    return nullptr;
}

Playback* byId(uint32_t id) {
    for (Playback& p : g_playbacks) {
        if (p.start.id == id) return &p;
    }
    return nullptr;
}

cutscene_wire::Start makeStart(uint32_t id, const sequence_info::Info& info) {
    cutscene_wire::Start start{};
    start.id = id;
    start.category = info.category;
    start.stopFrame = info.stopFrame;
    std::memcpy(start.resource, info.resource, sizeof(start.resource));
    std::memcpy(start.entity, info.entity, sizeof(start.entity));
    std::memcpy(start.network, info.network, sizeof(start.network));
    return start;
}

void updatePlaying() {
    g_playing = std::any_of(g_playbacks.begin(), g_playbacks.end(), [](const Playback& p) { return p.phase == Phase::Playing; });
}

// A Playback of the guest's that has no Sequence yet takes one that carries its resource UUID.
void bind(Playback& p, uintptr_t sequence, const sequence_info::Info& info, ULONGLONG now) {
    p.sequence = sequence;
    std::memcpy(p.ownEntity, info.entity, sizeof(p.ownEntity));
    p.phase = Phase::Held;
    g_outReady.push_back(p.start.id);
    if (p.goEarly) {
        p.phase = Phase::Released;
        p.releaseAtMs = now;
    }
    logger::write("cutscene: held cutscene %u (Sequence %p) for the host's go", p.start.id, reinterpret_cast<void*>(sequence));
}

Playback* waitingFor(const sequence_info::Info& info) {
    for (Playback& p : g_playbacks) {
        if (p.phase == Phase::Announced && sameUuid(p.start.resource, info.resource)) return &p;
    }
    return nullptr;
}

bool hostDecideHold(uintptr_t sequence, const sequence_info::Info& info, ULONGLONG now) {
    Playback* p = bySequence(sequence);
    if (!p) {
        if (g_guests == 0 || g_playbacks.size() >= kMaxPlaybacks) return false;
        Playback fresh;
        fresh.start = makeStart(++g_nextId, info);
        fresh.phase = Phase::Held;
        fresh.sequence = sequence;
        std::memcpy(fresh.ownEntity, info.entity, sizeof(fresh.ownEntity));
        fresh.createdMs = now;
        g_playbacks.push_back(fresh);
        g_outStarts.push_back(fresh.start);
        logger::write("cutscene: holding cutscene %u (category %u, %d frames) for %zu guest(s)", fresh.start.id, info.category,
                      info.stopFrame, g_guests.load());
        return true;
    }
    if (p->phase == Phase::Released && now >= p->releaseAtMs) {
        p->phase = Phase::Playing;
        updatePlaying();
        return false;
    }
    return p->phase == Phase::Held || p->phase == Phase::Released;
}

bool guestDecideHold(uintptr_t sequence, const sequence_info::Info& info, ULONGLONG now) {
    Playback* p = bySequence(sequence);
    if (!p) p = waitingFor(info);
    if (!p) {
        const bool known = std::any_of(g_orphans.begin(), g_orphans.end(), [&](const Orphan& o) { return o.sequence == sequence; });
        if (!known && g_orphans.size() < kMaxPlaybacks) {
            g_orphans.push_back({sequence, info});
            logger::write("cutscene: holding Sequence %p (category %u), the host has not announced it", reinterpret_cast<void*>(sequence), info.category);
        }
        return true;
    }
    if (p->phase == Phase::Announced) bind(*p, sequence, info, now);
    if (p->phase == Phase::Released && now >= p->releaseAtMs) {
        p->phase = Phase::Playing;
        updatePlaying();
        return false;
    }
    return p->phase != Phase::Playing;
}

void startDetour(uintptr_t sequence) {
    sequence_info::Info info;
    const bool shared = sequence_info::read(sequence, info) && cutscene_wire::isSharedCategory(info.category);
    bool hold = false;
    if (shared && g_sync && (g_host || g_guest)) {
        std::lock_guard lock(g_mutex);
        const ULONGLONG now = GetTickCount64();
        hold = g_host ? hostDecideHold(sequence, info, now) : guestDecideHold(sequence, info, now);
    }
    if (shared) cutscene_log::onStart(sequence, info, hold ? "held" : "started");
    if (!hold) g_start(sequence);
}

void stopDetour(uintptr_t sequence, int32_t reason) {
    if (g_sync) {
        std::lock_guard lock(g_mutex);
        Playback* p = bySequence(sequence);
        if (p && p->phase == Phase::Playing) {
            if (g_host && !sequence_info::stopRecorded(sequence)) {
                cutscene_wire::End end{};
                end.id = p->start.id;
                end.frame = sequence_info::frame(sequence);
                end.reason = static_cast<uint8_t>(std::min<int32_t>(reason, cutscene_wire::kStopReasonLast));
                g_outEnds.push_back(end);
                logger::write("cutscene: cutscene %u stopped at frame %d of %d (reason %d)", end.id, end.frame, p->start.stopFrame, reason);
            }
            g_playbacks.erase(g_playbacks.begin() + (p - g_playbacks.data()));
            updatePlaying();
        }
    }
    g_stop(sequence, reason);
}

void skipDetour() {
    if (g_sync && g_guest && g_playing) {
        logger::writeUnlessRepeated("cutscene: skip refused, only the host skips");
        return;
    }
    g_skip();
}

// The Sequence entity a guest's Playback names, when the engine has it and it is not started.
uintptr_t adoptableSequence(const Playback& p, sequence_info::Info& info) {
    const uintptr_t entity = ds2::entityByUuid(p.start.entity);
    return entity && sequence_info::isSequence(entity) && !sequence_info::started(entity) && sequence_info::read(entity, info) ? entity : 0;
}

struct Actions {
    std::vector<uintptr_t> forceStart;
    std::vector<std::pair<uintptr_t, int32_t>> forceStop;
    std::vector<std::array<uint8_t, cutscene_wire::kUuidSize>> startNetworks;
};

void dropGone(ULONGLONG now) {
    std::erase_if(g_orphans, [](const Orphan& o) { return !ds2::entityExists(o.info.entity); });
    std::erase_if(g_playbacks, [&](const Playback& p) {
        const bool gone = p.sequence && !ds2::entityExists(p.ownEntity);
        return gone || now - p.createdMs > kLifetimeMs;
    });
    updatePlaying();
}

void bindOrphans(ULONGLONG now) {
    for (auto it = g_orphans.begin(); it != g_orphans.end();) {
        Playback* p = waitingFor(it->info);
        if (p) {
            bind(*p, it->sequence, it->info, now);
            it = g_orphans.erase(it);
        } else {
            ++it;
        }
    }
}

// A guest's announced cutscene its own graph did not start: start the network, then adopt the entity.
void chaseAnnounced(ULONGLONG now, Actions& actions) {
    for (Playback& p : g_playbacks) {
        if (p.phase != Phase::Announced || now - p.createdMs < kOwnStartGraceMs) continue;
        if (!isZero(p.start.network) && p.networkStartedMs == 0) {
            p.networkStartedMs = now;
            std::array<uint8_t, cutscene_wire::kUuidSize> uuid;
            std::memcpy(uuid.data(), p.start.network, uuid.size());
            actions.startNetworks.push_back(uuid);
        } else if (!p.adoptTried && (isZero(p.start.network) || now - p.networkStartedMs >= kAdoptWaitMs)) {
            p.adoptTried = true;
            sequence_info::Info info;
            const uintptr_t sequence = adoptableSequence(p, info);
            if (sequence) {
                logger::write("cutscene: adopted Sequence %p of cutscene %u by its entity UUID", reinterpret_cast<void*>(sequence), p.start.id);
                bind(p, sequence, info, now);
            } else {
                logger::write("cutscene: cutscene %u could not be started here (network and entity not found)", p.start.id);
            }
        }
    }
}

void releaseStale(ULONGLONG now, Actions& actions) {
    for (Playback& p : g_playbacks) {
        if (p.phase != Phase::Released || now < p.releaseAtMs + kRetryGraceMs) continue;
        p.phase = Phase::Playing;
        actions.forceStart.push_back(p.sequence);
    }
    updatePlaying();
}

// The host's END: a copy that is still running is cut short when the host stopped before the end; a copy still held plays.
void applyEnds(ULONGLONG now, Actions& actions) {
    for (const cutscene_wire::End& end : g_inEnds) {
        Playback* p = byId(end.id);
        if (!p) continue;
        if (p->phase == Phase::Playing && cutscene_wire::endedEarly(end, p->start.stopFrame, kEndSlackFrames)) {
            actions.forceStop.emplace_back(p->sequence, cutscene_wire::kStopScripted);
        } else if (p->phase == Phase::Held) {
            p->phase = Phase::Released;
            p->releaseAtMs = now;
        }
        if (p->phase != Phase::Released) g_playbacks.erase(g_playbacks.begin() + (p - g_playbacks.data()));
    }
    g_inEnds.clear();
}

void tick() {
    if (!g_sync || !(g_host || g_guest)) return;
    const ULONGLONG now = GetTickCount64();
    Actions actions;
    {
        std::lock_guard lock(g_mutex);
        dropGone(now);
        if (g_guest) {
            bindOrphans(now);
            chaseAnnounced(now, actions);
            applyEnds(now, actions);
        }
        releaseStale(now, actions);
    }
    for (const auto& uuid : actions.startNetworks) {
        logger::write("cutscene: starting the SequenceNetwork the host's cutscene belongs to");
        sequence_info::startNetwork(uuid.data());
    }
    for (const uintptr_t sequence : actions.forceStart) g_start(sequence);
    for (const auto& [sequence, reason] : actions.forceStop) g_stop(sequence, reason);
}

}  // namespace

namespace cutscene {

void installEarly(bool sync) {
    g_sync = sync;
    hooks::install("cutscene sequence start", ds2::at(kSequenceStart), reinterpret_cast<void*>(&startDetour), reinterpret_cast<void**>(&g_start));
    hooks::install("cutscene sequence stop", ds2::at(kSequenceStop), reinterpret_cast<void*>(&stopDetour), reinterpret_cast<void**>(&g_stop));
    if (!sync) return;
    hooks::install("cutscene skip", ds2::at(kSkipSelected), reinterpret_cast<void*>(&skipDetour), reinterpret_cast<void**>(&g_skip));
    sim_tick::add(&tick, "cutscene sync", sim_tick::Gate::Gameplay);
}

}  // namespace cutscene

namespace game {

void setCutsceneRole(bool host, bool guest, size_t guestCount) {
    g_host = host;
    g_guest = guest;
    g_guests = guestCount;
    if (host || guest) return;
    std::lock_guard lock(g_mutex);
    g_playbacks.clear();
    g_orphans.clear();
    g_outStarts.clear();
    g_outEnds.clear();
    g_outReady.clear();
    g_inEnds.clear();
    updatePlaying();
}

std::vector<cutscene_wire::Start> takeCutsceneStarts() {
    std::lock_guard lock(g_mutex);
    std::vector<cutscene_wire::Start> out;
    out.swap(g_outStarts);
    return out;
}

void releaseCutscene(uint32_t id, uint32_t delayMs) {
    std::lock_guard lock(g_mutex);
    Playback* p = byId(id);
    if (!p || p->phase != Phase::Held) return;
    p->phase = Phase::Released;
    p->releaseAtMs = GetTickCount64() + delayMs;
}

std::vector<cutscene_wire::End> takeCutsceneEnds() {
    std::lock_guard lock(g_mutex);
    std::vector<cutscene_wire::End> out;
    out.swap(g_outEnds);
    return out;
}

void armCutscene(const cutscene_wire::Start& start) {
    std::lock_guard lock(g_mutex);
    if (byId(start.id) || g_playbacks.size() >= kMaxPlaybacks) return;
    Playback p;
    p.start = start;
    p.createdMs = GetTickCount64();
    g_playbacks.push_back(p);
}

std::vector<uint32_t> takeCutsceneReady() {
    std::lock_guard lock(g_mutex);
    std::vector<uint32_t> out;
    out.swap(g_outReady);
    return out;
}

void goCutscene(uint32_t id) {
    std::lock_guard lock(g_mutex);
    Playback* p = byId(id);
    if (!p) return;
    if (p->phase == Phase::Held) {
        p->phase = Phase::Released;
        p->releaseAtMs = GetTickCount64();
    } else if (p->phase == Phase::Announced) {
        p->goEarly = true;
    }
}

void endCutscene(const cutscene_wire::End& end) {
    std::lock_guard lock(g_mutex);
    if (g_inEnds.size() < kMaxPlaybacks) g_inEnds.push_back(end);
}

bool cutscenePlaying() { return g_playing; }

}  // namespace game
