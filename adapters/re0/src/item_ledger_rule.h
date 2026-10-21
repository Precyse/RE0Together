#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "stream_clock.h"

// Rules of the host item ledger (partner_mode=state, docs/RE0_NOTES.md "State mode: item ledger"): floor items are taken
// through the host, which grants each item to the first request and refuses every later one. Pure, no game access,
// unit tested.
namespace item_ledger_rule {

using Vec3 = std::array<float, 3>;

// Two requests name the same floor item when they come from the same scene, carry the same item id and their positions
// are this close (the plan's "pos / 10" key as a distance, so no item sits across a cell border).
constexpr float kSameItemDistance = 10.0f;
// A grant holds the item for its taker until the item leaves the floor (FLOOR_TAKE) or this long passes: the take
// prompt may be declined, which removes nothing.
constexpr uint32_t kReservationMs = 15000;
// A guest's pickup waits this long for the host's answer before it ends.
constexpr uint32_t kHoldTimeoutMs = 2000;
constexpr size_t kMaxEntries = 256;

struct Key {
    uint16_t scene;  // stage << 8 | room
    uint32_t itemId;
    Vec3 pos;
};

// The answer to a take request. The wire carries Granted..Stale (Stale: the request was made in a world from before
// the host's last load, world_epoch_rule.h); TimedOut exists only on the asking machine.
enum class Answer : uint8_t { Granted = 0, Taken = 1, Claimed = 2, Stale = 3, TimedOut = 4 };
constexpr uint8_t kLastWireAnswer = static_cast<uint8_t>(Answer::Stale);

// The reason in a refusal line.
constexpr const char* name(Answer answer) {
    switch (answer) {
        case Answer::Granted: return "granted";
        case Answer::Taken: return "taken";
        case Answer::Claimed: return "claimed";
        case Answer::Stale: return "stale";
        case Answer::TimedOut: return "timeout";
    }
    return "unknown";
}

inline bool sameItem(const Key& a, const Key& b) {
    if (a.scene != b.scene || a.itemId != b.itemId) return false;
    float sum = 0.0f;
    for (size_t axis = 0; axis < a.pos.size(); ++axis) sum += (a.pos[axis] - b.pos[axis]) * (a.pos[axis] - b.pos[axis]);
    return sum <= kSameItemDistance * kSameItemDistance;
}

struct Decision {
    Answer answer;
    uint32_t serial;  // the ledger entry's serial: the grant's own, or the entry that refused
};

// Host: the item ledger. An entry is a grant (reserved for `holder` until it expires) or a consumed item (the journal of
// takes since the last save or load). Numbers every entry 1, 2, 3, ... in the order it was made.
class Ledger {
public:
    // First request wins. The holder asking again is granted again (a declined prompt, picked up a second time); a
    // reservation that ran out is up for grabs.
    Decision request(const Key& key, uint8_t slot, uint32_t nowMs) {
        Entry* entry = find(key);
        if (!entry) return {Answer::Granted, add(key, false, slot, nowMs).serial};
        if (entry->consumed) return {Answer::Taken, entry->serial};
        if (entry->holder == slot) {
            entry->expiresMs = nowMs + kReservationMs;
            return {Answer::Granted, entry->serial};
        }
        if (stream_clock::after(nowMs, entry->expiresMs)) {
            *entry = makeEntry(key, false, slot, nowMs);
            return {Answer::Granted, entry->serial};
        }
        return {Answer::Claimed, entry->serial};
    }

    // The item left the floor (a FLOOR_TAKE, granted here or not): it is consumed whoever held it.
    void taken(const Key& key) {
        if (Entry* entry = find(key)) {
            entry->consumed = true;
            return;
        }
        add(key, true, 0, 0);
    }

    // An item was put on the floor: the entry for that spot describes an item that is gone and is forgotten.
    void put(const Key& key) {
        for (size_t index = entries_.size(); index-- > 0;) {
            if (sameItem(entries_[index].key, key)) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    // The item at `key` is known to have left the floor.
    bool consumed(const Key& key) const {
        for (const Entry& entry : entries_) {
            if (entry.consumed && sameItem(entry.key, key)) return true;
        }
        return false;
    }

    // Every consumed item: the takes since the last save or load, oldest first.
    std::vector<Key> consumedKeys() const {
        std::vector<Key> keys;
        for (const Entry& entry : entries_) {
            if (entry.consumed) keys.push_back(entry.key);
        }
        return keys;
    }

    // A save or a load makes the game's own data current.
    void clear() { entries_.clear(); }

    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        Key key;
        bool consumed;
        uint8_t holder;
        uint32_t expiresMs;
        uint32_t serial;
    };

    Entry makeEntry(const Key& key, bool consumed, uint8_t holder, uint32_t nowMs) {
        return {key, consumed, holder, nowMs + kReservationMs, ++serial_};
    }

    Entry& add(const Key& key, bool consumed, uint8_t holder, uint32_t nowMs) {
        if (entries_.size() >= kMaxEntries) entries_.erase(entries_.begin());
        entries_.push_back(makeEntry(key, consumed, holder, nowMs));
        return entries_.back();
    }

    Entry* find(const Key& key) {
        for (Entry& entry : entries_) {
            if (sameItem(entry.key, key)) return &entry;
        }
        return nullptr;
    }

    std::vector<Entry> entries_;
    uint32_t serial_ = 0;
};

// One pickup's wait for the ledger's answer on the machine that takes (pickup_guard holds the engine's take step while
// it waits). Idle until the first step, then the caller submits the request and reports what came back.
class TakeHold {
public:
    enum class Step : uint8_t {
        Submit,  // nothing asked yet: submit the request
        Wait,    // asked, no answer yet
        Run,     // granted: the engine's step runs
        End      // refused: the pickup ends
    };

    Step step(uint32_t nowMs, std::optional<Answer> arrived) {
        switch (state_) {
            case State::Idle: return Step::Submit;
            case State::Waiting:
                if (arrived) {
                    settle(*arrived);
                } else if (nowMs - sentMs_ >= kHoldTimeoutMs) {
                    settle(Answer::TimedOut);
                }
                return state_ == State::Waiting ? Step::Wait : result();
            case State::Settled: return result();
        }
        return Step::End;
    }

    // The request went out at `nowMs`; the answer is awaited.
    void sent(uint32_t nowMs) {
        sentMs_ = nowMs;
        state_ = State::Waiting;
    }

    // The answer is known at once (the host decides its own take).
    void settle(Answer answer) {
        answer_ = answer;
        state_ = State::Settled;
    }

    Answer answer() const { return answer_; }

    // The next pickup starts over.
    void reset() { state_ = State::Idle; }

private:
    enum class State : uint8_t { Idle, Waiting, Settled };

    Step result() const { return answer_ == Answer::Granted ? Step::Run : Step::End; }

    State state_ = State::Idle;
    Answer answer_ = Answer::Granted;
    uint32_t sentMs_ = 0;
};

}  // namespace item_ledger_rule
