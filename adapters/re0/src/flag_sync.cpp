#include "flag_sync.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "flag_diff.h"
#include "flag_family.h"
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

uintptr_t managerObject() { return game::readPointer(game::kFlagManagerGlobal); }

uintptr_t flagsAddress() {
    const uintptr_t manager = managerObject();
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

void logChanges(const char* direction, const std::vector<Change>& changes) {
    for (const Change& change : changes) {
        for (uint32_t bit = 0; bit < 32; ++bit) {
            const uint32_t index = change.word * 32u + bit;
            const uint32_t mask = 1u << bit;
            const char* family = flag_family::name(flag_family::classify(index));
            if (change.set & mask) logger::write("flag_sync: %s %s flag 0x%x set", direction, family, index);
            if (change.clear & mask) logger::write("flag_sync: %s %s flag 0x%x clear", direction, family, index);
        }
    }
}

void writeFlagWord(uintptr_t manager, uint16_t word, uint32_t set, uint32_t clear) {
    const uintptr_t address = manager + game::kFlagBitsOffset + word * sizeof(uint32_t);
    uint32_t value = 0;
    if (game::readMemory(address, value)) game::writeMemory(address, (value | set) & ~clear);
}

// Story, effect, act, display and BGM bits go through the game's setter so the peer runs the same side effects
// as the local player. Enemy bits stay on the raw mirror: the enemy sync already spawns them, a setter call would spawn twice.
void applyIncoming(uintptr_t manager, Words& words) {
    std::vector<Change> incoming;
    {
        std::lock_guard lock(g_mutex);
        incoming.swap(g_incoming);
    }
    if (incoming.empty()) return;
    logChanges("received", incoming);
    bool bgmChanged = false;
    for (const Change& change : incoming) {
        uint32_t rawSet = 0;
        uint32_t rawClear = 0;
        for (uint32_t bit = 0; bit < 32; ++bit) {
            const uint32_t mask = 1u << bit;
            const bool wantSet = change.set & mask;
            if (!wantSet && !(change.clear & mask)) continue;
            if (static_cast<bool>(words[change.word] & mask) == wantSet) continue;
            const uint32_t index = change.word * 32u + bit;
            const flag_family::Family family = flag_family::classify(index);
            if (family == flag_family::Family::Enemy) {
                (wantSet ? rawSet : rawClear) |= mask;
                continue;
            }
            game::callThiscall<void>(game::kFlagSetFunction, reinterpret_cast<void*>(manager), index,
                                     static_cast<uint32_t>(wantSet));
            if (family == flag_family::Family::Bgm) bgmChanged = true;
        }
        if (rawSet | rawClear) writeFlagWord(manager, change.word, rawSet, rawClear);
        flag_diff::apply(words, change);
        flag_diff::apply(g_known, change);
    }
    const uintptr_t bgmObject = game::readPointer(game::kBgmRefreshObjectGlobal);
    if (bgmChanged && bgmObject) game::callThiscall<void>(game::kBgmRefreshFunction, reinterpret_cast<void*>(bgmObject));
    debug_stats::count(debug_stats::Counter::FlagWordsApplied, static_cast<uint32_t>(incoming.size()));
}

void onTick() {
    if (!net_pad::active()) {
        g_hasKnown = false;
        return;
    }
    if (++g_frame % kCheckEveryFrames != 0) return;
    const uintptr_t manager = managerObject();
    const uintptr_t address = manager ? manager + game::kFlagBitsOffset : 0;
    Words words;
    if (!address || !game::readMemory(address, words)) return;
    if (!g_hasKnown) {
        g_known = words;
        g_hasKnown = true;
        logger::write("flag_sync: tracking %zu flag words", flag_diff::kWords);
    }
    const std::vector<Change> local = flag_diff::diff(g_known, words);
    if (!local.empty()) {
        logChanges("sent", local);
        send(local);
        g_known = words;
    }
    applyIncoming(manager, words);
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
    for (const Change& change : changes) {
        if (change.word >= flag_diff::kWords) return;
    }
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
