#include "reject_counters.h"

#include <array>
#include <cstdio>
#include <map>
#include <mutex>

namespace {

constexpr size_t kReasons = static_cast<size_t>(reject_counters::Reason::Count);
using Totals = std::array<uint64_t, kReasons>;

std::mutex g_mutex;
std::map<uint16_t, Totals> g_totals;
bool g_changed = false;
TimeUs g_lastSummary = 0;

}  // namespace

namespace reject_counters {

const char* name(Reason reason) {
    switch (reason) {
        case Reason::StaleEpoch: return "stale_epoch";
        case Reason::UnknownObject: return "unknown_object";
        case Reason::WrongSender: return "wrong_sender";
        case Reason::InvalidOwner: return "invalid_owner";
        case Reason::Malformed: return "malformed";
        case Reason::Expired: return "expired";
        case Reason::Overflow: return "overflow";
        case Reason::RateLimited: return "rate_limited";
        case Reason::Count: break;
    }
    return "?";
}

void count(uint16_t messageType, Reason reason, uint32_t amount) {
    if (amount == 0) return;
    std::lock_guard lock(g_mutex);
    g_totals[messageType][static_cast<size_t>(reason)] += amount;
    g_changed = true;
}

uint64_t total(uint16_t messageType, Reason reason) {
    std::lock_guard lock(g_mutex);
    const auto found = g_totals.find(messageType);
    return found == g_totals.end() ? 0 : found->second[static_cast<size_t>(reason)];
}

std::string summaryIfDue(TimeUs now) {
    std::lock_guard lock(g_mutex);
    if (!g_changed || now - g_lastSummary < kLogIntervalUs) return {};
    g_changed = false;
    g_lastSummary = now;
    std::string line = "rejects:";
    char piece[64];
    for (const auto& [type, totals] : g_totals) {
        for (size_t i = 0; i < kReasons; ++i) {
            if (totals[i] == 0) continue;
            std::snprintf(piece, sizeof(piece), " 0x%04X %s=%llu", type, name(static_cast<Reason>(i)),
                          static_cast<unsigned long long>(totals[i]));
            line += piece;
        }
    }
    return line;
}

void reset() {
    std::lock_guard lock(g_mutex);
    g_totals.clear();
    g_changed = false;
    g_lastSummary = 0;
}

}  // namespace reject_counters
