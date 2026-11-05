#pragma once
// The shared zone around the host (pure, unit tested; cd design world.md 1). Inside it the guest hides its own hostile
// natives and shows copies of the host's. The golden predicate decides which actors are shared; both PCs must agree or
// enemies attack from nowhere.
#include <cstdint>
#include <optional>

namespace zone_rule {

constexpr float kOnRadiusM = 150.0f;
constexpr float kOffRadiusM = 250.0f;
constexpr uint64_t kOnHoldMs = 2000;
constexpr uint64_t kOffHoldMs = 5000;
constexpr uint64_t kFightWaitMs = 60000;       // an off decision waits this long for the guest to stop fighting
constexpr uint64_t kHostDeathGraceMs = 60000;  // the host's death and respawn never turn the zone off

enum class OffReason : uint8_t { None, Distance, FieldChanged, BossStage, NotReady };

struct Inputs {
    uint64_t nowMs = 0;
    bool bothReady = false;
    bool sameField = false;
    float distanceM = 0.0f;
    bool hostBossStage = false;
    bool hostDead = false;     // the host is dead or respawning
    bool guestFighting = false;  // the guest's battle state is not 0
};

struct Transition {
    bool changed = false;
    bool on = false;
    OffReason reason = OffReason::None;
};

class Zone {
public:
    bool on() const { return on_; }
    uint32_t epoch() const { return epoch_; }

    Transition update(const Inputs& in) {
        if (in.hostDead && !hostWasDead_) hostDeathMs_ = in.nowMs;
        hostWasDead_ = in.hostDead;
        const bool grace = hostDeathMs_ && in.nowMs - *hostDeathMs_ < kHostDeathGraceMs;
        return on_ ? whileOn(in, grace) : whileOff(in);
    }

private:
    Transition whileOff(const Inputs& in) {
        const bool near = in.bothReady && in.sameField && !in.hostBossStage && in.distanceM <= kOnRadiusM;
        if (!near) {
            nearSinceMs_.reset();
            return {};
        }
        if (!nearSinceMs_) nearSinceMs_ = in.nowMs;
        if (in.nowMs - *nearSinceMs_ < kOnHoldMs) return {};
        on_ = true;
        ++epoch_;
        farSinceMs_.reset();
        offSinceMs_.reset();
        return {true, true, OffReason::None};
    }

    Transition whileOn(const Inputs& in, bool hostGrace) {
        if (in.distanceM > kOffRadiusM && !hostGrace) {
            if (!farSinceMs_) farSinceMs_ = in.nowMs;
        } else {
            farSinceMs_.reset();
        }
        OffReason reason = OffReason::None;
        if (!in.sameField && !hostGrace) {
            reason = OffReason::FieldChanged;
        } else if (!in.bothReady && !hostGrace) {
            reason = OffReason::NotReady;
        } else if (in.hostBossStage) {
            reason = OffReason::BossStage;
        } else if (farSinceMs_ && in.nowMs - *farSinceMs_ >= kOffHoldMs) {
            reason = OffReason::Distance;
        }
        if (reason == OffReason::None) {
            offSinceMs_.reset();
            return {};
        }
        // Distance and boss stage wait for the guest's fight to end (at most kFightWaitMs); a field change or a lost
        // world cannot keep the zone.
        const bool waits = reason == OffReason::Distance || reason == OffReason::BossStage;
        if (!offSinceMs_) offSinceMs_ = in.nowMs;
        if (waits && in.guestFighting && in.nowMs - *offSinceMs_ < kFightWaitMs) return {};
        on_ = false;
        nearSinceMs_.reset();
        return {true, false, reason};
    }

    bool on_ = false;
    uint32_t epoch_ = 0;
    std::optional<uint64_t> nearSinceMs_, farSinceMs_, offSinceMs_, hostDeathMs_;  // when each condition began
    bool hostWasDead_ = false;
};

// The golden predicate's inputs, read from an ActorLoadDataAck record and the static tables.
enum class VariantKind : uint8_t { Normal, Boss, MiddleBoss, Unique };
enum class SpawnOrigin : uint8_t { Normal, Quest, Boss, Contents, Wanted };

constexpr uint8_t kRecordTypeNpc = 3;
constexpr uint8_t kParentMount = 5;
constexpr uint8_t kParentTower = 7;

struct Candidate {
    uint8_t recordType = 0;
    uint32_t owner = 0;
    bool hostileFaction = false;  // one of the 55 hostile factions of 135
    VariantKind variant = VariantKind::Normal;
    SpawnOrigin origin = SpawnOrigin::Normal;
    uint8_t parentType = 0;  // 0: no parent
};

// True when the actor is shared: the host copies it to the guest and the guest hides its own.
constexpr bool shared(const Candidate& c) {
    return c.recordType == kRecordTypeNpc && c.owner == 0 && c.hostileFaction && c.variant == VariantKind::Normal &&
           c.origin == SpawnOrigin::Normal && c.parentType != kParentMount && c.parentType != kParentTower;
}

}  // namespace zone_rule
