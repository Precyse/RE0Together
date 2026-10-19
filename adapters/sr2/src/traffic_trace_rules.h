#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "population_rules.h"

// The pure rules of the traffic trace (milestone 15 B1, trace=1, unit tested): which player a vehicle is near, the vehicles that
// live farther from their owner than from someone else, the spawn and despawn counts per owner and area, and the lines. The
// quota is population_rules.h's.
namespace traffic_trace_rules {

using population_rules::Player;
using population_rules::Point;

constexpr size_t kSlots = 8;  // the session slots of a game for 8 players
constexpr float kFarOwnedRadius = 150.0f;

// The player a position is nearest to: false without players. A tie goes to the lower slot.
inline bool nearestPlayer(const Point& at, const Player* players, size_t count, uint8_t& slot, float& distance) {
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        const float apart = population_rules::distance(at, players[i].position);
        if (!found || apart < distance || (apart == distance && players[i].slot < slot)) {
            found = true;
            distance = apart;
            slot = players[i].slot;
        }
    }
    return found;
}

// An owned traffic vehicle that lives far from its owner while another player is nearer: more than kFarOwnedRadius from the
// owner and nearer to someone else. `others` are the players other than the owner.
struct FarOwned {
    bool isFar = false;
    uint8_t nearestSlot = 0;
    float distance = 0;
};

inline FarOwned farOwned(const Point& vehicle, const Player& owner, const Player* others, size_t count) {
    FarOwned result;
    const float fromOwner = population_rules::distance(vehicle, owner.position);
    if (fromOwner <= kFarOwnedRadius) return result;
    float nearest = 0;
    uint8_t slot = 0;
    if (!nearestPlayer(vehicle, others, count, slot, nearest) || nearest >= fromOwner) return result;
    result = {true, slot, nearest};
    return result;
}

// Events counted by the machine that owns the vehicle and the player it is nearest to.
class OwnerAreaCounts {
public:
    void add(uint8_t owner, uint8_t area) {
        ++counts_[clamp(owner)][clamp(area)];
        ++total_;
    }
    uint32_t total() const { return total_; }
    void clear() {
        counts_ = {};
        total_ = 0;
    }

    // "by slot S near slot A n, by slot S near slot A m" for each pair seen, none for no event.
    std::string describe() const {
        std::string text;
        for (size_t owner = 0; owner < kSlots; ++owner) {
            for (size_t area = 0; area < kSlots; ++area) {
                if (counts_[owner][area] == 0) continue;
                char part[64];
                std::snprintf(part, sizeof(part), "%sby slot %zu near slot %zu %u", text.empty() ? "" : ", ", owner, area, counts_[owner][area]);
                text += part;
            }
        }
        return text.empty() ? "none" : text;
    }

private:
    static size_t clamp(uint8_t slot) { return slot < kSlots ? slot : kSlots - 1; }

    std::array<std::array<uint32_t, kSlots>, kSlots> counts_ = {};
    uint32_t total_ = 0;
};

// The line once a minute: the spawns and despawns since the last line and the owned vehicles far from their owner.
inline std::string trafficLine(const OwnerAreaCounts& spawned, const OwnerAreaCounts& despawned, size_t farOwnedCount, const FarOwned& example) {
    char farText[96];
    if (farOwnedCount == 0) {
        std::snprintf(farText, sizeof(farText), "0");
    } else {
        std::snprintf(farText, sizeof(farText), "%zu (nearest other slot %u at %.0f)", farOwnedCount, static_cast<unsigned>(example.nearestSlot), example.distance);
    }
    return "trace: traffic: spawned " + spawned.describe() + "; despawned " + despawned.describe() + "; owned far from owner " + farText;
}

// The line on every change of the quota or of the leader set.
inline std::string quotaLine(const population_rules::Quota& quota, int32_t limit, size_t owned) {
    std::string slots;
    for (size_t i = 0; i < quota.leads; ++i) {
        char part[16];
        std::snprintf(part, sizeof(part), "%s%u", i == 0 ? "" : ",", static_cast<unsigned>(quota.led[i]));
        slots += part;
    }
    char line[160];
    std::snprintf(line, sizeof(line), "trace: traffic: quota %d of %d, leads %zu of %zu (slots {%s}), owned %zu", quota.quota, limit, quota.leads,
                  quota.players, slots.c_str(), owned);
    return line;
}

// What a change of the quota line is: the quota, the limit and the set of players this machine leads. The owned count moves all
// the time and is not part of it.
inline bool sameQuota(const population_rules::Quota& a, int32_t limitA, const population_rules::Quota& b, int32_t limitB) {
    if (a.quota != b.quota || limitA != limitB || a.leads != b.leads || a.players != b.players) return false;
    for (size_t i = 0; i < a.leads; ++i) {
        if (a.led[i] != b.led[i]) return false;
    }
    return true;
}

}  // namespace traffic_trace_rules
