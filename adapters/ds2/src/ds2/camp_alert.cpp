// DEATH STRANDING 2: the alert phase of enemy camps. The camp manager (global 0x14623F348: +0x128 count, +0x130 an array
// of pointers to camp records) holds per camp its locator UUID (record +0xD0) and the camp object (record +0x100); the
// camp object has the phase (+0x110) and an array of member pointers (+0xD8, count +0xD0) with their own phase (+0xF8)
// and alert gauge (+0xFC). The host reads the phases on the simulation tick; a guest sets the same phase on its own camp
// the way SetForceAlertCP 0x141babda0 does (docs/DS2_NOTES.md, "Camp alert").
#include "ds2/camp_alert.h"

#include <windows.h>

#include <cstring>
#include <mutex>
#include <vector>

#include "camp_wire.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kCampManagerGlobal = 0x14623F348;
constexpr uintptr_t kCampCount = 0x128, kCampArray = 0x130;
constexpr uintptr_t kRecordUuid = 0xD0, kRecordObject = 0x100;
constexpr uintptr_t kObjectPhase = 0x110, kObjectMemberCount = 0xD0, kObjectMembers = 0xD8;
constexpr uintptr_t kMemberPhase = 0xF8, kMemberGauge = 0xFC;
constexpr uintptr_t kForceAlert = 0x141babda0;  // DsSneakingNpcCommand SetForceAlertCP(const GGUUID*)
constexpr size_t kUpdateSlot = 38;              // the camp's virtual at vtable +0x130, called with (camp, 0.0f)
constexpr ULONGLONG kPollMs = 500;
constexpr int32_t kMaxCampCount = 256;
constexpr int32_t kMaxMembers = 256;

std::mutex g_mutex;
bool g_sharing = false;
bool g_sendAll = false;
std::vector<camp_wire::CampPhase> g_known;  // simulation thread: the phases seen at the last poll
std::vector<camp_wire::CampPhase> g_outgoing;
std::vector<camp_wire::CampPhase> g_incoming;

struct Camp {
    camp_wire::CampPhase phase;
    uintptr_t object;
};

std::vector<Camp> readCamps() {
    std::vector<Camp> camps;
    const uintptr_t manager = decima::readPointer(ds2::at(kCampManagerGlobal));
    const int32_t count = manager ? ds2::field<int32_t>(manager, kCampCount) : 0;
    const uintptr_t array = manager ? decima::readPointer(manager + kCampArray) : 0;
    for (int32_t i = 0; array && i < count && i < kMaxCampCount; ++i) {
        const uintptr_t record = decima::readPointer(array + i * sizeof(uintptr_t));
        const uintptr_t object = record ? decima::readPointer(record + kRecordObject) : 0;
        if (!object) continue;
        Camp camp{};
        decima::safeCopy(camp.phase.uuid, record + kRecordUuid, camp_wire::kUuidSize);
        camp.phase.phase = ds2::field<int32_t>(object, kObjectPhase);
        camp.object = object;
        camps.push_back(camp);
    }
    return camps;
}

bool sameUuid(const camp_wire::CampPhase& a, const camp_wire::CampPhase& b) {
    return std::memcmp(a.uuid, b.uuid, camp_wire::kUuidSize) == 0;
}

void hostTick() {
    const std::vector<Camp> camps = readCamps();
    std::lock_guard lock(g_mutex);
    const bool all = g_sendAll;
    g_sendAll = false;
    for (const Camp& camp : camps) {
        bool changed = true;
        for (const camp_wire::CampPhase& known : g_known) {
            if (!sameUuid(known, camp.phase)) continue;
            changed = known.phase != camp.phase.phase;
            if (changed) logger::write("camp_alert: camp phase %d -> %d, CAMP_ALERT queued", known.phase, camp.phase.phase);
        }
        if ((changed || all) && g_outgoing.size() < camp_wire::kMaxCamps) g_outgoing.push_back(camp.phase);
    }
    if (camps.size() != g_known.size()) logger::write("camp_alert: %zu camps", camps.size());
    g_known.clear();
    for (const Camp& camp : camps) g_known.push_back(camp.phase);
}

void setPhase(const Camp& camp, int32_t phase) {
    alignas(16) uint8_t uuid[camp_wire::kUuidSize];
    std::memcpy(uuid, camp.phase.uuid, sizeof(uuid));
    __try {
        if (phase == camp_wire::kPhaseAlert) {
            reinterpret_cast<void (*)(const void*)>(ds2::at(kForceAlert))(uuid);
            return;
        }
        ds2::field<int32_t>(camp.object, kObjectPhase) = phase;
        const int32_t members = ds2::field<int32_t>(camp.object, kObjectMemberCount);
        const uintptr_t array = decima::readPointer(camp.object + kObjectMembers);
        for (int32_t i = 0; array && i < members && i < kMaxMembers; ++i) {
            const uintptr_t member = decima::readPointer(array + i * sizeof(uintptr_t));
            if (!member) continue;
            ds2::field<int32_t>(member, kMemberPhase) = phase;
            ds2::field<float>(member, kMemberGauge) = 0.0f;
        }
        const uintptr_t vtable = decima::readPointer(camp.object);
        const uintptr_t update = vtable ? decima::readPointer(vtable + kUpdateSlot * sizeof(uintptr_t)) : 0;
        if (update) reinterpret_cast<void (*)(uintptr_t, float)>(update)(camp.object, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logger::write("camp_alert: the engine faulted setting a camp to phase %d", phase);
    }
}

void guestTick() {
    std::vector<camp_wire::CampPhase> wanted;
    {
        std::lock_guard lock(g_mutex);
        wanted.swap(g_incoming);
    }
    if (wanted.empty()) return;
    for (const camp_wire::CampPhase& want : wanted) {
        for (const Camp& camp : readCamps()) {
            if (!sameUuid(camp.phase, want) || camp.phase.phase == want.phase) continue;
            logger::write("camp_alert: camp phase %d -> %d", camp.phase.phase, want.phase);
            setPhase(camp, want.phase);
        }
    }
}

void tick() {
    static ULONGLONG last = 0;
    bool sharing;
    {
        std::lock_guard lock(g_mutex);
        sharing = g_sharing;
    }
    if (!sharing) {
        guestTick();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - last < kPollMs) return;
    last = now;
    hostTick();
}

}  // namespace

namespace camp_alert {

void installEarly() { sim_tick::add(&tick, "camp alert"); }

}  // namespace camp_alert

namespace game {

void shareCamps(bool host) {
    std::lock_guard lock(g_mutex);
    if (g_sharing == host) return;
    g_sharing = host;
    g_known.clear();
    g_outgoing.clear();
}

std::vector<camp_wire::CampPhase> takeCampPhases(bool all) {
    std::lock_guard lock(g_mutex);
    if (all) g_sendAll = true;
    std::vector<camp_wire::CampPhase> out;
    out.swap(g_outgoing);
    return out;
}

void applyCampPhases(const std::vector<camp_wire::CampPhase>& camps) {
    std::lock_guard lock(g_mutex);
    g_incoming.insert(g_incoming.end(), camps.begin(), camps.end());
    constexpr size_t kMaxQueued = camp_wire::kMaxCamps * 4;
    if (g_incoming.size() > kMaxQueued) g_incoming.erase(g_incoming.begin(), g_incoming.end() - kMaxQueued);
}

}  // namespace game
