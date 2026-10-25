#include "hit_fx.h"

#include <intrin.h>

#include <array>
#include <mutex>
#include <optional>
#include <vector>

#include "debug_stats.h"
#include "enemy_observer.h"
#include "enemy_registry.h"
#include "enemy_spawn.h"
#include "frame_router.h"
#include "game.h"
#include "game_tick.h"
#include "hit_fx_wire.h"
#include "hooks.h"
#include "log.h"
#include "pose_wire.h"
#include "puppet.h"
#include "room_authority.h"
#include "scene.h"
#include "state_net.h"
#include "timed_queue.h"

namespace {

namespace wire = hit_fx_wire;

constexpr uintptr_t kSpawnHitEffectFunction = 0x4c8b80;  // uEnemyBase::spawnHitEffectByAttackType, thiscall ret 0xc
constexpr uintptr_t kPlaySeAtPositionFunction = 0x644740;  // playSeAtPosition, stdcall (float*, bank, id, u8) ret 0x10
constexpr uintptr_t kPartBurstFunction = 0x4256a0;  // the base family's part burst, thiscall (enemy) (part, arg) ret 8
constexpr std::array<uint8_t, 8> kSpawnHitEffectBytes = {0x8b, 0x44, 0x24, 0x0c, 0x48, 0x56, 0x8b, 0xf1};  // mov eax,[esp+0xc]; dec eax; push esi; mov esi,ecx
constexpr std::array<uint8_t, 6> kPlaySeBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};  // push ebp; mov ebp,esp; and esp,-16
constexpr std::array<uint8_t, 9> kPartBurstBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0, 0x83, 0xec, 0x68};  // ... sub esp,0x68
constexpr size_t kMaxInbox = 64;

using SpawnHitEffect = void(__fastcall*)(void* enemy, void* edx, game::HitPoint* point, int32_t zone, int32_t attackType);
using PlaySe = int(__stdcall*)(float* pos, uint32_t bank, uint32_t id, uint8_t flag);
using PartBurst = void(__fastcall*)(void* enemy, void* edx, int32_t part, int32_t arg);

SpawnHitEffect g_originalSpawn = nullptr;
PlaySe g_originalPlaySe = nullptr;
PartBurst g_originalBurst = nullptr;

// Game thread only.
wire::Recorder g_recorder;
uintptr_t g_recordedEnemy = 0;
uint32_t g_sendSeq = 0;
timed_queue::TimedQueue<wire::HitFx> g_queue;
int g_queueSender = -1;
std::vector<wire::HitFx> g_due;
uint32_t g_burstSeq = 0;
timed_queue::TimedQueue<wire::PartBurst> g_burstQueue;
int g_burstSender = -1;
std::vector<wire::PartBurst> g_burstsDue;
bool g_showingBurst = false;  // game thread: showBurst is making the call, which must not be sent back
uint16_t g_movingRecord = 0;    // the spawn record of the enemy whose move runs now
bool g_moving = false;
std::vector<wire::EnemySoundEntry> g_tickSounds;  // this tick's sounds of simulated enemies
uint32_t g_soundSeq = 0;
wire::MaskTracker<game::kEnemyPoolSlots> g_masks;  // game thread: the part masks of the enemies simulated here
uint32_t g_maskSeq = 0;
timed_queue::TimedQueue<wire::PartMask> g_maskQueue;
int g_maskSender = -1;
std::vector<wire::PartMask> g_masksDue;

struct Arrival {
    uint8_t sender;
    wire::HitFx fx;
};
std::mutex g_mutex;
std::vector<Arrival> g_inbox;  // guarded by g_mutex: net thread to game thread
struct BurstArrival {
    uint8_t sender;
    wire::PartBurst burst;
};
std::vector<BurstArrival> g_burstInbox;  // guarded by g_mutex

void __fastcall spawnDetour(void* enemy, void* edx, game::HitPoint* point, int32_t zone, int32_t attackType) {
    if (g_recorder.recording() && reinterpret_cast<uintptr_t>(enemy) == g_recordedEnemy && point) {
        g_recorder.addEffect(*point, zone, attackType);
    }
    g_originalSpawn(enemy, edx, point, zone, attackType);
}

int __stdcall playSeDetour(float* pos, uint32_t bank, uint32_t id, uint8_t flag) {
    if (pos && g_recorder.recording()) {
        const float at[3] = {pos[0], pos[1], pos[2]};
        g_recorder.addSound(at, bank, id, flag);
    } else if (pos && g_moving && wire::fromEnemyCode(reinterpret_cast<uintptr_t>(_ReturnAddress())) &&
               g_tickSounds.size() < wire::kMaxBatchSounds) {
        g_tickSounds.push_back({g_movingRecord, flag, 0, {pos[0], pos[1], pos[2]}, bank, id});
    }
    return g_originalPlaySe(pos, bank, id, flag);
}

// The simulator of an enemy sends each part it loses; a copy shown from a stream makes the call only through showBurst.
void __fastcall burstDetour(void* enemy, void* edx, int32_t part, int32_t arg) {
    g_originalBurst(enemy, edx, part, arg);
    if (g_showingBurst) return;
    const auto address = reinterpret_cast<uintptr_t>(enemy);
    const int slot = enemy_registry::slotOf(address);
    const uint16_t loaded = scene::current();
    if (slot == enemy_registry::kNoSlot || loaded == scene::kNone) return;
    const enemy_observer_rule::Simulator simulator = enemy_observer::simulatorOf(static_cast<uint8_t>(slot));
    const std::optional<enemy_spawn::Origin> origin = enemy_spawn::originOf(address);
    const std::optional<room_authority::Holder> holder = room_authority::holderOf(loaded);
    if (!simulator.known || !simulator.here || !origin || !holder) return;
    const wire::PartBurst burst{pose_wire::clockMs(), ++g_burstSeq, holder->epoch, loaded, origin->record, part, arg};
    if (state_net::send(wire::kMsgPartBurst, true, proto::bytesOf(burst))) {
        logger::write("hit_fx: slot %d record %u lost part %d, sent", slot, origin->record, part);
    }
}

struct SoundArrival {
    uint8_t sender;
    wire::EnemySoundBatch batch;
};
std::vector<SoundArrival> g_soundInbox;  // guarded by g_mutex
struct MaskArrival {
    uint8_t sender;
    wire::PartMask mask;
};
std::vector<MaskArrival> g_maskInbox;  // guarded by g_mutex

void onMaskFrame(const GameFrame& frame) {
    const std::optional<wire::PartMask> mask = wire::decodeMask(frame.payload);
    if (!mask) {
        logger::writeUnlessRepeated("hit_fx: malformed part mask from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_maskInbox.size() >= kMaxInbox) g_maskInbox.erase(g_maskInbox.begin());
    g_maskInbox.push_back({frame.slot, *mask});
}
timed_queue::TimedQueue<wire::EnemySoundBatch> g_soundQueue;
int g_soundSender = -1;
std::vector<wire::EnemySoundBatch> g_soundsDue;

void onSoundFrame(const GameFrame& frame) {
    const std::optional<wire::EnemySoundBatch> batch = wire::decodeSounds(frame.payload);
    if (!batch) {
        logger::writeUnlessRepeated("hit_fx: malformed enemy sounds from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_soundInbox.size() >= kMaxInbox) g_soundInbox.erase(g_soundInbox.begin());
    g_soundInbox.push_back({frame.slot, *batch});
}

void onBurstFrame(const GameFrame& frame) {
    const std::optional<wire::PartBurst> burst = wire::decodeBurst(frame.payload);
    if (!burst) {
        logger::writeUnlessRepeated("hit_fx: malformed part burst from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_burstInbox.size() >= kMaxInbox) g_burstInbox.erase(g_burstInbox.begin());
    g_burstInbox.push_back({frame.slot, *burst});
}

// The local copy of the enemy a message names, in the loaded room and the table's epoch, or 0.
uintptr_t localEnemy(uint16_t scene, uint32_t roomEpoch, uint16_t spawnRecord) {
    if (scene != scene::current()) return 0;
    const std::optional<room_authority::Holder> holder = room_authority::holderOf(scene);
    if (!holder || holder->epoch != roomEpoch) return 0;
    uint8_t slot = 0;
    if (!enemy_observer::toLocalSlot(slot, spawnRecord)) {
        logger::writeUnlessRepeated("hit_fx: record %u has no enemy here", spawnRecord);
        return 0;
    }
    return enemy_registry::enemyAt(slot);
}

void showBurst(const wire::PartBurst& burst) {
    const uintptr_t enemy = localEnemy(burst.scene, burst.roomEpoch, burst.spawnRecord);
    if (!enemy) return;
    g_showingBurst = true;
    g_originalBurst(reinterpret_cast<void*>(enemy), nullptr, burst.part, burst.arg);
    g_showingBurst = false;
    logger::write("hit_fx: record %u lost part %d (from its simulator)", burst.spawnRecord, burst.part);
}

void onFrame(const GameFrame& frame) {
    const std::optional<wire::HitFx> fx = wire::decode(frame.payload);
    if (!fx) {
        logger::writeUnlessRepeated("hit_fx: malformed message from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_inbox.size() >= kMaxInbox) g_inbox.erase(g_inbox.begin());
    g_inbox.push_back({frame.slot, *fx});
}

// The calls the simulator's damage function made, on this machine's copy of the enemy.
void show(const wire::HitFx& fx) {
    const uintptr_t enemy = localEnemy(fx.scene, fx.roomEpoch, fx.spawnRecord);
    if (!enemy) return;
    for (uint8_t i = 0; i < fx.effects; ++i) {
        game::HitPoint point = fx.effect[i].point;
        g_originalSpawn(reinterpret_cast<void*>(enemy), nullptr, &point, fx.effect[i].zone, fx.effect[i].attackType);
    }
    for (uint8_t i = 0; i < fx.sounds; ++i) {
        const wire::Sound& sound = fx.sound[i];
        float pos[3] = {sound.pos[0], sound.pos[1], sound.pos[2]};
        g_originalPlaySe(pos, sound.bank, sound.id, sound.flag);
    }
    debug_stats::count(debug_stats::Counter::HitFxShown);
}

// The part masks of the enemies this machine simulates that changed since they were last sent.
void sendChangedMasks() {
    const uint16_t loaded = scene::current();
    const std::optional<room_authority::Holder> holder = loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
    for (size_t slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(static_cast<int>(slot));
        const std::optional<enemy_spawn::Origin> origin = enemy ? enemy_spawn::originOf(enemy) : std::nullopt;
        uint32_t words[wire::kPartMaskWords] = {};
        if (!origin || !holder || !enemy_observer::simulatorOf(static_cast<uint8_t>(slot)).here ||
            !game::readMemory(enemy + wire::kPartMaskOffset, words)) {
            g_masks.forget(slot);
            continue;
        }
        if (!g_masks.changed(slot, origin->record, words)) continue;
        const wire::PartMask mask{pose_wire::clockMs(), ++g_maskSeq, holder->epoch, loaded, origin->record, {words[0], words[1]}};
        state_net::send(wire::kMsgPartMask, true, proto::bytesOf(mask));
    }
}

void showMask(const wire::PartMask& mask) {
    const uintptr_t enemy = localEnemy(mask.scene, mask.roomEpoch, mask.spawnRecord);
    if (!enemy) return;
    uint32_t words[wire::kPartMaskWords] = {mask.words[0], mask.words[1]};
    game::writeMemory(enemy + wire::kPartMaskOffset, words);
}

void tickMasks() {
    std::vector<MaskArrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_maskInbox);
    }
    for (const MaskArrival& arrival : arrivals) {
        if (g_maskSender != arrival.sender) {
            g_maskQueue.clear();
            g_maskSender = arrival.sender;
        }
        g_maskQueue.push(arrival.mask);
    }
    if (g_maskSender < 0) return;
    const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(g_maskSender));
    if (!clock || !clock->ready()) return;
    g_masksDue.clear();
    g_maskQueue.takeDue(clock->target(pose_wire::clockMs()), g_masksDue);
    for (const wire::PartMask& mask : g_masksDue) showMask(mask);
}

// The tick's sounds of the enemies this machine simulates, in one batch.
void sendTickSounds() {
    if (g_tickSounds.empty()) return;
    const uint16_t loaded = scene::current();
    const std::optional<room_authority::Holder> holder = loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
    if (holder) {
        wire::EnemySoundBatch batch{};
        batch.senderMs = pose_wire::clockMs();
        batch.seq = ++g_soundSeq;
        batch.roomEpoch = holder->epoch;
        batch.scene = loaded;
        batch.count = static_cast<uint8_t>(g_tickSounds.size());
        std::copy(g_tickSounds.begin(), g_tickSounds.end(), batch.entry);
        state_net::send(wire::kMsgEnemySound, false, proto::bytesOf(batch));
    }
    g_tickSounds.clear();
}

void showSounds(const wire::EnemySoundBatch& batch) {
    for (uint8_t i = 0; i < batch.count; ++i) {
        const wire::EnemySoundEntry& e = batch.entry[i];
        if (!localEnemy(batch.scene, batch.roomEpoch, e.spawnRecord)) continue;
        float pos[3] = {e.pos[0], e.pos[1], e.pos[2]};
        g_originalPlaySe(pos, e.bank, e.id, e.flag);
    }
}

void tickSounds() {
    std::vector<SoundArrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_soundInbox);
    }
    for (const SoundArrival& arrival : arrivals) {
        if (g_soundSender != arrival.sender) {
            g_soundQueue.clear();
            g_soundSender = arrival.sender;
        }
        g_soundQueue.push(arrival.batch);
    }
    if (g_soundSender < 0) return;
    const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(g_soundSender));
    if (!clock || !clock->ready()) return;
    g_soundsDue.clear();
    g_soundQueue.takeDue(clock->target(pose_wire::clockMs()), g_soundsDue);
    for (const wire::EnemySoundBatch& batch : g_soundsDue) showSounds(batch);
}

// Hands out the bursts due by the sender's shown time.
void tickBursts() {
    std::vector<BurstArrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_burstInbox);
    }
    for (const BurstArrival& arrival : arrivals) {
        if (g_burstSender != arrival.sender) {
            g_burstQueue.clear();
            g_burstSender = arrival.sender;
        }
        g_burstQueue.push(arrival.burst);
    }
    if (g_burstSender < 0) return;
    const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(g_burstSender));
    if (!clock || !clock->ready()) return;
    g_burstsDue.clear();
    g_burstQueue.takeDue(clock->target(pose_wire::clockMs()), g_burstsDue);
    for (const wire::PartBurst& burst : g_burstsDue) showBurst(burst);
}

void onTick() {
    sendChangedMasks();
    tickMasks();
    sendTickSounds();
    tickSounds();
    tickBursts();
    std::vector<Arrival> arrivals;
    {
        std::lock_guard lock(g_mutex);
        arrivals.swap(g_inbox);
    }
    for (const Arrival& arrival : arrivals) {
        if (g_queueSender != arrival.sender) {
            g_queue.clear();
            g_queueSender = arrival.sender;
        }
        g_queue.push(arrival.fx);
    }
    if (g_queueSender < 0) return;
    const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(g_queueSender));
    if (!clock || !clock->ready()) return;
    g_due.clear();
    g_queue.takeDue(clock->target(pose_wire::clockMs()), g_due);
    for (const wire::HitFx& fx : g_due) show(fx);
}

}  // namespace

namespace hit_fx {

bool enable() {
    if (!game::bytesMatch(kSpawnHitEffectFunction, kSpawnHitEffectBytes) || !game::bytesMatch(kPlaySeAtPositionFunction, kPlaySeBytes) ||
        !game::bytesMatch(kPartBurstFunction, kPartBurstBytes)) {
        logger::write("hit_fx: effect, sound or part burst bytes differ, hit effects stay on the simulator");
        return false;
    }
    if (!hooks::install("spawnHitEffectByAttackType", kSpawnHitEffectFunction, reinterpret_cast<void*>(spawnDetour),
                        reinterpret_cast<void**>(&g_originalSpawn)) ||
        !hooks::install("playSeAtPosition", kPlaySeAtPositionFunction, reinterpret_cast<void*>(playSeDetour),
                        reinterpret_cast<void**>(&g_originalPlaySe)) ||
        !hooks::install("part burst", kPartBurstFunction, reinterpret_cast<void*>(burstDetour), reinterpret_cast<void**>(&g_originalBurst))) {
        return false;
    }
    if (!frame_router::add(wire::kMsgHitFx, frame_router::SenderPolicy::Any, onFrame) ||
        !frame_router::add(wire::kMsgPartBurst, frame_router::SenderPolicy::Any, onBurstFrame) ||
        !frame_router::add(wire::kMsgEnemySound, frame_router::SenderPolicy::Any, onSoundFrame) ||
        !frame_router::add(wire::kMsgPartMask, frame_router::SenderPolicy::Any, onMaskFrame)) {
        return false;
    }
    game_tick::addCallback("hit_fx", onTick);
    return true;
}

void beginHit(uintptr_t enemy) {
    const uint16_t loaded = scene::current();
    const std::optional<enemy_spawn::Origin> origin = enemy_spawn::originOf(enemy);
    const std::optional<room_authority::Holder> holder = loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
    if (!g_originalSpawn || !origin || !holder) return;
    g_recordedEnemy = enemy;
    g_recorder.start(pose_wire::clockMs(), ++g_sendSeq, loaded, origin->record, holder->epoch);
}

void beginMove(uintptr_t enemy) {
    const std::optional<enemy_spawn::Origin> origin = enemy_spawn::originOf(enemy);
    if (!g_originalPlaySe || !origin) return;
    g_movingRecord = origin->record;
    g_moving = true;
}

void endMove() { g_moving = false; }

void endHit() {
    if (!g_recorder.recording()) return;
    const std::optional<wire::HitFx> fx = g_recorder.finish();
    g_recordedEnemy = 0;
    if (fx && state_net::send(wire::kMsgHitFx, true, proto::bytesOf(*fx))) debug_stats::count(debug_stats::Counter::HitFxSent);
}

}  // namespace hit_fx
