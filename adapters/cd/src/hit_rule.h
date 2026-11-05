#pragma once
// The host's acceptance of a guest's HIT_INTENT (pure, unit tested; cd design world.md 4.2). Every refusal has a
// reason the caller counts and logs with the inputs. Engagement is not required: a passive enemy accepts the first blow.
#include <array>
#include <cstdint>
#include <deque>

namespace hit_rule {

constexpr float kMeleeRangeM = 15.0f;
constexpr float kProjectileRangeM = 80.0f;
constexpr uint32_t kMaxIntentsPerSecond = 15;
constexpr uint64_t kRateWindowMs = 1000;
constexpr size_t kRememberedHitIds = 1024;

enum class Accessor : uint8_t { Melee = 0, Projectile = 1 };

enum class Refusal : uint8_t {
    None,
    StaleZone,      // the intent's zone epoch is not the current one
    StaleSerial,    // the guest's load serial changed since
    EnemyGone,      // the host key is not alive at the host's current serial
    NotDamageable,  // a boss, or an actor without an HP stat (a lever or door crashed the server)
    HostBusy,       // a host cutscene or load
    TooFar,
    BadDelta,       // not positive, or above the enemy's max HP
    Rate,           // more than 15 per second from this guest
    Duplicate,      // this hit id was seen
    BadAccessor,
};

struct Intent {
    uint32_t hitId = 0;
    uint32_t zoneEpoch = 0;
    uint16_t loadSerial = 0;
    uint8_t accessor = 0;
    float distanceM = 0.0f;   // the guest's reported position to the host enemy
    int64_t deltaX1000 = 0;   // HP the guest's own server took from the shadow, x1000
};

struct Enemy {
    bool alive = false;
    bool boss = false;
    bool hasHp = false;
    int64_t maxHpX1000 = 0;
};

struct Context {
    uint32_t zoneEpoch = 0;
    uint16_t guestLoadSerial = 0;
    bool hostBusy = false;
};

// One per guest: remembers its hit ids and its recent intents.
class Gate {
public:
    Refusal check(const Intent& in, const Enemy& enemy, const Context& ctx, uint64_t nowMs) {
        const Refusal refusal = evaluate(in, enemy, ctx, nowMs);
        if (refusal != Refusal::Duplicate) remember(in.hitId);
        if (refusal == Refusal::None) accepted_.push_back(nowMs);
        return refusal;
    }

private:
    Refusal evaluate(const Intent& in, const Enemy& enemy, const Context& ctx, uint64_t nowMs) {
        if (seen(in.hitId)) return Refusal::Duplicate;
        if (in.zoneEpoch != ctx.zoneEpoch) return Refusal::StaleZone;
        if (in.loadSerial != ctx.guestLoadSerial) return Refusal::StaleSerial;
        if (!enemy.alive) return Refusal::EnemyGone;
        if (enemy.boss || !enemy.hasHp) return Refusal::NotDamageable;
        if (ctx.hostBusy) return Refusal::HostBusy;
        if (in.accessor > static_cast<uint8_t>(Accessor::Projectile)) return Refusal::BadAccessor;
        const float range = in.accessor == static_cast<uint8_t>(Accessor::Melee) ? kMeleeRangeM : kProjectileRangeM;
        if (!(in.distanceM >= 0.0f && in.distanceM <= range)) return Refusal::TooFar;
        if (in.deltaX1000 <= 0 || in.deltaX1000 > enemy.maxHpX1000) return Refusal::BadDelta;
        while (!accepted_.empty() && nowMs - accepted_.front() >= kRateWindowMs) accepted_.pop_front();
        if (accepted_.size() >= kMaxIntentsPerSecond) return Refusal::Rate;
        return Refusal::None;
    }

    bool seen(uint32_t hitId) const {
        for (uint32_t id : ids_) {
            if (id == hitId) return true;
        }
        return false;
    }

    void remember(uint32_t hitId) {
        if (ids_.size() == kRememberedHitIds) ids_.pop_front();
        ids_.push_back(hitId);
    }

    std::deque<uint32_t> ids_;
    std::deque<uint64_t> accepted_;
};

}  // namespace hit_rule
