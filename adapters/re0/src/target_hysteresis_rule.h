#pragma once
#include <cfloat>
#include <cstddef>
#include <cstdint>

// Pure rule (no game access, unit tested): which character a large enemy keeps chasing. The class's own selector says
// who is nearer ("native"); this rule only lets the enemy change its mind when the new target is clearly better, the
// last switch is old enough, or it was just attacked by the other character.
namespace target_hysteresis_rule {

constexpr uint8_t kNone = 0xFF;                // enemy_protocol::kNoTarget
constexpr float kIneligible = FLT_MAX;         // the engine's distance for a character it cannot target
constexpr size_t kCharacterCount = 2;          // distance[0] controlled, distance[1] partner

struct Tuning {
    float bonus;         // distance taken off the character that attacked, while the attack is recent
    int64_t attackerMs;  // how long an attack counts
    int64_t lockMs;      // after a switch, no other switch (except when the target stops being eligible)
    float margin;        // a new target must be nearer than the current one by at least this
};

// RE0 units are about 1 mm: the logged walk was 192 units in 1 s, and the game's own gates are 200.0 and 500.0.
constexpr Tuning kLargeBossTuning = {.bonus = 3000.0f, .attackerMs = 6000, .lockMs = 3000, .margin = 1500.0f};
constexpr Tuning kStalkerTuning = {.bonus = 1500.0f, .attackerMs = 4000, .lockMs = 2000, .margin = 1000.0f};

enum class Reason : uint8_t { None, Acquired, Ineligible, Attacker, Nearer };

struct Memory {
    uint8_t current = kNone;
    int64_t lockUntil = 0;
    uint8_t attacker = kNone;
    int64_t attackerUntil = 0;
    Reason reason = Reason::None;  // why the last switch happened
};

inline const char* reasonName(Reason reason) {
    switch (reason) {
        case Reason::Acquired: return "acquired";
        case Reason::Ineligible: return "target ineligible";
        case Reason::Attacker: return "attacker";
        case Reason::Nearer: return "nearer";
        case Reason::None: break;
    }
    return "none";
}

namespace detail {

inline float distanceOf(uint8_t character, const float (&distance)[kCharacterCount],
                        const uint8_t (&id)[kCharacterCount]) {
    for (size_t i = 0; i < kCharacterCount; ++i) {
        if (id[i] == character) return distance[i];
    }
    return kIneligible;
}

inline bool attackActive(const Memory& memory, int64_t nowMs) {
    return memory.attacker != kNone && nowMs < memory.attackerUntil;
}

}  // namespace detail

// `native`: the character the class's selector just chose (kNone for none). `distance[i]` is the engine's distance to
// the character `id[i]` (kIneligible when it cannot be targeted). Returns the character to chase, or kNone.
inline uint8_t choose(const Tuning& tuning, Memory& memory, uint8_t native, const float (&distance)[kCharacterCount],
                      const uint8_t (&id)[kCharacterCount], int64_t nowMs) {
    const auto eligible = [&](uint8_t character) {
        return character != kNone && detail::distanceOf(character, distance, id) < kIneligible;
    };
    const auto effective = [&](uint8_t character) {
        const float own = detail::distanceOf(character, distance, id);
        return character == memory.attacker && detail::attackActive(memory, nowMs) ? own - tuning.bonus : own;
    };
    const auto switchTo = [&](uint8_t character, Reason reason) {
        memory.current = character;
        memory.lockUntil = nowMs + tuning.lockMs;
        memory.reason = reason;
    };

    if (!eligible(memory.current)) {
        const uint8_t next = eligible(native) ? native : kNone;
        if (next != memory.current) switchTo(next, memory.current == kNone ? Reason::Acquired : Reason::Ineligible);
        return memory.current;
    }
    if (nowMs < memory.lockUntil) return memory.current;
    const uint8_t attacker = memory.attacker;
    const bool pulled = detail::attackActive(memory, nowMs) && attacker != memory.current && eligible(attacker) &&
                        effective(attacker) + tuning.margin <= effective(memory.current);
    if (pulled) {
        switchTo(attacker, Reason::Attacker);
    } else if (native != memory.current && eligible(native) &&
               effective(native) + tuning.margin <= effective(memory.current)) {
        switchTo(native, Reason::Nearer);
    }
    return memory.current;
}

// A hit by `attacker` landed on the enemy.
inline void noteAttacker(const Tuning& tuning, Memory& memory, uint8_t attacker, int64_t nowMs) {
    if (attacker == kNone) return;
    memory.attacker = attacker;
    memory.attackerUntil = nowMs + tuning.attackerMs;
}

}  // namespace target_hysteresis_rule
