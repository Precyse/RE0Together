#include "anim_sync.h"

#include <chrono>
#include <cstring>
#include <vector>

#include "ds2/remote_animation.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kSendInterval = std::chrono::milliseconds(33);
constexpr auto kSnapshotInterval = std::chrono::seconds(1);
constexpr size_t kEntryHeaderSize = 3;  // u16 index, u8 type

Clock::time_point g_lastSend;
Clock::time_point g_lastSnapshot;
uint32_t g_seq = 0;
constexpr auto kStatsInterval = std::chrono::seconds(5);
Clock::time_point g_lastStats;
uint64_t g_bytesSent = 0;
uint32_t g_reportsSent = 0;

void countSent(Clock::time_point now, size_t bytes) {
    g_bytesSent += bytes;
    ++g_reportsSent;
    if (now - g_lastStats < kStatsInterval) return;
    const double seconds = std::chrono::duration<double>(now - g_lastStats).count();
    logger::write("anim_sync: sent %u reports, %.0f bytes/s in the last %.0f s", g_reportsSent, g_bytesSent / seconds, seconds);
    g_lastStats = now;
    g_bytesSent = 0;
    g_reportsSent = 0;
}

size_t valueSize(uint8_t type) { return remote_animation::valueBytes(type); }

std::vector<uint8_t> encode(const std::vector<remote_animation::Change>& changes, bool snapshot) {
    std::vector<uint8_t> out(sizeof(anim_sync::AnimHeader));
    for (const remote_animation::Change& change : changes) {
        const size_t size = valueSize(change.type);
        out.resize(out.size() + kEntryHeaderSize + size);
        uint8_t* at = out.data() + out.size() - kEntryHeaderSize - size;
        std::memcpy(at, &change.index, sizeof(change.index));
        at[sizeof(change.index)] = change.type;
        std::memcpy(at + kEntryHeaderSize, change.value, size);
    }
    const anim_sync::AnimHeader header{++g_seq, static_cast<uint16_t>(changes.size()),
                                       static_cast<uint16_t>(snapshot ? anim_sync::kFlagSnapshot : 0)};
    std::memcpy(out.data(), &header, sizeof(header));
    return out;
}

}  // namespace

namespace anim_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != kMsgAnimState || frame.payload.size() < sizeof(AnimHeader)) return;
    AnimHeader header;
    std::memcpy(&header, frame.payload.data(), sizeof(header));
    size_t at = sizeof(header);
    for (uint16_t i = 0; i < header.count; ++i) {
        if (at + kEntryHeaderSize > frame.payload.size()) return;
        remote_animation::Change change{};
        std::memcpy(&change.index, frame.payload.data() + at, sizeof(change.index));
        change.type = frame.payload[at + sizeof(change.index)];
        const size_t size = valueSize(change.type);
        if (size == 0 || at + kEntryHeaderSize + size > frame.payload.size()) return;
        std::memcpy(change.value, frame.payload.data() + at + kEntryHeaderSize, size);
        remote_animation::setPeerChange(frame.slot, change);
        at += kEntryHeaderSize + size;
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    remote_animation::setCollecting(session.linked);
    if (!session.linked || now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    const bool snapshot = now - g_lastSnapshot >= kSnapshotInterval;
    if (snapshot) {
        g_lastSnapshot = now;
        remote_animation::requestSnapshot();
    }
    const std::vector<remote_animation::Change> changes = remote_animation::takeLocalChanges();
    if (changes.empty()) return;
    const std::vector<uint8_t> payload = encode(changes, snapshot);
    if (net.send(kMsgAnimState, false, proto::kSlotAll, payload)) countSent(now, payload.size());
}

}  // namespace anim_sync
