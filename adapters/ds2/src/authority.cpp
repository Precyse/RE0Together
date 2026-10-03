#include "authority.h"

#include <algorithm>

namespace {

using authority::AuthMessage;
using authority::Outcome;
using authority::Record;

bool contains(const std::vector<uint8_t>& slots, uint8_t slot) {
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

AuthMessage makeMessage(uint64_t id, uint32_t epoch, uint8_t owner) { return {id, epoch, owner, {}}; }

}  // namespace

namespace authority {

std::optional<reject_counters::Reason> rejectReason(Outcome outcome) {
    using Reason = reject_counters::Reason;
    switch (outcome) {
        case Outcome::StaleEpoch: return Reason::StaleEpoch;
        case Outcome::UnknownObject: return Reason::UnknownObject;
        case Outcome::WrongSender: return Reason::WrongSender;
        case Outcome::InvalidOwner: return Reason::InvalidOwner;
        case Outcome::Malformed: return Reason::Malformed;
        case Outcome::Applied:
        case Outcome::Duplicate: break;
    }
    return std::nullopt;
}

void AuthorityTable::setSession(uint8_t localSlot, uint8_t hostSlot) {
    local_ = localSlot;
    host_ = hostSlot;
}

std::optional<AuthMessage> AuthorityTable::claim(uint64_t id) {
    return inSession() ? assign(id, local_) : std::nullopt;
}

std::optional<AuthMessage> AuthorityTable::assign(uint64_t id, uint8_t owner) {
    const bool host = local_ == host_;
    if (!inSession() || owner == kNoOwner || (owner != local_ && !host)) return std::nullopt;
    const auto found = records_.find(id);
    if (found != records_.end()) {
        const Record& record = found->second;
        if (record.owner == owner || (record.owner != kNoOwner && !host)) return std::nullopt;
    }
    const AuthMessage claim = makeMessage(id, found == records_.end() ? 1 : found->second.epoch + 1, owner);
    if (onMessage(authority_wire::kMsgAuthClaim, local_, claim) != Outcome::Applied) return std::nullopt;
    return claim;
}

std::optional<AuthMessage> AuthorityTable::release(uint64_t id) {
    const auto found = records_.find(id);
    if (!inSession() || found == records_.end() || found->second.owner != local_) return std::nullopt;
    const AuthMessage stop = makeMessage(id, found->second.epoch, local_);
    if (onMessage(authority_wire::kMsgAuthStop, local_, stop)!= Outcome::Applied) return std::nullopt;
    return stop;
}

std::optional<AuthMessage> AuthorityTable::stop(uint64_t id) {
    const auto found = records_.find(id);
    if (!inSession() || local_ != host_ || found == records_.end() || found->second.owner == kNoOwner) {
        return std::nullopt;
    }
    const AuthMessage stop = makeMessage(id, found->second.epoch, found->second.owner);
    if (onMessage(authority_wire::kMsgAuthStop, local_, stop)!= Outcome::Applied) return std::nullopt;
    return stop;
}

std::optional<AuthMessage> AuthorityTable::decline(uint64_t id) {
    const auto found = records_.find(id);
    if (!inSession() || found == records_.end() || found->second.owner != local_) return std::nullopt;
    const AuthMessage decline = makeMessage(id, found->second.epoch, local_);
    if (onMessage(authority_wire::kMsgAuthDecline, local_, decline)!= Outcome::Applied) {
        return std::nullopt;
    }
    return decline;
}

Outcome AuthorityTable::onMessage(uint16_t type, uint8_t sender, const AuthMessage& message) {
    switch (type) {
        case authority_wire::kMsgAuthClaim: return applyClaim(sender, message);
        case authority_wire::kMsgAuthDecline: return applyDecline(sender, message);
        case authority_wire::kMsgAuthStop: return applyStop(sender, message);
        default: return Outcome::Malformed;
    }
}

Outcome AuthorityTable::applyClaim(uint8_t sender, const AuthMessage& message) {
    if (message.owner != sender && sender != host_) return Outcome::WrongSender;
    const auto found = records_.find(message.id);
    if (found == records_.end()) {
        Record& fresh = records_[message.id];
        change(fresh, {message.id, message.owner, message.epoch, {}});
        return Outcome::Applied;
    }
    Record& record = found->second;
    if (contains(record.barred, message.owner)) return Outcome::InvalidOwner;
    Record next = record;
    next.owner = message.owner;
    next.epoch = message.epoch;
    if (message.epoch > record.epoch) {
        change(record, next);
        return Outcome::Applied;
    }
    if (message.epoch < record.epoch) return Outcome::StaleEpoch;
    if (record.owner == message.owner) return Outcome::Duplicate;
    if (record.owner == kNoOwner || message.owner > record.owner) return Outcome::StaleEpoch;
    change(record, next);  // two claims of one epoch: the lower slot wins everywhere
    return Outcome::Applied;
}

Outcome AuthorityTable::applyDecline(uint8_t sender, const AuthMessage& message) {
    const auto found = records_.find(message.id);
    if (found == records_.end()) return Outcome::UnknownObject;
    Record& record = found->second;
    if (message.epoch != record.epoch) return Outcome::StaleEpoch;
    if (sender != message.owner || record.owner != message.owner) return Outcome::WrongSender;
    Record next = record;
    next.owner = kNoOwner;
    next.epoch = record.epoch + 1;
    if (!contains(next.barred, message.owner)) next.barred.push_back(message.owner);
    change(record, next);
    return Outcome::Applied;
}

Outcome AuthorityTable::applyStop(uint8_t sender, const AuthMessage& message) {
    const auto found = records_.find(message.id);
    if (found == records_.end()) return Outcome::UnknownObject;
    Record& record = found->second;
    if (message.epoch != record.epoch) return Outcome::StaleEpoch;
    if (record.owner != message.owner || (sender != record.owner && sender != host_)) {
        return Outcome::WrongSender;
    }
    Record next = record;
    next.owner = kNoOwner;
    next.epoch = record.epoch + 1;
    if (sender == record.owner) next.barred.clear();  // a voluntary release gives everyone a fresh start
    change(record, next);
    return Outcome::Applied;
}

void AuthorityTable::change(Record& record, const Record& next) {
    const Record before = record;
    record = next;
    if (onChange_) onChange_(before, record);
}

void AuthorityTable::dropSlot(uint8_t slot) {
    for (auto& [id, record] : records_) {
        Record next = record;
        std::erase(next.barred, slot);
        if (record.owner == slot) {
            next.owner = kNoOwner;
            next.epoch = record.epoch + 1;
        }
        if (next.owner != record.owner || next.barred != record.barred) change(record, next);
    }
}

bool AuthorityTable::isOwner(uint64_t id) const { return inSession() && ownerOf(id) == local_; }

uint8_t AuthorityTable::ownerOf(uint64_t id) const {
    const auto found = records_.find(id);
    return found == records_.end() ? kNoOwner : found->second.owner;
}

uint32_t AuthorityTable::epochOf(uint64_t id) const {
    const auto found = records_.find(id);
    return found == records_.end() ? 0 : found->second.epoch;
}

uint8_t AuthorityTable::pickOwner(uint64_t id, std::span<const uint8_t> candidates) const {
    const auto found = records_.find(id);
    for (const uint8_t slot : candidates) {
        if (slot != kNoOwner && (found == records_.end() || !contains(found->second.barred, slot))) return slot;
    }
    return kNoOwner;
}

std::vector<AuthMessage> AuthorityTable::claimsOf(uint8_t slot) const {
    std::vector<AuthMessage> claims;
    for (const auto& [id, record] : records_) {
        if (record.owner == slot) claims.push_back(makeMessage(id, record.epoch, slot));
    }
    return claims;
}

}  // namespace authority
