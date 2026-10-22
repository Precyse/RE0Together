#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "partner_rules.h"
#include "sr2_addresses.h"

// The pure rules of the anim and asset streaming per remote (milestone 16 W1, unit tested; docs/SR2_NOTES.md "Anim and asset
// streaming per remote (milestone 16 W1)"): who a streaming_sync message goes to and who it may run for, the need row masks, the
// size and the relocation of the need table, the ledger of stream requests per member, the set change time gates per sender and
// the activity trigger presets per member. A slot is a member's session slot (+0x1B0).
namespace anim_stream_rules {

// The last mask bit: a slot of 7 or more shares it, and a need set outside any message handler (a guest's rows are the host's) uses it.
constexpr uint8_t kSharedBitSlot = 7;
constexpr size_t kRowsPerPlayer = 3;  // chunk anim slots: the rows one machine needs at once
constexpr size_t kSpareRows = 2;

// ---- streaming_sync (message 0x7E) ----

enum class Recipient : uint8_t { Host, HandlerSender, Broadcast };

// Who a sub of the message goes to. A request and a drop (a release) go to the host only, an answer to the member whose request
// the handler is running; with no such member, and for any other sub, to everyone as shipped.
constexpr Recipient recipient(uint8_t sub, bool haveHandlerSender) {
    switch (sub) {
        case sr2::kStreamingSubRequest:
        case sr2::kStreamingSubDrop: return Recipient::Host;
        case sr2::kStreamingSubAnswer: return haveHandlerSender ? Recipient::HandlerSender : Recipient::Broadcast;
        default: return Recipient::Broadcast;
    }
}

enum class Verdict : uint8_t { Run, NotHost, AnswerOnHost, NotFromHost, NoPendingRequest };

// Whether the game's handler 0xA83F30 may run for a message: requests and drops are for the host, an answer only for a guest, only
// from the host, and only for a group it has a pending request for (a stale answer would load a set nothing tracks).
constexpr Verdict gate(uint8_t sub, bool thisIsHost, bool senderIsHost, bool groupPending) {
    switch (sub) {
        case sr2::kStreamingSubRequest:
        case sr2::kStreamingSubDrop: return thisIsHost ? Verdict::Run : Verdict::NotHost;
        case sr2::kStreamingSubAnswer:
            if (thisIsHost) return Verdict::AnswerOnHost;
            if (!senderIsHost) return Verdict::NotFromHost;
            return groupPending ? Verdict::Run : Verdict::NoPendingRequest;
        default: return Verdict::Run;
    }
}

// The reason in the log line of a dropped message.
constexpr const char* dropText(Verdict verdict) {
    switch (verdict) {
        case Verdict::NotHost: return "not the host";
        case Verdict::AnswerOnHost: return "answer on the host";
        case Verdict::NotFromHost: return "answer not from the host";
        case Verdict::NoPendingRequest: return "no pending request";
        default: return "";
    }
}

// ---- the need rows ----

constexpr uint8_t slotBit(uint8_t slot) { return static_cast<uint8_t>(1u << std::min(slot, kSharedBitSlot)); }

// A remote row's mask after a set by `owner` (the member whose message is handled); with none, the shared bit.
constexpr uint8_t maskAfterSet(uint8_t mask, std::optional<uint8_t> owner) {
    return static_cast<uint8_t>(mask | slotBit(owner.value_or(kSharedBitSlot)));
}

// A remote row's mask after a clear by `owner`; with none (a guest releasing its own need), every bit.
constexpr uint8_t maskAfterClear(uint8_t mask, std::optional<uint8_t> owner) {
    return owner ? static_cast<uint8_t>(mask & ~slotBit(*owner)) : uint8_t{0};
}

// The rows the need table needs for `maxPlayers`: 3 chunk anim slots of the host and at most 3 distinct groups per guest, plus 2.
// Two players give the shipped 8.
constexpr size_t needRows(unsigned maxPlayers) { return kRowsPerPlayer * maxPlayers + kSpareRows; }

// The value of one of the immediates that reach the need table (a base or an end plus a field offset) once the table is `rows` rows
// at `newBase`; any other value stays.
constexpr uint32_t relocatedNeedRef(uint32_t old, uint32_t newBase, size_t rows) {
    const uint32_t rowBytes = static_cast<uint32_t>(sr2::kAnimNeedRowBytes);
    if (old >= sr2::kAnimNeedTable && old - sr2::kAnimNeedTable < rowBytes) return newBase + (old - sr2::kAnimNeedTable);
    if (old >= sr2::kAnimNeedTableEnd && old - sr2::kAnimNeedTableEnd < rowBytes) {
        return newBase + static_cast<uint32_t>(rows) * rowBytes + (old - sr2::kAnimNeedTableEnd);
    }
    return old;
}

// ---- the answer the game's 0xA83340 would load ----

// A request this machine made: the anim group and the id of the set it asked for.
struct Pending {
    uint16_t group;
    uint32_t id;
};

struct PendingLookup {
    bool found;
    uint32_t id;
};

// The first pending entry of `group`, as 0xA83340 scans the list.
inline PendingLookup pendingFor(std::span<const Pending> list, uint16_t group) {
    const auto at = std::find_if(list.begin(), list.end(), [group](const Pending& entry) { return entry.group == group; });
    return at == list.end() ? PendingLookup{false, 0} : PendingLookup{true, at->id};
}

// 0xA83340: loads when the group is already loaded or is the one being streamed, and the pending entry (when there is one) names this id.
constexpr bool answerWouldLoad(bool chunkLoaded, bool stateNames, PendingLookup pending, uint32_t id) {
    return (chunkLoaded || stateNames) && (!pending.found || pending.id == id);
}

// ---- the stream requests per member ----

enum class RequestKind : uint8_t { World, Character };

// What a stream request names: a world one {id, category, key}, a character one {name hash as id, category}.
struct RequestKey {
    RequestKind kind;
    uint32_t id;
    int8_t category;
    uint32_t key;  // 0 for a character request

    friend bool operator==(const RequestKey&, const RequestKey&) = default;
};

struct HeldRequest {
    RequestKey key;
    uint32_t count;
};

// What each member has asked this machine to keep streamed: an add is +1, a remove -1 and never below 0. A member that leaves has
// its requests released by replaying its removes. Per session: a new session starts empty.
class RequestLedger {
public:
    static constexpr size_t kSlots = sr2::kMaxReplicatedPlayers;
    static constexpr size_t kMaxKeysPerSlot = 64;

    void reset(uintptr_t session) {
        if (session == session_) return;
        session_ = session;
        for (std::vector<HeldRequest>& held : slots_) held.clear();
        overflowed_ = false;
    }

    // False when the slot already holds the most keys and this one is new: it is not recorded.
    bool add(uint8_t slot, const RequestKey& key) {
        if (slot >= kSlots) return false;
        std::vector<HeldRequest>& held = slots_[slot];
        const auto at = find(held, key);
        if (at != held.end()) {
            ++at->count;
            return true;
        }
        if (held.size() >= kMaxKeysPerSlot) {
            overflowed_ = true;
            return false;
        }
        held.push_back({key, 1});
        return true;
    }

    void remove(uint8_t slot, const RequestKey& key) {
        if (slot >= kSlots) return;
        std::vector<HeldRequest>& held = slots_[slot];
        const auto at = find(held, key);
        if (at == held.end()) return;
        if (--at->count == 0) held.erase(at);
    }

    // What the slot holds, which is then empty.
    std::vector<HeldRequest> take(uint8_t slot) {
        std::vector<HeldRequest> taken;
        if (slot < kSlots) taken.swap(slots_[slot]);
        return taken;
    }

    // The requests the slot holds (the sum of its counts).
    uint32_t held(uint8_t slot) const {
        uint32_t total = 0;
        if (slot < kSlots) {
            for (const HeldRequest& request : slots_[slot]) total += request.count;
        }
        return total;
    }

    bool overflowed() const { return overflowed_; }

private:
    static std::vector<HeldRequest>::iterator find(std::vector<HeldRequest>& held, const RequestKey& key) {
        return std::find_if(held.begin(), held.end(), [&key](const HeldRequest& request) { return request.key == key; });
    }

    uintptr_t session_ = 0;
    bool overflowed_ = false;
    std::array<std::vector<HeldRequest>, kSlots> slots_;
};

// ---- the set change time gates per sender ----

// The game keeps 18 time gates for strm_set_change (a set is loaded when its gate is not past the message's time), shared by every
// sender. Here each sender slot has its own bank, so one sender's later time no longer skips another's earlier set. A shadow keeps
// what the one shared bank would hold, to tell which loads the shipped game would have skipped.
class SenderGates {
public:
    static constexpr size_t kSlots = sr2::kMaxReplicatedPlayers;
    using Bank = std::array<uint32_t, sr2::kSetChangeGateCount>;

    // The slot's bank. A member other than the one that last used the slot starts at 0.
    Bank load(uint8_t slot, uintptr_t member) {
        if (slot >= kSlots) return Bank{};
        if (members_[slot] != member) {
            members_[slot] = member;
            banks_[slot] = Bank{};
        }
        return banks_[slot];
    }

    void store(uint8_t slot, const Bank& values) {
        if (slot < kSlots) banks_[slot] = values;
    }

    void resetAll() {
        for (Bank& bank : banks_) bank = Bank{};
        shared_ = Bank{};
        sharedSlots_.fill(0);
    }

    // The shared gate would hold a time past `time`: the shipped game skips this load.
    bool sharedWouldSkip(size_t index, uint32_t time) const { return index < shared_.size() && shared_[index] > time; }

    // A load at `time` by `slot` that the shared gate lets through is written to it.
    void noteLoad(size_t index, uint32_t time, uint8_t slot) {
        if (index >= shared_.size() || sharedWouldSkip(index, time)) return;
        shared_[index] = time;
        sharedSlots_[index] = slot;
    }

    uint32_t sharedTime(size_t index) const { return index < shared_.size() ? shared_[index] : 0; }
    uint8_t sharedSlot(size_t index) const { return index < sharedSlots_.size() ? sharedSlots_[index] : 0; }

private:
    std::array<Bank, kSlots> banks_{};
    std::array<uintptr_t, kSlots> members_{};
    Bank shared_{};
    std::array<uint8_t, sr2::kSetChangeGateCount> sharedSlots_{};
};

// ---- the activity trigger character sets per member ----

using partner_rules::kNoSlot;
constexpr uint8_t kPartnerCategoryVanilla = 4;  // the category of the game's own partner load, and under Juiced
constexpr uint8_t kPartnerCategoryJuiced = 6;

// Only these two categories are known to release the set a partner load took; any other byte at 0x685750 is not trusted.
constexpr bool releasableCategory(uint8_t category) {
    return category == kPartnerCategoryVanilla || category == kPartnerCategoryJuiced;
}

// A preset held for a member's slot.
struct SlotPreset {
    uint8_t slot;
    uint32_t preset;
};

// The preset the game's Set_Player_Character keeps for "the partner" in [0x2527B68], one per member. The default partner's entry is
// the one the game's own code keeps in [0x2527B68].
class TriggerSlots {
public:
    static constexpr size_t kSlots = sr2::kMaxReplicatedPlayers;

    // The game's [0x2527B68] now belongs to the default partner `slot`.
    void syncDefault(uint8_t slot, uint32_t value) { swapOut(slot, value); }

    // The value of [0x2527B68] while a message of `sender` is handled.
    uint32_t swapIn(uint8_t sender) const { return sender < kSlots ? presets_[sender] : 0; }

    // What the handler left in [0x2527B68] belongs to `sender`.
    void swapOut(uint8_t sender, uint32_t value) {
        if (sender < kSlots) presets_[sender] = value;
    }

    // The set entries of slots that are no longer present (`presentMask` has slotBit of each member), in slot order. They are emptied.
    std::vector<SlotPreset> gone(uint8_t presentMask) {
        std::vector<SlotPreset> released;
        for (uint8_t slot = 0; slot < kSlots; ++slot) {
            if (presets_[slot] == 0 || (presentMask & slotBit(slot)) != 0) continue;
            released.push_back({slot, presets_[slot]});
            presets_[slot] = 0;
        }
        return released;
    }

    // True when the default partner is not the one of the last call (kNoSlot before the first).
    bool defaultChanged(uint8_t newDefault) {
        const bool changed = newDefault != lastDefault_;
        lastDefault_ = newDefault;
        return changed;
    }

    uint8_t lastDefault() const { return lastDefault_; }

    // The set entries other than the default partner's, in slot order, for the cleanup to release one by one. Every entry is emptied
    // (the cleanup releases the default partner's own).
    std::vector<SlotPreset> cleanupPasses(uint8_t defaultSlot) {
        std::vector<SlotPreset> passes;
        for (uint8_t slot = 0; slot < kSlots; ++slot) {
            if (presets_[slot] != 0 && slot != defaultSlot) passes.push_back({slot, presets_[slot]});
            presets_[slot] = 0;
        }
        return passes;
    }

    void clear() {
        presets_.fill(0);
        lastDefault_ = kNoSlot;
    }

private:
    std::array<uint32_t, kSlots> presets_{};
    uint8_t lastDefault_ = kNoSlot;
};

}  // namespace anim_stream_rules
