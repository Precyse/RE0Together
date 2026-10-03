#pragma once
// Per message type, how many messages were refused and why. A message the adapter drops (an old epoch, an object it
// does not have, a sender that does not own the object, a payload that does not parse, a held message that expired)
// is counted here instead of vanishing, and the totals go to the log every few seconds while they change.
#include <cstdint>
#include <string>

#include "time_us.h"

namespace reject_counters {

enum class Reason : uint8_t {
    StaleEpoch,     // addressed to an older generation of the object (or one this machine has not heard of yet)
    UnknownObject,  // no record of the object
    WrongSender,    // the sender is not allowed to say this about the object
    InvalidOwner,   // the named owner declined or is otherwise barred from owning it
    Malformed,      // the payload does not parse
    Expired,        // held for an object that never appeared
    Overflow,       // a bounded queue was full and the message was dropped
    Count
};

const char* name(Reason reason);

// Any thread.
void count(uint16_t messageType, Reason reason, uint32_t amount = 1);
uint64_t total(uint16_t messageType, Reason reason);

// One line listing every non-zero total, or empty when nothing changed since the last non-empty answer or the last one
// was less than `kLogInterval` ago. Net thread.
constexpr TimeUs kLogIntervalUs = 5'000'000;
std::string summaryIfDue(TimeUs now);

void reset();

}  // namespace reject_counters
