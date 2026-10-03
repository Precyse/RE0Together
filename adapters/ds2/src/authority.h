#pragma once
// Authority records: for each shared object (vehicle, enemy, loose cargo) which slot simulates it, and the object's
// generation (epoch). Every machine keeps the same table and applies the same rules to the same messages, so the
// tables converge without a round trip; the host only has the extra right to assign or stop an owner. Pure logic, no
// game and no network (authority_sync does the sending).
//
// Rules, which make a stale or duplicated message harmless:
//   - a CLAIM with a newer epoch replaces the record; the same epoch from the same owner is a duplicate; two owners
//     claiming the same epoch at once are settled by the lower slot (every machine picks the same one);
//   - STOP and DECLINE must carry the record's current epoch, and after either the record is unowned at epoch + 1;
//   - a DECLINE bars its sender from owning the object until the owner releases it voluntarily, so ownership cannot
//     bounce between machines that cannot host it (`pickOwner` skips the barred);
//   - a slot that leaves loses every record it owned.
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "authority_wire.h"
#include "reject_counters.h"

namespace authority {

using authority_wire::AuthMessage;
using authority_wire::kNoOwner;

struct Record {
    uint64_t id = 0;
    uint8_t owner = kNoOwner;
    uint32_t epoch = 0;
    std::vector<uint8_t> barred;  // slots that declined and may not own it again until a voluntary release
};

// What a message did to the table. Everything but Applied and Duplicate is a refusal, counted by `rejectReason`.
enum class Outcome { Applied, Duplicate, StaleEpoch, UnknownObject, WrongSender, InvalidOwner, Malformed };

// The reject counter a refused message goes to; nothing for Applied and Duplicate.
std::optional<reject_counters::Reason> rejectReason(Outcome outcome);

class AuthorityTable {
public:
    using ChangeHandler = std::function<void(const Record& before, const Record& after)>;

    // The local slot and the host's. Until set, nothing can be claimed.
    void setSession(uint8_t localSlot, uint8_t hostSlot);

    // A new session: every record is forgotten (the change handler is not called).
    void clear() { records_.clear(); }

    // Called after every change to a record (`before.epoch` is 0 for a new one), including ones the local machine
    // makes. Use it to start or stop simulating the object.
    void setChangeHandler(ChangeHandler handler) { onChange_ = std::move(handler); }

    // Local actions. Each returns the message to send to everyone, or nothing when the rules refuse it (the object is
    // owned by someone else, the slot is barred, there is nothing to release).
    std::optional<AuthMessage> claim(uint64_t id);                 // the local slot takes the object
    std::optional<AuthMessage> assign(uint64_t id, uint8_t owner);  // host: `owner` takes the object
    std::optional<AuthMessage> release(uint64_t id);               // the local owner gives it up
    std::optional<AuthMessage> stop(uint64_t id);                  // host: the current owner must stop
    std::optional<AuthMessage> decline(uint64_t id);               // the local slot cannot host an object it was given

    // A message from `sender`, the slot the transport says it came from.
    Outcome onMessage(uint16_t type, uint8_t sender, const AuthMessage& message);

    // The slot left: every record it owned becomes unowned (one epoch up) and it is no longer barred anywhere.
    void dropSlot(uint8_t slot);

    bool isOwner(uint64_t id) const;
    uint8_t ownerOf(uint64_t id) const;  // kNoOwner when unowned or unknown
    uint32_t epochOf(uint64_t id) const;  // 0 when unknown

    // The first of `candidates` that has not declined `id` (host, choosing whom to assign), or kNoOwner.
    uint8_t pickOwner(uint64_t id, std::span<const uint8_t> candidates) const;

    // The CLAIM messages of every record `slot` owns, to repeat to a machine that just joined or asked for a resync.
    std::vector<AuthMessage> claimsOf(uint8_t slot) const;

private:
    Outcome applyClaim(uint8_t sender, const AuthMessage& message);
    Outcome applyDecline(uint8_t sender, const AuthMessage& message);
    Outcome applyStop(uint8_t sender, const AuthMessage& message);
    void change(Record& record, const Record& next);
    bool inSession() const { return local_ != kNoOwner; }

    uint8_t local_ = kNoOwner;
    uint8_t host_ = kNoOwner;
    std::unordered_map<uint64_t, Record> records_;
    ChangeHandler onChange_;
};

}  // namespace authority
