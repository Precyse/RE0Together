#include "flag_sync.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "flag_diff.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"

namespace {

using flag_diff::Change;
using flag_diff::Words;

constexpr uint32_t kCheckEveryFrames = 6;
constexpr size_t kHeaderSize = 4;
constexpr size_t kMaxChanges = flag_diff::kWords;

NetClient* g_net = nullptr;
Words g_known{};       // game thread: the flags as both machines last agreed on them
bool g_hasKnown = false;
uint32_t g_frame = 0;

std::mutex g_mutex;
std::vector<Change> g_incoming;  // guarded by g_mutex

uintptr_t flagsAddress() {
    const uintptr_t manager = game::readPointer(game::kFlagManagerGlobal);
    return manager ? manager + game::kFlagBitsOffset : 0;
}

void send(const std::vector<Change>& changes) {
    std::vector<uint8_t> payload(kHeaderSize + changes.size() * sizeof(Change));
    const auto count = static_cast<uint16_t>(changes.size());
    std::memcpy(payload.data(), &count, sizeof(count));
    std::memcpy(payload.data() + kHeaderSize, changes.data(), changes.size() * sizeof(Change));
    if (g_net->send(proto::kMsgFlagDiff, true, proto::kSlotAll, payload)) {
        debug_stats::count(debug_stats::Counter::FlagWordsSent, static_cast<uint32_t>(changes.size()));
    }
}

void applyIncoming(uintptr_t address, Words& words) {
    std::vector<Change> incoming;
    {
        std::lock_guard lock(g_mutex);
        incoming.swap(g_incoming);
    }
    if (incoming.empty()) return;
    for (const Change& change : incoming) {
        flag_diff::apply(words, change);
        flag_diff::apply(g_known, change);
    }
    if (game::writeMemory(address, words)) {
        debug_stats::count(debug_stats::Counter::FlagWordsApplied, static_cast<uint32_t>(incoming.size()));
    }
}

void onTick() {
    if (!net_pad::active()) {
        g_hasKnown = false;
        return;
    }
    if (++g_frame % kCheckEveryFrames != 0) return;
    const uintptr_t address = flagsAddress();
    Words words;
    if (!address || !game::readMemory(address, words)) return;
    if (!g_hasKnown) {
        g_known = words;
        g_hasKnown = true;
        logger::write("flag_sync: tracking %zu flag words", flag_diff::kWords);
    }
    const std::vector<Change> local = flag_diff::diff(g_known, words);
    if (!local.empty()) {
        send(local);
        g_known = words;
    }
    applyIncoming(address, words);
}

}  // namespace

namespace flag_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgFlagDiff || frame.payload.size() < kHeaderSize) return;
    uint16_t count = 0;
    std::memcpy(&count, frame.payload.data(), sizeof(count));
    if (count > kMaxChanges || frame.payload.size() != kHeaderSize + count * sizeof(Change)) return;
    std::vector<Change> changes(count);
    std::memcpy(changes.data(), frame.payload.data() + kHeaderSize, count * sizeof(Change));
    std::lock_guard lock(g_mutex);
    g_incoming.insert(g_incoming.end(), changes.begin(), changes.end());
}

bool read(Words& out) {
    const uintptr_t address = flagsAddress();
    return address && game::readMemory(address, out);
}

void applySnapshot(const Words& words) {
    const uintptr_t address = flagsAddress();
    if (!address || !game::writeMemory(address, words)) return;
    g_known = words;
    g_hasKnown = true;
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("flag_sync", onTick);
}

}  // namespace flag_sync
