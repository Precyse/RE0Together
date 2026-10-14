#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "sr2_addresses.h"

// The pure rules of milestone 5h (unit tested): which members a guest has already welcomed, and when a member is due.
namespace guest_welcome_rules {

// A member is welcomed once it is ready. A guest learns a newcomer's state from the host's updates and may skip the ready
// state itself, so "ready or later" counts, not "becomes ready".
inline bool due(uint32_t state) { return state >= sr2::kMemberStateReady; }

// The look sync goes out only once our own member is in the game.
inline bool looksSyncable(uint32_t ownState) { return ownState == sr2::kMemberStateInGame; }

// A member as the set knows it: the session and member objects can be reused, so the member's slot is part of the key.
struct Key {
    uintptr_t session;
    uintptr_t member;
    uint8_t slot;

    bool operator==(const Key&) const = default;
};

// The members that were welcomed.
class WelcomedSet {
public:
    // True the first time `key` is added.
    bool add(const Key& key) {
        if (std::find(keys_.begin(), keys_.end(), key) != keys_.end()) return false;
        keys_.push_back(key);
        return true;
    }

    // Forgets every member of `session` that is not in `present`, and every member of another session.
    void keepOnly(uintptr_t session, const std::vector<Key>& present) {
        keys_.erase(std::remove_if(keys_.begin(), keys_.end(),
                                   [&](const Key& key) {
                                       return key.session != session ||
                                              std::find(present.begin(), present.end(), key) == present.end();
                                   }),
                    keys_.end());
    }

    void forget(const Key& key) { keys_.erase(std::remove(keys_.begin(), keys_.end(), key), keys_.end()); }

    size_t size() const { return keys_.size(); }

private:
    std::vector<Key> keys_;
};

}  // namespace guest_welcome_rules
