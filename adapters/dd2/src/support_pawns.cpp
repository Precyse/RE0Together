#include "support_pawns.h"

#include <array>
#include <optional>
#include <vector>

#include "calls.h"
#include "local_body.h"
#include "log.h"
#include "puppet_registry.h"
#include "refw.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr const char* kPawnManager = "app.PawnManager";
constexpr const char* kPawnIdEnum = "app.PawnManager.PawnID";
constexpr std::array<const char*, 2> kSubSeats{"SubPawn01", "SubPawn02"};
constexpr bool kLiveOnly = false;  // getPartyPawn's Boolean: a seated pawn counts even when down
constexpr uint64_t kScanPeriodMs = 1000;
constexpr uint64_t kSettleMs = 20000;  // the host's world registers its party pawns some seconds after the load

struct Seat {
    void* character = nullptr;
    uint32_t id = 0;
    bool support = false;  // a hired pawn of the world: not a puppet and not a player's main pawn
};

std::optional<std::array<int64_t, kSubSeats.size()>> g_seatIds;
bool g_seatIdsTried = false;
std::optional<uint64_t> g_firstLoadStartMs;
bool g_firstLoadDone = false;
uint64_t g_lastScanMs = 0;

bool seatIds() {
    if (!g_seatIdsTried) {
        g_seatIdsTried = true;
        std::array<int64_t, kSubSeats.size()> ids{};
        bool all = true;
        for (size_t i = 0; i < kSubSeats.size(); ++i) {
            const auto literal = refw::enumLiteral(kPawnIdEnum, kSubSeats[i]);
            all = all && literal;
            if (literal) ids[i] = *literal;
        }
        if (all) g_seatIds = ids;
        else logger::write("support_pawns: PawnID literals missing, support pawns are left alone");
    }
    return g_seatIds.has_value();
}

// The filled sub pawn seats.
std::vector<Seat> seats() {
    std::vector<Seat> out;
    void* pawns = refw::managedSingleton(kPawnManager);
    if (!pawns || !calls::enabled(calls::Feature::Puppet) || !seatIds()) return out;
    void* ownMainPawn = local_body::character(wire_dd2::kKindMainPawn);
    for (int64_t id : *g_seatIds) {
        void* pawn = calls::objectResult(calls::kGetPartyPawn, pawns,
                                         {reinterpret_cast<void*>(static_cast<intptr_t>(id)), reinterpret_cast<void*>(uintptr_t{kLiveOnly})});
        void* character = pawn ? calls::objectResult(calls::kGetCachedCharacter, pawn) : nullptr;
        const refw::InvokeResult* charaId = character ? refw::invoke(calls::method(calls::kGetCharaID), character) : nullptr;
        if (!charaId) continue;
        Seat seat{character, charaId->dword, false};
        seat.support = !puppet_registry::isPuppetObject(character) && !puppet_registry::isPuppetId(seat.id) && character != ownMainPawn;
        out.push_back(seat);
    }
    return out;
}

bool dismiss(const Seat& seat, const char* why) {
    void* pawns = refw::managedSingleton(kPawnManager);
    const bool ok = pawns && refw::invoke(calls::method(calls::kDismissPawn), pawns, {reinterpret_cast<void*>(uintptr_t{seat.id})});
    logger::write("support_pawns: support pawn 0x%08X dismissed (%s)%s", seat.id, why, ok ? "" : ", refused");
    return ok;
}

void dismissAll(const char* why) {
    for (const Seat& seat : seats()) {
        if (seat.support) dismiss(seat, why);
    }
}

bool guestInWorld() { return session::active() && !session::isHost() && world_gate::place() == world_gate_rule::Place::InGame; }

}  // namespace

namespace support_pawns {

void tick(uint64_t nowMs) {
    if (g_firstLoadDone || !guestInWorld() || nowMs - g_lastScanMs < kScanPeriodMs) return;
    g_lastScanMs = nowMs;
    if (!g_firstLoadStartMs) g_firstLoadStartMs = nowMs;
    dismissAll("the host's world, first co-op load");
    g_firstLoadDone = nowMs - *g_firstLoadStartMs >= kSettleMs;
}

void makeRoom() {
    const std::vector<Seat> taken = seats();
    if (taken.size() < kSubSeats.size()) return;
    for (const Seat& seat : taken) {
        if (seat.support) {
            dismiss(seat, "a seat for a peer's main pawn");
            return;
        }
    }
}

}  // namespace support_pawns
