#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "contact_rule.h"
#include "enemy_follow_rule.h"
#include "game.h"

// The lease table of the grab lease (partner_mode=state, unit tested): which enemies this machine has given away
// (Away: it shows them from the holder's stream) and which it holds for the authority (Held: it simulates them natively
// and streams them back). One lease per enemy; a lease lives until it ends, the room changes or the session is lost.
namespace enemy_lease_rule {

enum class Role : uint8_t { None, Away, Held };

struct Lease {
    uint32_t token = 0;
    uint8_t slot = 0;
    uint8_t peer = 0;  // Away: the holder's slot. Held: the authority's.
    uint8_t victim = 0;
    uint8_t classIndex = 0;
    uint16_t spawnId = 0;
    Role role = Role::None;
    enemy_follow_rule::Action granted;  // the record the holder started
    int16_t hp = 0;                     // the enemy's HP at the grant; a streamed pose may only be at or below it
    float lastPos[3] = {};              // Away: the last accepted position. Held: the position of the grant.
    uint32_t quat = 0;                  // Held: the rotation of the grant (pose_wire::packQuat)
    uint32_t startedMs = 0;             // local clock: the grant was sent (Away) or started (Held)
    uint32_t heardMs = 0;               // Away: local clock of the last accepted pose
    bool heard = false;                 // Away: a pose was accepted
};

// A machine streams an enemy to the other when it simulates its room and has not given the enemy away, or when it
// holds a lease on it.
constexpr bool streams(bool simulatesRoom, Role role) {
    return role == Role::Held || (simulatesRoom && role != Role::Away);
}

// A token whose low 16 bits are never 0: they ride in every streamed pose to name the lease.
constexpr uint32_t nextToken(uint32_t previous) {
    uint32_t token = previous + 1;
    while ((token & 0xffff) == 0) ++token;
    return token;
}

enum class PoseVerdict : uint8_t { Accepted, NoLease, WrongToken, Rejected };

constexpr const char* name(PoseVerdict verdict) {
    switch (verdict) {
        case PoseVerdict::NoLease: return "no lease";
        case PoseVerdict::WrongToken: return "wrong token";
        case PoseVerdict::Rejected: return "hp or displacement";
        case PoseVerdict::Accepted: break;
    }
    return "accepted";
}

struct Lapsed {
    uint8_t slot;
    uint32_t token;
    contact_rule::End reason;
};

class LeaseTable {
public:
    void clear() { leases_ = {}; }

    // False when the enemy already has a lease.
    bool add(const Lease& lease) {
        if (lease.slot >= leases_.size() || leases_[lease.slot]) return false;
        leases_[lease.slot] = lease;
        return true;
    }

    const Lease* find(uint8_t slot) const {
        return slot < leases_.size() && leases_[slot] ? &*leases_[slot] : nullptr;
    }

    Role roleOf(uint8_t slot) const {
        const Lease* lease = find(slot);
        return lease ? lease->role : Role::None;
    }

    bool any(Role role) const {
        for (const std::optional<Lease>& lease : leases_) {
            if (lease && lease->role == role) return true;
        }
        return false;
    }

    // Removes the lease when `token` is its token; the removed lease.
    std::optional<Lease> remove(uint8_t slot, uint32_t token) {
        if (slot >= leases_.size() || !leases_[slot] || leases_[slot]->token != token) return std::nullopt;
        std::optional<Lease> removed = leases_[slot];
        leases_[slot].reset();
        return removed;
    }

    // Away: a pose the holder streamed for the enemy in `slot`, named by the low 16 bits of the lease token. Accepted
    // poses move the baseline: the position they put the enemy at, and an HP that can only fall.
    PoseVerdict offerPose(uint8_t slot, uint16_t tokenLow, const float (&pos)[3], int32_t hp, uint32_t nowMs) {
        if (slot >= leases_.size() || !leases_[slot] || leases_[slot]->role != Role::Away) return PoseVerdict::NoLease;
        Lease& lease = *leases_[slot];
        if ((lease.token & 0xffff) != tokenLow) return PoseVerdict::WrongToken;
        const uint32_t sinceMs = nowMs - (lease.heard ? lease.heardMs : lease.startedMs);
        if (!contact_rule::acceptHolderPose(lease.lastPos, pos, sinceMs, hp, lease.hp)) return PoseVerdict::Rejected;
        for (int i = 0; i < 3; ++i) lease.lastPos[i] = pos[i];
        if (hp < lease.hp) lease.hp = static_cast<int16_t>(hp);
        lease.heard = true;
        lease.heardMs = nowMs;
        return PoseVerdict::Accepted;
    }

    // Away leases the holder let lapse (never answered, or went silent); they are removed.
    std::vector<Lapsed> lapsed(uint32_t nowMs) {
        std::vector<Lapsed> out;
        for (std::optional<Lease>& place : leases_) {
            if (!place || place->role != Role::Away) continue;
            const contact_rule::End reason =
                contact_rule::awayLapse(place->heard, nowMs - place->startedMs, nowMs - place->heardMs);
            if (reason == contact_rule::End::None) continue;
            out.push_back({place->slot, place->token, reason});
            place.reset();
        }
        return out;
    }

private:
    std::array<std::optional<Lease>, game::kEnemyPoolSlots> leases_{};
};

}  // namespace enemy_lease_rule
