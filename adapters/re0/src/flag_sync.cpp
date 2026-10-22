#include "flag_sync.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "event_rule.h"
#include "flag_diff.h"
#include "flag_family.h"
#include "flag_load_hold_rule.h"
#include "flag_sequencer.h"
#include "game.h"
#include "game_tick.h"
#include "join_sync.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "scene.h"
#include "state_mode.h"

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
flag_load_hold_rule::LoadHold g_loadHold;  // game thread, state mode: a local load is rewriting the flags

std::mutex g_mutex;
std::vector<Change> g_incoming;  // guarded by g_mutex

uintptr_t managerObject() { return game::readPointer(game::kFlagManagerGlobal); }

uintptr_t flagsAddress() {
    const uintptr_t manager = managerObject();
    return manager ? manager + game::kFlagBitsOffset : 0;
}

void send(const std::vector<Change>& changes) {
    if (state_mode::on()) {
        flag_sequencer::submitLocal(changes);
        return;
    }
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

// State mode: the changes the host's order says to apply wait in the same queue a peer's FLAG_DIFF would use.
void queueOrdered(const std::vector<Change>& ordered) {
    std::lock_guard lock(g_mutex);
    g_incoming.insert(g_incoming.end(), ordered.begin(), ordered.end());
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
        g_loadHold.cancel();
        if (state_mode::on()) flag_sequencer::reset();
        return;
    }
    if (++g_frame % kCheckEveryFrames != 0) return;
    if (g_loadHold.holding(scene::loadSerial(), g_frame)) return;
    const uintptr_t manager = managerObject();
    const uintptr_t address = manager ? manager + game::kFlagBitsOffset : 0;
    Words words;
    if (!address || !game::readMemory(address, words)) return;
    if (!g_hasKnown) {
        g_known = words;
        g_hasKnown = true;
        logger::write("flag_sync: tracking %zu flag words", flag_diff::kWords);
    }
    // A guest that has not joined the host's world yet keeps its flags to itself: they come from an older save.
    const bool mayPropose = !state_mode::on() || join_sync::caughtUp();
    std::vector<Change> local = mayPropose ? flag_diff::diff(g_known, words) : std::vector<Change>{};
    for (const uint32_t bit : event_rule::kPresenceFlags) flag_diff::dropBit(local, bit);  // each machine's own
    if (!local.empty()) {
        logChanges("sent", local);
        send(local);
        g_known = words;
    }
    if (state_mode::on()) queueOrdered(flag_sequencer::collect(words));
    applyIncoming(manager, words);
}

}  // namespace

namespace flag_sync {

void onFrame(const GameFrame& frame) {
    if (state_mode::on()) return;  // FLAG_DIFF is replaced by FLAG_PROPOSE / FLAG_COMMIT (flag_sequencer)
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

void onLocalLoad() {
    if (!state_mode::on()) return;
    g_hasKnown = false;
    g_loadHold.begin(scene::loadSerial(), g_frame);
    flag_sequencer::onLocalLoad();
}

void applySnapshot(const Words& words) {
    const uintptr_t address = flagsAddress();
    if (!address || !game::writeMemory(address, words)) return;
    g_known = words;
    g_hasKnown = true;
    g_loadHold.cancel();
    if (state_mode::on()) flag_sequencer::reset();
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("flag_sync", onTick);
}

}  // namespace flag_sync
