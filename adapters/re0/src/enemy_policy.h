#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "game.h"

// How partner_mode=state replicates each of the 38 enemy classes (STATE_ENEMIES 6.4; pure, unit tested).
//   Observer: the machine that does not run the room shows the enemy from the simulator's stream, the engine's own
//             CharPause path animating it (enemy_observer).
//   Legacy:   today's decision replay stays (enemy_decision, enemy_state, HIT_APPLIED) until probe X2 decides phase 18:
//             the classes that pose their body from code or spawn and own other units.
//   Native:   runs natively on both machines (event actors, stubs, static objects).
namespace enemy_policy {

enum class Policy : uint8_t { Observer, Legacy, Native };

struct EnemyClass {
    uintptr_t vtable;
    const char* name;
    Policy policy;
};

constexpr size_t kClassCount = game::kEnemyVtables.size();
constexpr int kNoClass = -1;

// In the order of game::kEnemyVtables: the index is the class on the wire.
constexpr std::array<EnemyClass, 38> kClasses = {{
    {0xcbdcd8, "uEnemy10", Policy::Observer},
    {0xcc4f28, "uEnemy11", Policy::Observer},
    {0xcc50a0, "uEnemy12", Policy::Observer},
    {0xcc5218, "uEnemy14", Policy::Observer},
    {0xcc5390, "uEnemy15", Policy::Observer},
    {0xcc5508, "uEnemy16", Policy::Observer},
    {0xcc5680, "uEnemy17", Policy::Observer},
    {0xcc57f8, "uEnemy19", Policy::Observer},
    {0xcc5970, "uEnemy1a", Policy::Observer},
    {0xcc3fe8, "uEnemy2a", Policy::Observer},
    {0xcbf0d0, "uEnemy2b", Policy::Observer},
    {0xcbf5f8, "uEnemy2c", Policy::Observer},
    {0xcbf9a0, "uEnemy2f", Policy::Observer},
    {0xcbfbc8, "uEnemy30", Policy::Observer},
    {0xcbfe58, "uEnemy31", Policy::Observer},
    {0xcc01a8, "uEnemy34", Policy::Observer},
    {0xcc0458, "uEnemy35", Policy::Observer},
    {0xcc08c0, "uEnemy37", Policy::Observer},
    {0xcc5ae8, "uEnemy38", Policy::Observer},
    {0xcc0cb0, "uEnemy39", Policy::Observer},
    {0xcc13d8, "uEnemy3a", Policy::Legacy},
    {0xcc1618, "uEnemy3bRebecca", Policy::Native},
    {0xcc18e8, "uEnemy3c", Policy::Observer},
    {0xcc5c60, "uEnemy3e", Policy::Legacy},
    {0xcc1ce8, "uEnemy40", Policy::Observer},
    {0xcc1e60, "uEnemy41", Policy::Observer},
    {0xcc1fd8, "uEnemy42", Policy::Observer},
    {0xcc25d8, "uEnemy43", Policy::Legacy},
    {0xcc2b38, "uEnemy44", Policy::Native},
    {0xcc2f70, "uEnemy45", Policy::Native},
    {0xcc3248, "uEnemy46", Policy::Observer},
    {0xcc3710, "uEnemy47", Policy::Legacy},
    {0xcc5dd8, "uEnemy50", Policy::Observer},
    {0xcc5f50, "uEnemy51", Policy::Observer},
    {0xcc60c8, "uEnemy5a", Policy::Observer},
    {0xcc6240, "uEnemy5b", Policy::Observer},
    {0xcc63b8, "uEnemy6a", Policy::Observer},
    {0xcc6530, "uEnemy6b", Policy::Observer},
}};

constexpr bool sameOrderAsGame() {
    for (size_t i = 0; i < kClassCount; ++i) {
        if (kClasses[i].vtable != game::kEnemyVtables[i]) return false;
    }
    return true;
}
static_assert(sameOrderAsGame(), "the class index on the wire is the position in game::kEnemyVtables");

// Index of the class with this vtable, or kNoClass.
constexpr int classIndexOf(uintptr_t vtable) {
    for (size_t i = 0; i < kClassCount; ++i) {
        if (kClasses[i].vtable == vtable) return static_cast<int>(i);
    }
    return kNoClass;
}

// Policy of an enemy class by vtable; anything that is not one of the 38 classes is Native (never replicated).
constexpr Policy policyOf(uintptr_t vtable) {
    const int index = classIndexOf(vtable);
    return index == kNoClass ? Policy::Native : kClasses[static_cast<size_t>(index)].policy;
}

// Name of the class at a wire index; "?" past the table.
constexpr const char* nameOf(size_t classIndex) { return classIndex < kClassCount ? kClasses[classIndex].name : "?"; }

}  // namespace enemy_policy
