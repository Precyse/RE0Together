#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#include "game.h"
#include "protocol.h"
#include "room_authority_wire.h"

// HIT_FX (state mode, docs/RE0_NOTES.md "State mode: hit effects"): what a hit looked and sounded like on the machine
// that applied it. Every enemy class's damage function (vtable slot 35) spawns the blood and sparks itself, through
// uEnemyBase::spawnHitEffectByAttackType 0x4c8b80, and plays its hit sounds through playSeAtPosition 0x644740. Only
// the enemy's simulator runs that function, so the other machine (the shooter's included, when it does not run the
// room) saw no blood and heard no hit. The simulator records those calls while it applies a hit and sends them; the
// other machines make the same calls on their copy of the enemy, found by its spawn record. Pure, no game access, unit
// tested.
namespace hit_fx_wire {

constexpr uint16_t kMsgHitFx = proto::kFirstGameType + 0x62;     // 0x0162, the enemy's simulator to all, reliable
constexpr uint16_t kMsgPartBurst = proto::kFirstGameType + 0x6a;  // 0x016a, the enemy's simulator to all, reliable
constexpr uint16_t kMsgEnemySound = proto::kFirstGameType + 0x6b;  // 0x016b, the enemies' simulator to all, unreliable
constexpr uint16_t kMsgPartMask = proto::kFirstGameType + 0x6c;    // 0x016c, the enemy's simulator to all, reliable

constexpr uint8_t kMaxEffects = 4;
constexpr uint8_t kMaxSounds = 4;

// One spawnHitEffectByAttackType call: thiscall (enemy) (HitPoint*, zone, attack type).
struct Effect {
    game::HitPoint point;
    int32_t zone;        // the hit zone the damage function picked (attack kind x 3 + legs / body / head)
    int32_t attackType;  // the class's attack type of the hit (vtable slot 76)
};
static_assert(sizeof(Effect) == 20);

// One playSeAtPosition call: stdcall (float* position, bank, sound id, flag) ret 0x10.
struct Sound {
    float pos[3];
    uint32_t bank;
    uint32_t id;
    uint8_t flag;
    uint8_t pad[3];  // zero
};
static_assert(sizeof(Sound) == 24);

struct HitFx {
    uint32_t senderMs;  // the simulator's clock: the other machines show it one render delay late, with the enemy
    uint32_t seq;       // the simulator's numbering of its hits
    uint32_t roomEpoch;
    uint16_t scene;
    uint16_t spawnRecord;  // the enemy, as both machines know it
    uint8_t effects;       // used entries of `effect`
    uint8_t sounds;        // used entries of `sound`
    uint16_t pad;          // zero
    Effect effect[kMaxEffects];
    Sound sound[kMaxSounds];
};
static_assert(sizeof(HitFx) == 196);  // 20 + kMaxEffects x Effect + kMaxSounds x Sound
static_assert(std::is_trivially_copyable_v<HitFx>);

inline bool finite(const float* v, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(v[i])) return false;
    }
    return true;
}

// A message whose counts, ids, padding and numbers are in range, with something to show.
inline bool valid(const HitFx& fx) {
    if (fx.scene >= room_authority_wire::kSceneIdLimit || fx.spawnRecord >= game::kSpawnRecordCount || fx.pad != 0) return false;
    if (fx.effects > kMaxEffects || fx.sounds > kMaxSounds || fx.effects + fx.sounds == 0) return false;
    for (uint8_t i = 0; i < fx.effects; ++i) {
        if (!finite(&fx.effect[i].point.x, 3)) return false;
    }
    for (uint8_t i = 0; i < fx.sounds; ++i) {
        const Sound& s = fx.sound[i];
        if (!finite(s.pos, 3) || s.pad[0] != 0 || s.pad[1] != 0 || s.pad[2] != 0) return false;
    }
    return true;
}

// PART_BURST: a base-family enemy (the zombies) lost a part: 0x4256a0, thiscall (enemy) (part, arg) ret 8, part 2 the
// head (the critical kill, think 0x4240c0) and 4 a limb. It runs in the enemy's think, which only its simulator runs, so
// the other machine kept the head. The other machines make the same call on their copy one render delay late.
struct PartBurst {
    uint32_t senderMs;
    uint32_t seq;
    uint32_t roomEpoch;
    uint16_t scene;
    uint16_t spawnRecord;
    int32_t part;
    int32_t arg;
};
static_assert(sizeof(PartBurst) == 24);

constexpr int32_t kMaxPart = 0x20;  // the parts the function's switch knows are small numbers

inline bool valid(const PartBurst& burst) {
    return burst.scene < room_authority_wire::kSceneIdLimit && burst.spawnRecord < game::kSpawnRecordCount && burst.part >= 0 &&
           burst.part < kMaxPart;
}

inline std::optional<PartBurst> decodeBurst(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(PartBurst)) return std::nullopt;
    PartBurst burst{};
    std::memcpy(&burst, payload.data(), sizeof(burst));
    return valid(burst) ? std::optional<PartBurst>(burst) : std::nullopt;
}

// ENEMY_SOUND: the sounds an enemy's own code played during its update on its simulator (groans, attack cries, a hold's
// sounds). That code never runs on a copy shown from a stream; sounds tied to animation events play there by themselves
// and are not sent. One batch per game tick of the sounds of that tick, every enemy of the room.
constexpr uint8_t kMaxBatchSounds = 8;

struct EnemySoundEntry {
    uint16_t spawnRecord;
    uint8_t flag;
    uint8_t pad;  // zero
    float pos[3];
    uint32_t bank;
    uint32_t id;
};
static_assert(sizeof(EnemySoundEntry) == 24);

struct EnemySoundBatch {
    uint32_t senderMs;
    uint32_t seq;
    uint32_t roomEpoch;
    uint16_t scene;
    uint8_t count;
    uint8_t pad;  // zero
    EnemySoundEntry entry[kMaxBatchSounds];
};
static_assert(sizeof(EnemySoundBatch) == 208);

inline bool valid(const EnemySoundBatch& batch) {
    if (batch.scene >= room_authority_wire::kSceneIdLimit || batch.count == 0 || batch.count > kMaxBatchSounds || batch.pad != 0) return false;
    for (uint8_t i = 0; i < batch.count; ++i) {
        const EnemySoundEntry& e = batch.entry[i];
        if (e.spawnRecord >= game::kSpawnRecordCount || e.pad != 0 || !finite(e.pos, 3)) return false;
    }
    return true;
}

inline std::optional<EnemySoundBatch> decodeSounds(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(EnemySoundBatch)) return std::nullopt;
    EnemySoundBatch batch{};
    std::memcpy(&batch, payload.data(), sizeof(batch));
    return valid(batch) ? std::optional<EnemySoundBatch>(batch) : std::nullopt;
}

// PART_MASK: the model part mask of an enemy (uEnemyBase +0x110, one bit per mesh part; 0x4c8730 swaps a part for its
// stump: a burst head, a lost limb), sent by the simulator when it changes, every class alike. The bursts' blood comes
// with PART_BURST (the zombies) or HIT_FX; this makes the missing part stay missing on the other machine.
constexpr uintptr_t kPartMaskOffset = 0x110;
constexpr size_t kPartMaskWords = 2;

struct PartMask {
    uint32_t senderMs;
    uint32_t seq;
    uint32_t roomEpoch;
    uint16_t scene;
    uint16_t spawnRecord;
    uint32_t words[kPartMaskWords];
};
static_assert(sizeof(PartMask) == 24);

inline bool valid(const PartMask& mask) {
    return mask.scene < room_authority_wire::kSceneIdLimit && mask.spawnRecord < game::kSpawnRecordCount;
}

inline std::optional<PartMask> decodeMask(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(PartMask)) return std::nullopt;
    PartMask mask{};
    std::memcpy(&mask, payload.data(), sizeof(mask));
    return valid(mask) ? std::optional<PartMask>(mask) : std::nullopt;
}

// The last part mask sent per pool slot: a mask is sent when its enemy (record) or its words change.
template <size_t Slots>
class MaskTracker {
public:
    // True when slot `slot` now holds `record` with `words` that were not sent for it yet; remembers them.
    bool changed(size_t slot, uint16_t record, const uint32_t (&words)[kPartMaskWords]) {
        if (slot >= Slots) return false;
        Seen& seen = seen_[slot];
        if (!seen.known || seen.record != record) {
            seen = {true, record, {words[0], words[1]}};
            return true;  // a new enemy in the slot (or a room this machine just took over): its mask may already differ
        }
        if (seen.words[0] == words[0] && seen.words[1] == words[1]) return false;
        seen.words[0] = words[0];
        seen.words[1] = words[1];
        return true;
    }

    void forget(size_t slot) {
        if (slot < Slots) seen_[slot] = {};
    }

private:
    struct Seen {
        bool known = false;
        uint16_t record = 0;
        uint32_t words[kPartMaskWords] = {};
    };
    std::array<Seen, Slots> seen_{};
};

// A playSeAtPosition call returning into the enemy classes' code (sEnemy and the uEnemy classes sit between these
// addresses; the player's and the sound system's own callers lie above): the class's own sound, not an animation's.
constexpr uintptr_t kEnemyCodeBegin = 0x415000;
constexpr uintptr_t kEnemyCodeEnd = 0x4e0000;
constexpr bool fromEnemyCode(uintptr_t returnAddress) { return returnAddress >= kEnemyCodeBegin && returnAddress < kEnemyCodeEnd; }

// The payload as a HIT_FX of exactly the right size whose fields are in range, or nothing.
inline std::optional<HitFx> decode(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(HitFx)) return std::nullopt;
    HitFx fx{};
    std::memcpy(&fx, payload.data(), sizeof(fx));
    return valid(fx) ? std::optional<HitFx>(fx) : std::nullopt;
}

// Collects the calls of one applied hit; the calls past the capacity are dropped (a damage function makes one or two).
class Recorder {
public:
    void start(uint32_t senderMs, uint32_t seq, uint16_t scene, uint16_t spawnRecord, uint32_t roomEpoch) {
        fx_ = HitFx{};
        fx_.senderMs = senderMs;
        fx_.seq = seq;
        fx_.scene = scene;
        fx_.spawnRecord = spawnRecord;
        fx_.roomEpoch = roomEpoch;
        recording_ = true;
    }

    bool recording() const { return recording_; }

    void addEffect(const game::HitPoint& point, int32_t zone, int32_t attackType) {
        if (!recording_ || fx_.effects >= kMaxEffects) return;
        fx_.effect[fx_.effects++] = {point, zone, attackType};
    }

    void addSound(const float (&pos)[3], uint32_t bank, uint32_t id, uint8_t flag) {
        if (!recording_ || fx_.sounds >= kMaxSounds) return;
        Sound& s = fx_.sound[fx_.sounds++];
        std::memcpy(s.pos, pos, sizeof(s.pos));
        s.bank = bank;
        s.id = id;
        s.flag = flag;
    }

    // The recorded hit, or nothing when it made no effect and no sound; recording stops either way.
    std::optional<HitFx> finish() {
        recording_ = false;
        if (fx_.effects + fx_.sounds == 0) return std::nullopt;
        return fx_;
    }

private:
    HitFx fx_{};
    bool recording_ = false;
};

}  // namespace hit_fx_wire
