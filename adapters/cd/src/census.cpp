#include "census.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "capture.h"
#include "funnels.h"
#include "log.h"
#include "troc_catalogue.h"

namespace {

constexpr uint64_t kPeriodMs = 60000;
constexpr size_t kTopOpcodes = 6;

uint64_t g_lastMs = 0;
uint64_t g_lastPackets = 0;

std::string busiest(packet::Direction direction) {
    std::vector<std::pair<uint32_t, uint16_t>> counts;
    for (const auto& [opcode, info] : troc_catalogue::table().all()) {
        if (const uint32_t n = funnels::opcodeCount(direction, opcode)) counts.emplace_back(n, opcode);
    }
    const size_t top = std::min(kTopOpcodes, counts.size());
    std::partial_sort(counts.begin(), counts.begin() + top, counts.end(), std::greater<>());
    std::string out;
    for (size_t i = 0; i < top; ++i) {
        out += (i ? ", " : "") + std::string(troc_catalogue::nameOf(counts[i].second)) + " " + std::to_string(counts[i].first);
    }
    return out;
}

}  // namespace

namespace census {

void tick(uint64_t nowMs) {
    if (nowMs - g_lastMs < kPeriodMs) return;
    g_lastMs = nowMs;
    const funnels::Totals t = funnels::totals();
    const uint64_t packets = t.packets[0] + t.packets[1];
    if (packets == g_lastPackets) return;
    g_lastPackets = packets;
    logger::write("census: c2s %llu (%llu B), s2c %llu (%llu B)", static_cast<unsigned long long>(t.packets[0]),
                  static_cast<unsigned long long>(t.bytes[0]), static_cast<unsigned long long>(t.packets[1]),
                  static_cast<unsigned long long>(t.bytes[1]));
    if (t.malformed || t.eventsDropped || t.faults || capture::dropped()) {
        logger::write("census: malformed %llu, events dropped %llu, observer faults %llu, capture dropped %llu",
                      static_cast<unsigned long long>(t.malformed), static_cast<unsigned long long>(t.eventsDropped),
                      static_cast<unsigned long long>(t.faults), static_cast<unsigned long long>(capture::dropped()));
    }
    logger::write("census: top c2s %s", busiest(packet::Direction::ClientToServer).c_str());
    logger::write("census: top s2c %s", busiest(packet::Direction::ServerToClient).c_str());
}

}  // namespace census
