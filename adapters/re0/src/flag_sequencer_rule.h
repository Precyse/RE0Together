#pragma once
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "flag_diff.h"
#include "flag_sequencer_wire.h"
#include "pose_timeline.h"

// Rules of the host-ordered story flags (partner_mode=state, docs/RE0_NOTES.md "State mode: flags"). The host applies
// proposals in arrival order and numbers every commit; a guest checks the numbers and reports the bits where a commit
// overrode a value the guest had proposed itself. Pure, no game access, unit tested.
namespace flag_sequencer_rule {

using flag_diff::Change;
using flag_diff::Words;
using flag_sequencer_wire::Commit;

// Most proposals a guest remembers as unanswered; the oldest is forgotten beyond this.
constexpr size_t kMaxUnanswered = 64;

// The bits of `change` that `words` do not already show: set bits that are clear, clear bits that are set. A change
// for a word outside the bitset has none.
inline Change effectiveBits(const Words& words, const Change& change) {
    if (change.word >= flag_diff::kWords) return {change.word, 0, 0, 0};
    const uint32_t current = words[change.word];
    return {change.word, 0, change.set & ~current, change.clear & current};
}

// `proposal` reduced to the bits that change `words`; words that would not change are dropped.
inline std::vector<Change> effective(const Words& words, const std::vector<Change>& proposal) {
    std::vector<Change> changes;
    for (const Change& change : proposal) {
        const Change bits = effectiveBits(words, change);
        if (bits.set | bits.clear) changes.push_back(bits);
    }
    return changes;
}

// Number of bits in a mask.
constexpr uint32_t bitCount(uint32_t mask) {
    uint32_t count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}

// Host: numbers commits 1, 2, 3, ... in the order it issues them.
class HostOrder {
public:
    Commit issue(uint8_t origin, std::vector<Change> changes) { return {++serial_, origin, std::move(changes)}; }

private:
    uint32_t serial_ = 0;
};

enum class Verdict : uint8_t { InOrder, Duplicate, Gap };

// Guest: commits must arrive numbered one after the other. The first commit after a reset sets the baseline (the join
// snapshot already holds everything before it); a gap restarts the baseline at the commit that revealed it.
class SerialTracker {
public:
    Verdict accept(uint32_t serial) {
        if (!hasLast_ || serial == last_ + 1) return take(serial, Verdict::InOrder);
        if (!pose_timeline::after(serial, last_)) return Verdict::Duplicate;
        return take(serial, Verdict::Gap);
    }
    uint32_t expected() const { return last_ + 1; }
    void reset() { hasLast_ = false; }

private:
    Verdict take(uint32_t serial, Verdict verdict) {
        last_ = serial;
        hasLast_ = true;
        return verdict;
    }

    uint32_t last_ = 0;
    bool hasLast_ = false;
};

// Guest: the proposals the host has not answered yet (it answers each in order, with an empty commit when it changed
// nothing).
class ProposalLedger {
public:
    void proposed(const std::vector<Change>& changes) {
        if (pending_.size() >= kMaxUnanswered) pending_.pop_front();
        pending_.push_back(changes);
    }
    void answered() {
        if (!pending_.empty()) pending_.pop_front();
    }
    // The bits of `word` some unanswered proposal touches.
    uint32_t touched(uint16_t word) const {
        uint32_t mask = 0;
        for (const std::vector<Change>& proposal : pending_) {
            for (const Change& change : proposal) {
                if (change.word == word) mask |= change.set | change.clear;
            }
        }
        return mask;
    }
    void reset() { pending_.clear(); }

private:
    std::deque<std::vector<Change>> pending_;
};

// What a guest does with one commit.
struct Receipt {
    bool apply = false;            // false: an old commit seen again
    bool gap = false;              // serials were skipped: ask the host for a snapshot
    uint32_t expected = 0;         // the serial the guest expected (what a gap skipped to is the commit's own)
    std::vector<Change> overrode;  // bits that differ from the guest's value and that the guest had proposed itself
};

// Guest: orders commits and tells which committed bits override the guest's own unanswered changes.
class GuestOrder {
public:
    void proposed(const std::vector<Change>& changes) { ledger_.proposed(changes); }

    // `model` holds the guest's flags and takes the commit's bits when the commit applies.
    Receipt receive(const Commit& commit, uint8_t localSlot, Words& model) {
        Receipt receipt;
        receipt.expected = tracker_.expected();
        const Verdict verdict = tracker_.accept(commit.serial);
        if (verdict == Verdict::Duplicate) return receipt;
        receipt.apply = true;
        receipt.gap = verdict == Verdict::Gap;
        if (commit.origin == localSlot) ledger_.answered();
        for (const Change& change : commit.changes) {
            Change bits = effectiveBits(model, change);
            const uint32_t own = ledger_.touched(change.word);
            bits.set &= own;
            bits.clear &= own;
            if (bits.set | bits.clear) receipt.overrode.push_back(bits);
            flag_diff::apply(model, change);
        }
        return receipt;
    }

    void reset() {
        tracker_.reset();
        ledger_.reset();
    }

private:
    SerialTracker tracker_;
    ProposalLedger ledger_;
};

}  // namespace flag_sequencer_rule
