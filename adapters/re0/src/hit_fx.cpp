#include "hit_fx.h"

#include <intrin.h>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "character_owner.h"
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
#include "weapon_fx_game.h"

namespace {

namespace wire = hit_fx_wire;

constexpr uintptr_t kSpawnHitEffectFunction = 0x4c8b80;  // uEnemyBase::spawnHitEffectByAttackType, thiscall ret 0xc
// playSeAtPosition: (float*, bank, id, u8) ret 0x10, run on the caller's ECX: it hands ECX on as `this` of the sound
// request 0x644a00, which locks this + 0xf04. A detour must pass ECX through (an stdcall one let the compiler reuse ECX:
// a live crash locked 0x40 + 0xf04), and a replay needs the sound manager the game's own calls carry.
constexpr uintptr_t kPlaySeAtPositionFunction = 0x644740;
constexpr uintptr_t kPartBurstFunction = 0x4256a0;  // the base family's part burst, thiscall (enemy) (part, arg) ret 8
constexpr std::array<uint8_t, 8> kSpawnHitEffectBytes = {0x8b, 0x44, 0x24, 0x0c, 0x48, 0x56, 0x8b, 0xf1};  // mov eax,[esp+0xc]; dec eax; push esi; mov esi,ecx
constexpr std::array<uint8_t, 6> kPlaySeBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};  // push ebp; mov ebp,esp; and esp,-16
constexpr std::array<uint8_t, 9> kPartBurstBytes = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0, 0x83, 0xec, 0x68};  // ... sub esp,0x68
constexpr size_t kMaxInbox = 64;
constexpr size_t kEffectHandleWords = 2;  // sEffect::create's out handle

using SpawnHitEffect = void(__fastcall*)(void* enemy, void* edx, game::HitPoint* point, int32_t zone, int32_t attackType);
using PlaySe = int(__fastcall*)(void* sound, void* edx, float* pos, uint32_t bank, uint32_t id, uint8_t flag);
using PartBurst = void(__fastcall*)(void* enemy, void* edx, int32_t part, int32_t arg);

SpawnHitEffect g_originalSpawn = nullptr;
PlaySe g_originalPlaySe = nullptr;
std::atomic<void*> g_soundManager{nullptr};  // the ECX of the game's last own call: replays play through it
PartBurst g_originalBurst = nullptr;

// Messages from the room's simulator: received on the net thread, shown on the game thread at the sender's time to show.
template <typename Message>
class Channel {
public:
    void receive(uint8_t sender, const Message& message) {
        std::lock_guard lock(mutex_);
        if (inbox_.size() >= kMaxInbox) inbox_.erase(inbox_.begin());
        inbox_.push_back({sender, message});
    }

    // Game thread: queues the arrivals and shows the messages that are due; a new sender starts the queue over.
    template <typename Show>
    void tick(Show show) {
        std::vector<Arrival> arrivals;
        {
            std::lock_guard lock(mutex_);
            arrivals.swap(inbox_);
        }
        for (const Arrival& arrival : arrivals) {
            if (sender_ != arrival.sender) {
                queue_.clear();
                sender_ = arrival.sender;
            }
            queue_.push(arrival.message);
        }
        if (sender_ < 0) return;
        const std::optional<stream_clock::StreamClock> clock = puppet::clockFor(static_cast<uint8_t>(sender_));
        if (!clock || !clock->ready()) return;
        due_.clear();
        queue_.takeDue(clock->target(pose_wire::clockMs()), due_);
        for (const Message& message : due_) show(message);
    }

private:
    struct Arrival {
        uint8_t sender;
        Message message;
    };
    std::mutex mutex_;
    std::vector<Arrival> inbox_;  // guarded by mutex_
    timed_queue::TimedQueue<Message> queue_;
    int sender_ = -1;
    std::vector<Message> due_;
};

Channel<wire::HitFx> g_hits;
Channel<wire::PartBurst> g_bursts;
Channel<wire::EnemySoundBatch> g_sounds;
Channel<wire::EnemyEffectBatch> g_effects;
Channel<wire::PartMask> g_masks;

template <typename Message>
void receive(Channel<Message>& channel, const GameFrame& frame, std::optional<Message> (*decode)(std::span<const uint8_t>), const char* what) {
    const std::optional<Message> message = decode(frame.payload);
    if (!message) {
        logger::writeUnlessRepeated("hit_fx: malformed %s from slot %u refused", what, frame.slot);
        return;
    }
    channel.receive(frame.slot, *message);
}

// Game thread only.
wire::Recorder g_recorder;
uintptr_t g_recordedEnemy = 0;
uint32_t g_hitSeq = 0;
uint32_t g_burstSeq = 0;
bool g_inBurst = false;          // the simulator's own burst runs: its effects travel as PART_BURST
uintptr_t g_movingEnemy = 0;     // the enemy simulated here whose move runs now, or 0
uint16_t g_movingRecord = 0;
std::vector<wire::EnemySoundEntry> g_tickSounds;    // this tick's sounds of simulated enemies
std::vector<wire::EnemyEffectEntry> g_tickEffects;  // this tick's effects of simulated enemies
uint32_t g_soundSeq = 0;
uint32_t g_effectSeq = 0;
wire::MaskTracker<game::kEnemyPoolSlots> g_sentMasks;  // the part masks of the enemies simulated here
uint32_t g_maskSeq = 0;

void __fastcall spawnDetour(void* enemy, void* edx, game::HitPoint* point, int32_t zone, int32_t attackType) {
    if (g_recorder.recording() && reinterpret_cast<uintptr_t>(enemy) == g_recordedEnemy && point) {
        g_recorder.addEffect(*point, zone, attackType);
    }
    g_originalSpawn(enemy, edx, point, zone, attackType);
}

int __fastcall playSeDetour(void* sound, void* edx, float* pos, uint32_t bank, uint32_t id, uint8_t flag) {
    g_soundManager = sound;
    if (pos && g_recorder.recording()) {
        const float at[3] = {pos[0], pos[1], pos[2]};
        g_recorder.addSound(at, bank, id, flag);
    } else if (pos && g_movingEnemy && wire::fromEnemyCode(reinterpret_cast<uintptr_t>(_ReturnAddress())) &&
               g_tickSounds.size() < wire::kMaxBatchSounds) {
        g_tickSounds.push_back({g_movingRecord, flag, 0, {pos[0], pos[1], pos[2]}, bank, id});
    }
    return g_originalPlaySe(sound, edx, pos, bank, id, flag);
}

// The simulator of an enemy sends each part it loses; the other machines call the original through showBurst.
void __fastcall burstDetour(void* enemy, void* edx, int32_t part, int32_t arg) {
    g_inBurst = true;
    g_originalBurst(enemy, edx, part, arg);
    g_inBurst = false;
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

// A sound of the other machine's, through the sound manager the game's own calls use; none before the first such call.
void playSound(float (&pos)[3], uint32_t bank, uint32_t id, uint8_t flag) {
    if (void* sound = g_soundManager.load()) g_originalPlaySe(sound, nullptr, pos, bank, id, flag);
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
    g_originalBurst(reinterpret_cast<void*>(enemy), nullptr, burst.part, burst.arg);
    logger::write("hit_fx: record %u lost part %d (from its simulator)", burst.spawnRecord, burst.part);
}

// The calls the simulator's damage function made, on this machine's copy of the enemy.
void showHit(const wire::HitFx& fx) {
    const uintptr_t enemy = localEnemy(fx.scene, fx.roomEpoch, fx.spawnRecord);
    if (!enemy) return;
    for (uint8_t i = 0; i < fx.effects; ++i) {
        game::HitPoint point = fx.effect[i].point;
        g_originalSpawn(reinterpret_cast<void*>(enemy), nullptr, &point, fx.effect[i].zone, fx.effect[i].attackType);
    }
    for (uint8_t i = 0; i < fx.sounds; ++i) {
        const wire::Sound& sound = fx.sound[i];
        float pos[3] = {sound.pos[0], sound.pos[1], sound.pos[2]};
        playSound(pos, sound.bank, sound.id, sound.flag);
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
            g_sentMasks.forget(slot);
            continue;
        }
        if (!g_sentMasks.changed(slot, origin->record, words)) continue;
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

// The loaded room's holder for a batch of this tick, or nothing.
std::optional<room_authority::Holder> loadedHolder() {
    const uint16_t loaded = scene::current();
    return loaded == scene::kNone ? std::nullopt : room_authority::holderOf(loaded);
}

// The tick's sounds and effects of the enemies this machine simulates, one batch each.
void sendTickBatches() {
    const std::optional<room_authority::Holder> holder = loadedHolder();
    if (holder && !g_tickSounds.empty()) {
        wire::EnemySoundBatch batch{};
        batch.senderMs = pose_wire::clockMs();
        batch.seq = ++g_soundSeq;
        batch.roomEpoch = holder->epoch;
        batch.scene = scene::current();
        batch.count = static_cast<uint8_t>(g_tickSounds.size());
        std::copy(g_tickSounds.begin(), g_tickSounds.end(), batch.entry);
        state_net::send(wire::kMsgEnemySound, false, proto::bytesOf(batch));
    }
    if (holder && !g_tickEffects.empty()) {
        wire::EnemyEffectBatch batch{};
        batch.senderMs = pose_wire::clockMs();
        batch.seq = ++g_effectSeq;
        batch.roomEpoch = holder->epoch;
        batch.scene = scene::current();
        batch.count = static_cast<uint8_t>(g_tickEffects.size());
        std::copy(g_tickEffects.begin(), g_tickEffects.end(), batch.entry);
        state_net::send(wire::kMsgEnemyEffect, true, proto::bytesOf(batch));
    }
    g_tickSounds.clear();
    g_tickEffects.clear();
}

void showSounds(const wire::EnemySoundBatch& batch) {
    for (uint8_t i = 0; i < batch.count; ++i) {
        const wire::EnemySoundEntry& e = batch.entry[i];
        if (!localEnemy(batch.scene, batch.roomEpoch, e.spawnRecord)) continue;
        float pos[3] = {e.pos[0], e.pos[1], e.pos[2]};
        playSound(pos, e.bank, e.id, e.flag);
    }
}

// The local unit an entry's attachment names, or nothing when it names one that is not here.
std::optional<uintptr_t> attachmentOf(wire::Attach attach, uintptr_t enemy) {
    switch (attach) {
        case wire::Attach::None: return 0;
        case wire::Attach::Enemy: return enemy;
        case wire::Attach::Billy:
        case wire::Attach::Rebecca: {
            const uintptr_t player = character_owner::find(attach == wire::Attach::Billy ? character_owner::Character::Billy
                                                                                          : character_owner::Character::Rebecca);
            return player ? std::optional<uintptr_t>(player) : std::nullopt;
        }
    }
    return std::nullopt;
}

void showEffects(const wire::EnemyEffectBatch& batch) {
    void* effects = reinterpret_cast<void*>(game::readPointer(game::kEffectGlobal));
    if (!effects) return;
    for (uint8_t i = 0; i < batch.count; ++i) {
        const wire::EnemyEffectEntry& e = batch.entry[i];
        const uintptr_t enemy = localEnemy(batch.scene, batch.roomEpoch, e.spawnRecord);
        const std::optional<uintptr_t> attach = enemy ? attachmentOf(static_cast<wire::Attach>(e.attach), enemy) : std::nullopt;
        const uintptr_t set = enemy ? game::readPointer(enemy + game::kEffectSetOffset) : 0;
        if (!attach || !set) continue;
        uint32_t flags[2] = {0, e.flags};
        game::readMemory(game::kEffectFlagsGlobal, flags[0]);
        uint32_t handle[kEffectHandleWords] = {};
        reinterpret_cast<weapon_fx_game::EffectCreateFunction>(game::kEffectCreateFunction)(
            effects, nullptr, handle, enemy, set, e.index, *attach, e.joint, e.pos, e.rot, flags, e.extra);
    }
}

void onTick() {
    sendChangedMasks();
    sendTickBatches();
    g_masks.tick(showMask);
    g_sounds.tick(showSounds);
    g_effects.tick(showEffects);
    g_bursts.tick(showBurst);
    g_hits.tick(showHit);
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
    const bool routed =
        frame_router::add(wire::kMsgHitFx, frame_router::SenderPolicy::Any,
                          [](const GameFrame& f) { receive(g_hits, f, &wire::decode, "hit"); }) &&
        frame_router::add(wire::kMsgPartBurst, frame_router::SenderPolicy::Any,
                          [](const GameFrame& f) { receive(g_bursts, f, &wire::decodeBurst, "part burst"); }) &&
        frame_router::add(wire::kMsgEnemySound, frame_router::SenderPolicy::Any,
                          [](const GameFrame& f) { receive(g_sounds, f, &wire::decodeSounds, "enemy sounds"); }) &&
        frame_router::add(wire::kMsgEnemyEffect, frame_router::SenderPolicy::Any,
                          [](const GameFrame& f) { receive(g_effects, f, &wire::decodeEffects, "enemy effects"); }) &&
        frame_router::add(wire::kMsgPartMask, frame_router::SenderPolicy::Any,
                          [](const GameFrame& f) { receive(g_masks, f, &wire::decodeMask, "part mask"); });
    if (!routed) return false;
    game_tick::addCallback("hit_fx", onTick);
    return true;
}

void beginHit(uintptr_t enemy) {
    const std::optional<enemy_spawn::Origin> origin = enemy_spawn::originOf(enemy);
    const std::optional<room_authority::Holder> holder = loadedHolder();
    if (!g_originalSpawn || !origin || !holder) return;
    g_recordedEnemy = enemy;
    g_recorder.start(pose_wire::clockMs(), ++g_hitSeq, scene::current(), origin->record, holder->epoch);
}

void endHit() {
    if (!g_recorder.recording()) return;
    const std::optional<wire::HitFx> fx = g_recorder.finish();
    g_recordedEnemy = 0;
    if (fx && state_net::send(wire::kMsgHitFx, true, proto::bytesOf(*fx))) debug_stats::count(debug_stats::Counter::HitFxSent);
}

void beginMove(uintptr_t enemy) {
    const std::optional<enemy_spawn::Origin> origin = enemy_spawn::originOf(enemy);
    if (!g_originalPlaySe || !origin) return;
    g_movingEnemy = enemy;
    g_movingRecord = origin->record;
}

void endMove() { g_movingEnemy = 0; }

void effectCreated(uintptr_t caller, uintptr_t owner, uint32_t index, uintptr_t attach, int32_t joint, const float* pos,
                   const float* rot, const uint32_t* flags, uint32_t extra) {
    if (!game_tick::onGameThread() || !g_movingEnemy || owner != g_movingEnemy || g_inBurst || g_recorder.recording() ||
        !wire::fromEnemyCode(caller) || !pos || !rot || !flags || joint < wire::kMinJoint || joint > wire::kMaxJoint ||
        index > UINT16_MAX || g_tickEffects.size() >= wire::kMaxBatchEffects) {
        return;
    }
    wire::Attach named = wire::Attach::None;
    if (attach == owner) {
        named = wire::Attach::Enemy;
    } else if (attach) {
        const character_owner::Character character = character_owner::identify(attach);
        if (character == character_owner::Character::Unknown) return;
        named = character == character_owner::Character::Billy ? wire::Attach::Billy : wire::Attach::Rebecca;
    }
    g_tickEffects.push_back({g_movingRecord, static_cast<uint16_t>(index), static_cast<uint8_t>(named), static_cast<int8_t>(joint), 0,
                             flags[1], extra, {pos[0], pos[1], pos[2]}, {rot[0], rot[1], rot[2]}});
}

}  // namespace hit_fx
