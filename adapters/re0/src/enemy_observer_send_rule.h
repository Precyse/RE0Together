#pragma once
#include <array>
#include <cstdint>

#include "game.h"

// What the simulator sends for each pool slot in each ENEMY_POSE round (pure, unit tested).
namespace enemy_observer_send_rule {

constexpr uint32_t kPoseIntervalMs = 50;      // 20 Hz
constexpr uint32_t kCorpseEveryRounds = 20;   // a settled corpse is sampled again once a second, staggered by slot
constexpr uint32_t kRoundsPerSecond = 1000 / kPoseIntervalMs;
static_assert(kCorpseEveryRounds == kRoundsPerSecond);
// A death plays its fall over a few seconds (a zombie killed getting up: hit 0x1a, fall 0x12, lying 0x16 about 2.4 s
// later): the dying enemy is posed every round for this long, or the observer's copy plays its own motions between
// samples (finishes standing up) and snaps to the fall a second later.
constexpr uint32_t kDyingRounds = 5 * kRoundsPerSecond;

enum class Action : uint8_t {
    Skip,      // nothing this round
    Pose,      // a living enemy, in the unreliable batch
    Terminal,  // it just died (or was first seen dead): once, reliably
    Corpse,    // a dead enemy again, in the unreliable batch: every round while dying, then once a second
    Removed,   // the unit left the pool (or another enemy took the slot): once, reliably, with the old enemy's identity
};

// Decides per slot from what the slot held in the previous rounds. `present` = an Observer-class enemy with a known
// spawn stands in the slot, `alive` = its HP is above 0, `identity` changes when the slot holds another enemy.
class SendPlanner {
public:
    void clear() { slots_ = {}; }

    void startRound() { ++round_; }

    Action next(uint8_t slot, bool present, bool alive, uint32_t identity) {
        Memory& memory = slots_[slot];
        if (memory.present && (!present || memory.identity != identity)) {
            memory.present = false;
            return Action::Removed;
        }
        if (!present) return Action::Skip;
        if (!memory.present) memory = {true, false, 0, identity};
        if (alive) {
            memory.deadSent = false;
            return Action::Pose;
        }
        if (!memory.deadSent) {
            memory.deadSent = true;
            memory.deadRounds = 0;
            return Action::Terminal;
        }
        if (memory.deadRounds < kDyingRounds) {
            ++memory.deadRounds;
            return Action::Corpse;
        }
        return (round_ + slot) % kCorpseEveryRounds == 0 ? Action::Corpse : Action::Skip;
    }

private:
    struct Memory {
        bool present = false;
        bool deadSent = false;
        uint32_t deadRounds = 0;  // rounds since the terminal entry, up to kDyingRounds
        uint32_t identity = 0;
    };

    std::array<Memory, game::kEnemyPoolSlots> slots_{};
    uint32_t round_ = 0;
};

}  // namespace enemy_observer_send_rule
