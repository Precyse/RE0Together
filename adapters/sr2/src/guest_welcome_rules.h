#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "sr2_addresses.h"

// The pure rules of milestone 5h and 11 B2 (unit tested): which members a guest has already welcomed, when a member is due,
// and which welcomed members are still owed this guest's look. A welcome is two steps: the objects, queued once when the
// member is ready (state 0xB), and the look, sent once when our own member is in the game (state 0xD). A joiner that arrives
// while this guest is itself still joining is owed the look until then.
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

// The members that were welcomed, each with whether our look was sent to it.
class WelcomedSet {
public:
    // True the first time `key` is added: its objects are queued then, and the look is owed from then on.
    bool add(const Key& key) {
        if (find(key) != entries_.end()) return false;
        entries_.push_back({key, false});
        return true;
    }

    // The welcomed members that are owed the look, each returned once. Nothing is owed before our own member is in the game
    // (`ownInGame`), and never to our own member.
    std::vector<Key> takeOwedLooks(uintptr_t ownMember, bool ownInGame) {
        std::vector<Key> owed;
        if (!ownInGame) return owed;
        for (Entry& entry : entries_) {
            if (entry.lookSent || entry.key.member == ownMember) continue;
            entry.lookSent = true;
            owed.push_back(entry.key);
        }
        return owed;
    }

    // Forgets every member of `session` that is not in `present`, and every member of another session.
    void keepOnly(uintptr_t session, const std::vector<Key>& present) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& entry) {
                                          return entry.key.session != session ||
                                                 std::find(present.begin(), present.end(), entry.key) == present.end();
                                      }),
                       entries_.end());
    }

    void forget(const Key& key) {
        const auto entry = find(key);
        if (entry != entries_.end()) entries_.erase(entry);
    }

    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        Key key;
        bool lookSent;
    };

    std::vector<Entry>::iterator find(const Key& key) {
        return std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.key == key; });
    }

    std::vector<Entry> entries_;
};

}  // namespace guest_welcome_rules
