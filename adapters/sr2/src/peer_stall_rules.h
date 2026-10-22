#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// The pure rules of a stalled peer (unit tested; docs/SR2_NOTES.md, "Stalled peers and rejoins"). The game times a member out
// after 60 s without a datagram (0x7FA9E0, member +0x2240 against the frame clock 0x252A3CC) and until then shows its last
// pose: a peer whose game thread stopped stood frozen in every other world for a minute. Here a remote member that sent
// nothing for kStallMs has its human hidden on this machine, and shown again when it sends again. Nothing is sent to it again:
// deltacomp has no baseline left after a stall and sends full state, and the reliable layer resends every unacked message.
namespace peer_stall_rules {

constexpr uint32_t kStallMs = 3000;    // 10 to 30 datagrams a second per peer: 3 s of none is a stopped game, not loss

// The game's silence of a member: the frame clock minus the member's last receive time, as 0x7FA9E0 measures it (the clock
// wraps; the game takes one more off a wrapped difference).
constexpr uint32_t silenceOf(uint32_t clock, uint32_t lastReceive) {
    const uint32_t silence = clock - lastReceive;
    return clock < lastReceive ? silence - 1 : silence;
}

// The receive rate a peer reports when it is asked (0x16, the host's loss vote): 0x815120 measures the bytes received in the
// last 10 s, and answers 0 when it has no sample there (the first frame after a stall, or a long load) and at least 0x2000
// otherwise. A 0 broadcast (0x17, 0x80B620 stores it in member +0x2200) holds every sender at 10 packets a second to this peer
// for minutes; so no measurement is no report, and the senders keep the rate they had until the next vote asks again.
constexpr bool reportsRate(uint32_t measured) { return measured != 0; }

struct Key {
    uintptr_t member;
    uint8_t slot;
    bool operator==(const Key&) const = default;
};

enum class Change {
    None,
    Hide,  // just stalled: hide its human
    Show,  // sends again: show its human
};

struct Step {
    Change change;
    uint32_t longestMs;  // the silence that hid it (Hide), the longest silence of the stall that just ended (Show)
};

// The members whose humans this machine hid, each with the longest silence seen.
class Watch {
public:
    // One member with a human at this tick. A member that is not held and is silent for kStallMs becomes held (Hide); a held
    // member that is silent less than that again is let go (Show).
    Step update(Key key, uint32_t silenceMs) {
        const auto held = find(key);
        const bool stalled = silenceMs >= kStallMs;
        if (held == held_.end()) {
            if (!stalled) return {Change::None, 0};
            held_.push_back({key, silenceMs});
            return {Change::Hide, silenceMs};
        }
        if (stalled) {
            held->longestMs = std::max(held->longestMs, silenceMs);
            return {Change::None, held->longestMs};
        }
        const uint32_t longest = held->longestMs;
        held_.erase(held);
        return {Change::Show, longest};
    }

    bool held(Key key) const { return std::any_of(held_.begin(), held_.end(), [&](const Entry& e) { return e.key == key; }); }

    // Members that left the session (or lost their human) are forgotten: a rejoiner is a new member.
    template <typename Present>
    void keepOnly(Present present) {
        held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Entry& e) { return !present(e.key); }), held_.end());
    }

    size_t size() const { return held_.size(); }

private:
    struct Entry {
        Key key;
        uint32_t longestMs;
    };
    std::vector<Entry>::iterator find(Key key) {
        return std::find_if(held_.begin(), held_.end(), [&](const Entry& e) { return e.key == key; });
    }
    std::vector<Entry> held_;
};

}  // namespace peer_stall_rules
