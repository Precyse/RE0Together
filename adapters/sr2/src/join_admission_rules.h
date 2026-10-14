#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr2_addresses.h"

// The pure rules of the join admission (milestone 9, unit tested). The game's host admits one joiner at a time: a second
// setup_request replaces the first, and any member leaving cancels the open one. Here a joiner who asks while another is
// being admitted waits in a queue, is told to wait, and is admitted when the first is done.
namespace join_admission_rules {

constexpr size_t kMaxWaiting = sr2::kMaxWalkedPlayers;

// An admission is open while its accept dialog shows, its answer is deferred to the end of a mission, or the friends query
// that comes first still runs.
inline bool busy(uint32_t acceptDialog, uint8_t deferred, uintptr_t friendsQueryCallback) {
    return acceptDialog != 0 || deferred != 0 || friendsQueryCallback == sr2::kFriendsQueryHandler;
}

// A request from `sender` waits when another joiner's admission is open.
inline bool mustWait(uintptr_t pendingMember, uintptr_t sender, bool admissionBusy) {
    return pendingMember != 0 && pendingMember != sender && admissionBusy;
}

// The setup_response the game sends to say "wait": id 0x19, the payload length, the type 0 and the code. The header is one
// byte wider when the game's flag says so.
struct Message {
    std::array<uint8_t, 6> bytes{};
    size_t length = 0;
};

inline Message waitMessage(bool wideHeader) {
    constexpr size_t kHeaderBytes = 3;
    constexpr uint8_t kTypeRequest = 0;
    Message message;
    message.length = (wideHeader ? kHeaderBytes + 1 : kHeaderBytes) + 2;
    message.bytes[0] = sr2::kSetupResponseId;
    message.bytes[1] = static_cast<uint8_t>(message.length - kHeaderBytes);
    message.bytes[message.length - 2] = kTypeRequest;
    message.bytes[message.length - 1] = sr2::kSetupResponseWait;
    return message;
}

// The joiners that wait, oldest first, each with the request byte it sent.
class WaitingJoiners {
public:
    struct Entry {
        uintptr_t member;
        uint8_t request;
    };

    // A joiner asking again keeps its place and updates its request.
    void add(uintptr_t member, uint8_t request) {
        for (Entry& entry : entries_) {
            if (entry.member == member) {
                entry.request = request;
                return;
            }
        }
        if (entries_.size() < kMaxWaiting) entries_.push_back({member, request});
    }

    void drop(uintptr_t member) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->member == member) {
                entries_.erase(it);
                return;
            }
        }
    }

    // The oldest joiner for whom `present(member)` holds; those that left before it are forgotten. False when none.
    template <typename Present>
    bool next(Present present, Entry& out) {
        while (!entries_.empty()) {
            const Entry front = entries_.front();
            entries_.erase(entries_.begin());
            if (present(front.member)) {
                out = front;
                return true;
            }
        }
        return false;
    }

    size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

}  // namespace join_admission_rules
