#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "net_client.h"

// Shared status registry for the debug overlay. Modules write from any thread; the overlay only reads.
namespace debug_stats {

// Event counters: totals plus a per-second rate.
enum class Counter : size_t {
    PadSent,
    PadReceived,
    PlayerStateSent,
    PlayerStateReceived,
    Snaps,
    HitRequestSent,
    HitRequestReceived,
    HitAppliedSent,
    HitAppliedReceived,
    EnemyStateSent,
    EnemyStateReceived,
    EnemyMismatches,
    DeathsReported,
    DeathsApplied,
    PadUnderruns,
    PadSkips,
    SaveReads,
    SaveWrites,
    RoomDesyncs,
    DoorsSent,
    DoorsApplied,
    DoorsSuppressed,
    MenuFreezes,
    InventorySent,
    InventoryApplied,
    InventoryExchanges,
    FloorPutSent,
    FloorPutApplied,
    FloorTakeSent,
    FloorTakeApplied,
    FloorPendingApplied,
    FloorPendingDropped,
    FloorTakeMisses,
    CommandsSent,
    PickupsAborted,
    Blends,
    Count
};

// Latest-value gauges.
enum class Gauge : size_t { PadDepth, BillyOwner, RebeccaOwner, SaveRedirect, WorldFrozen, FloorPending, PartyMode, FocusedCharacter, DoorPhase, Room, Count };

constexpr size_t kCounterCount = static_cast<size_t>(Counter::Count);
constexpr size_t kGaugeCount = static_cast<size_t>(Gauge::Count);
constexpr int kUnknownOwner = -1;

// A short text with the time it was recorded.
struct Note {
    std::string text;  // empty when none
    uint64_t ageMs = 0;
};

struct Snapshot {
    bool linked = false;
    uint8_t localSlot = 0;
    uint8_t hostSlot = 0;
    uint32_t epoch = 0;
    std::vector<PeerInfo> peers;
    std::array<uint32_t, kCounterCount> total{};
    std::array<uint32_t, kCounterCount> perSecond{};
    std::array<int, kGaugeCount> gauge{};
    std::vector<std::string> disabledCallbacks;
    std::string lastError;  // empty when none
    uint64_t lastErrorAgeMs = 0;
    Note lastCommand;   // the last local command key press
    Note lastDecision;  // what the party commands last did with a request
};

void count(Counter counter, uint32_t amount = 1);
void set(Gauge gauge, int value);
void setSession(const SessionSnapshot& session);
void noteCallbackDisabled(const char* name);

// printf-style; also the text shown by the overlay.
void setError(const char* format, ...);

void setLastCommand(const char* text);
void setLastDecision(const char* text);

// Rates are recomputed over windows of about one second.
Snapshot snapshot();

}  // namespace debug_stats
