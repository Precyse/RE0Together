#include "floor_pending.h"

namespace {

using floor_pending::Event;

float distanceSquared(const floor_pending::Vec3& a, const floor_pending::Vec3& b) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) sum += (a[i] - b[i]) * (a[i] - b[i]);
    return sum;
}

// Index of the pending put the take picks up (nearest of the same id within kMatchDistance), or events.size().
size_t findPut(const std::vector<Event>& events, const Event& take) {
    size_t best = events.size();
    float bestDistance = floor_pending::kMatchDistance * floor_pending::kMatchDistance;
    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i].isTake || events[i].itemId != take.itemId) continue;
        const float distance = distanceSquared(events[i].pos, take.pos);
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

}  // namespace

namespace floor_pending {

AddResult Queue::add(uint16_t room, const Event& event) {
    std::vector<Event>& events = m_rooms[room];
    if (event.isTake) {
        const size_t put = findPut(events, event);
        if (put != events.size()) {
            events.erase(events.begin() + static_cast<std::ptrdiff_t>(put));
            return {true, false};
        }
    }
    const bool full = events.size() >= kMaxPendingPerRoom;
    if (full) events.erase(events.begin());
    events.push_back(event);
    return {false, full};
}

std::vector<Event> Queue::take(uint16_t room) {
    const auto found = m_rooms.find(room);
    if (found == m_rooms.end()) return {};
    std::vector<Event> events = std::move(found->second);
    m_rooms.erase(found);
    return events;
}

size_t Queue::total() const {
    size_t sum = 0;
    for (const auto& [room, events] : m_rooms) sum += events.size();
    return sum;
}

}  // namespace floor_pending
