#pragma once
#include <algorithm>
#include <cstdint>

#include "enemy_follow_rule.h"
#include "position_blend.h"

// Pure rules of the contact router and the grab lease (partner_mode=state, docs/RE0_NOTES.md "State mode: contact and
// lease", unit tested). Every enemy hit and grab on a player ends in the player's damage entry (vtable slot 34,
// 0x512570): its (kind, level) pick an entry of the reaction table 0xd8d6f8 whose first word is the group-2 think state
// the victim enters. A state the victim plays alone is a plain hit; a state that pairs the victim with the attacker (a
// hold, a bite, a carry) moves the attacker's simulation to the victim's machine for its duration.
namespace contact_rule {

constexpr int kReactionKindCount = 0x1d;  // rows of the reaction table (kind 0..0x1c)
constexpr int32_t kReactionGroup = 2;     // the think group the entry puts the victim in

// ---- Classification ----------------------------------------------------------------------------------------------

enum class Kind : uint8_t {
    Plain,   // states 0..5 flinch, knock-back, knock-down; 17..19 death: the victim reacts alone
    Paired,  // states 6..16 held, bitten, carried, pulled, attached, thrown: the victim is bound to the attacker
};

constexpr int32_t kFirstPairedState = 6;
constexpr int32_t kLastPairedState = 16;  // 11 has no entry in the table; it is treated as paired
constexpr int32_t kFirstDeathState = 17;
constexpr int32_t kLastDeathState = 19;

constexpr Kind classify(int32_t entryState) {
    return entryState >= kFirstPairedState && entryState <= kLastPairedState ? Kind::Paired : Kind::Plain;
}

constexpr bool isDeathState(int32_t state) { return state >= kFirstDeathState && state <= kLastDeathState; }

// The victim stands in one of the states a paired attack holds it in.
constexpr bool held(int32_t group, int32_t state) { return group == kReactionGroup && classify(state) == Kind::Paired; }

// One enemy-on-player contact as the router sees it. `point` is the hit point relative to the enemy.
struct Contact {
    uint8_t slot = 0;    // the enemy's pool slot
    uint8_t victim = 0;  // character_owner::Character of the hit player
    uint8_t kind = 0;    // reaction table row
    uint8_t level = 0;   // entry within the row
    float point[3] = {};
};

// ---- Routing -----------------------------------------------------------------------------------------------------

// A lease needs a living enemy and a victim that stands in the loaded room.
constexpr bool leaseEligible(int32_t enemyHp, bool victimInRoom) { return enemyHp > 0 && victimInRoom; }

// What the router does with a contact whose victim is a puppet, on the machine that simulates the enemy.
enum class Route : uint8_t {
    Drop,   // nothing to send: a repeat of a contact that started a lease, or a paired contact no lease can take (a victim
            // put in a hold without a holder would wait for an enemy that never comes)
    Hit,    // PLAYER_HIT to the victim's owner
    Lease,  // ENEMY_LEASE: the enemy's simulation moves to the victim's machine
};

struct Situation {
    Kind kind = Kind::Plain;
    bool eligible = false;    // leaseEligible
    bool slotBusy = false;    // a lease or a contact waiting to become one already stands on this enemy
    bool sameVictim = false;  // ... and it is this victim's
};

constexpr Route route(const Situation& situation) {
    if (situation.kind == Kind::Plain) return Route::Hit;
    if (situation.slotBusy) return situation.sameVictim ? Route::Drop : Route::Hit;
    return situation.eligible ? Route::Lease : Route::Drop;
}

// The record the enemy ended its update in can be started again on the holder: the grant is made once the enemy's update
// is over, and an enemy that died or is not in an action is not granted.
constexpr bool grantable(int32_t enemyHp, const enemy_follow_rule::Action& record) {
    return enemyHp > 0 && enemy_follow_rule::seedable(record);
}

// ---- End of a lease ----------------------------------------------------------------------------------------------

enum class End : uint8_t {
    None,
    Released,    // the enemy left the granted action and the victim left its hold
    EnemyDead,
    VictimDied,
    Declined,    // the holder could not start the grant
    Expired,     // the holder never answered the grant
    Silent,      // the holder stopped streaming
    Left,        // the enemy is gone from its slot on the holder
};
constexpr uint8_t kEndCount = 8;

constexpr const char* name(End end) {
    switch (end) {
        case End::Released: return "released";
        case End::EnemyDead: return "enemy dead";
        case End::VictimDied: return "victim died";
        case End::Declined: return "declined";
        case End::Expired: return "expired";
        case End::Silent: return "silent";
        case End::Left: return "enemy gone";
        case End::None: break;
    }
    return "none";
}

constexpr uint32_t kGrantExpiryMs = 1500;    // a grant not started (holder) or not answered by a first sample (authority)
constexpr uint32_t kHolderSilenceMs = 2500;  // the holder streamed once and then stopped
constexpr uint32_t kSettleMs = 300;          // after the start the victim's think and the enemy's record settle before "released" counts

// What the holder reads of the victim.
struct VictimView {
    int32_t group = 0;
    int32_t state = 0;
    int32_t hp = 0;
};

// The holder's final state decides how the authority resumes the enemy for these reasons; for the others (the grant never
// ran, the holder is gone or silent) the enemy resumes from the state the authority itself last showed.
constexpr bool carriesState(End end) { return end == End::Released || end == End::EnemyDead || end == End::VictimDied; }

// Why a started lease is over on the holder, or None. `sinceStartMs` is how long ago it started.
constexpr End finished(const VictimView& victim, const enemy_follow_rule::Action& record,
                       const enemy_follow_rule::Action& granted, int32_t enemyHp, uint32_t sinceStartMs) {
    if (enemyHp <= 0) return End::EnemyDead;
    if (victim.hp <= 0 || (victim.group == kReactionGroup && isDeathState(victim.state))) return End::VictimDied;
    if (sinceStartMs < kSettleMs) return End::None;
    return enemy_follow_rule::atBoundary(record, granted) && !held(victim.group, victim.state) ? End::Released : End::None;
}

// The authority gives up on a lease: no sample arrived within kGrantExpiryMs of the grant, or the stream stopped for
// kHolderSilenceMs after it began.
constexpr End awayLapse(bool heard, uint32_t sinceGrantMs, uint32_t sinceHeardMs) {
    if (!heard) return sinceGrantMs > kGrantExpiryMs ? End::Expired : End::None;
    return sinceHeardMs > kHolderSilenceMs ? End::Silent : End::None;
}

// The holder gives up on a grant that arrived `sinceReceivedMs` ago and was not started.
constexpr bool pendingLapsed(uint32_t sinceReceivedMs) { return sinceReceivedMs > kGrantExpiryMs; }

// ---- The holder's stream -----------------------------------------------------------------------------------------

constexpr float kPoseBaseUnits = 1000.0f;       // the authority accepts a holder pose this far from the last one at once
constexpr float kPoseUnitsPerSecond = 12000.0f; // plus this much per second since it
constexpr uint32_t kPoseMaxDtMs = 2500;         // the elapsed time counts at most this long

// A holder pose is taken only when the enemy has not healed past the lease's HP and has not moved farther than a
// walking enemy could since the last accepted pose.
inline bool acceptHolderPose(const float (&previous)[3], const float (&pos)[3], uint32_t dtMs, int32_t hp, int32_t leaseHp) {
    if (hp > leaseHp) return false;
    const float seconds = static_cast<float>(std::min(dtMs, kPoseMaxDtMs)) / 1000.0f;
    return position_blend::distance(previous, pos) <= kPoseBaseUnits + kPoseUnitsPerSecond * seconds;
}

}  // namespace contact_rule
