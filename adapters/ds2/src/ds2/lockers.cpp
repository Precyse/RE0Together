// DEATH STRANDING 2: the shared locker, engine side (see ds2/lockers.h and docs/DS2_NOTES.md, "Shared locker"). A locker is
// the slots of kind 28 or 29 of a construction object's baggage owner, named by the owner's identifier. The host polls them,
// the guest follows the host's lists and reports what its own game moved.
#include "ds2/lockers.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include "cargo_diff.h"
#include "cargo_transfer.h"
#include "ds2/sim_tick.h"
#include "locker_table.h"
#include "log.h"
#include "remote_apply.h"
#include "toast_queue.h"

namespace {

using cargo_diff::PieceKey;
using locker_wire::LockerMove;
using locker_wire::Reason;
using lockers::Locker;

constexpr ULONGLONG kHostPollMs = 250;     // pending moves and their verification
constexpr ULONGLONG kHostReadMs = 1000;    // the pass over every locker
constexpr ULONGLONG kApplySettleMs = 300;  // the game serves a create or delete request on its next update
constexpr ULONGLONG kApplyGiveUpMs = 2000;
constexpr ULONGLONG kApplyRetryMs = 500;
constexpr uint8_t kMaxApplyTries = 3;      // an order piece's stale copies are removed first, then the create is asked again
constexpr ULONGLONG kGuestPollMs = 500;
constexpr ULONGLONG kFollowSettleMs = 2000;
constexpr int kMaxChangesPerRound = 6;
constexpr uint8_t kMaxUndoTries = 3;
constexpr float kToastSeconds = 4.0f;
constexpr size_t kLoggedIdentifiers = 5;
constexpr const char* kRefusedText = "The host's locker refused that";

using Contents = std::map<uint64_t, std::vector<game::Cargo>>;  // by idOf(key, kind), pieces in canonical order

uint64_t idOf(uint32_t key, uint8_t kind) { return (uint64_t{key} << 8) | kind; }
uint32_t keyOfId(uint64_t id) { return static_cast<uint32_t>(id >> 8); }
uint8_t kindOfId(uint64_t id) { return static_cast<uint8_t>(id & 0xFF); }

std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};

std::mutex g_mutex;
// Host (g_mutex): the last read, what changed since the net thread last asked, and the moves and answers in flight.
Contents g_known;
bool g_read = false;
std::vector<Locker> g_changed;
struct HostMove {
    uint8_t slot;
    LockerMove move;
    uint8_t tries;
    ULONGLONG notBefore;
};
std::vector<HostMove> g_moves;
std::vector<lockers::Reply> g_replies;
// Guest (g_mutex).
Contents g_wanted;
bool g_endPending = false;
locker_table::PendingMoves g_pending;
std::vector<LockerMove> g_outgoing;
struct Undo {
    LockerMove move;
    uint8_t tries;
};
std::vector<Undo> g_undo;
std::set<std::pair<uint64_t, PieceKey>> g_refused;  // pieces the game refused to create in a locker: not asked again until its list changes

// Host, simulation thread only: moves carried out and waiting to show in the pool.
struct Verifying {
    uint8_t slot;
    LockerMove move;
    size_t before;  // pieces of the move's kind in the locker when it was carried out
    ULONGLONG appliedAt;
};
std::vector<Verifying> g_verifying;

// Guest, simulation thread only.
std::map<uint64_t, ULONGLONG> g_lastChange;
std::set<uintptr_t> g_sweptPrivate;
std::set<uint64_t> g_missingLogged;
uint32_t g_epoch = ~0u;

std::vector<game::Cargo> capped(std::vector<game::Cargo> pieces) {
    cargo_diff::canonicalOrder(pieces);
    if (pieces.size() > locker_wire::kMaxLockerPieces) {
        logger::write("lockers: a locker holds %zu pieces, only %u are shared", pieces.size(), locker_wire::kMaxLockerPieces);
        pieces.resize(locker_wire::kMaxLockerPieces);
    }
    return pieces;
}

size_t countOf(const std::vector<game::Cargo>& pieces, const PieceKey& key) {
    return static_cast<size_t>(std::count_if(pieces.begin(), pieces.end(), [&](const game::Cargo& c) { return cargo_diff::keyOf(c) == key; }));
}

bool hostHoldsOrderPiece(uint64_t orderId) {
    const std::vector<game::Cargo> carried = game::carriedCargo();
    return std::any_of(carried.begin(), carried.end(), [orderId](const game::Cargo& c) { return c.orderId == orderId; });
}

// The piece a take removes: the same kind and order id, a plain piece the one nearest in durability.
const game::Cargo* pieceToTake(const std::vector<game::Cargo>& held, const game::Cargo& wanted) {
    const game::Cargo* best = nullptr;
    for (const game::Cargo& piece : held) {
        if (cargo_diff::keyOf(piece) != cargo_diff::keyOf(wanted)) continue;
        if (!best || std::fabs(piece.durability - wanted.durability) < std::fabs(best->durability - wanted.durability)) best = &piece;
    }
    return best;
}

// ---- host ----

void reply(uint8_t slot, const LockerMove& move, Reason reason, std::optional<std::vector<game::Cargo>> pieces) {
    lockers::Reply answer{slot, move.requestId, reason, std::nullopt};
    if (pieces) answer.locker = Locker{move.key, move.slotKind, capped(std::move(*pieces))};
    logger::write("lockers: move %u from slot %u (%s, locker %08x kind %u): %s (reason %u)", move.requestId, slot,
                  move.direction == locker_wire::kDirectionPut ? "put" : "take", move.key, move.slotKind,
                  reason == Reason::Ok ? "done" : "refused", static_cast<unsigned>(reason));
    std::lock_guard lock(g_mutex);
    g_replies.push_back(std::move(answer));
}

// Carries out one move on this world. A put or take that was asked of the game waits in g_verifying until the pool shows it.
void applyMove(const HostMove& item, ULONGLONG now) {
    const LockerMove& move = item.move;
    const auto held = game::lockerPieces(move.key, move.slotKind);
    if (!held) return reply(item.slot, move, Reason::UnknownLocker, std::nullopt);
    const game::Cargo piece = cargo_transfer::fromAdd(move.piece);
    const PieceKey key = cargo_diff::keyOf(piece);
    if (move.direction == locker_wire::kDirectionPut) {
        if (game::isOrderId(piece.orderId) && hostHoldsOrderPiece(piece.orderId)) return reply(item.slot, move, Reason::OrderHeld, held);
        switch (game::addLockerPiece(move.key, move.slotKind, piece)) {
            case game::AddResult::Done: break;
            case game::AddResult::Retry: {
                if (item.tries + 1 >= kMaxApplyTries) return reply(item.slot, move, Reason::OrderHeld, held);
                std::lock_guard lock(g_mutex);
                g_moves.push_back({item.slot, move, static_cast<uint8_t>(item.tries + 1), now + kApplyRetryMs});
                return;
            }
            case game::AddResult::Refused: return reply(item.slot, move, Reason::Full, held);
        }
    } else {
        const game::Cargo* taken = pieceToTake(*held, piece);
        if (!taken || !game::removeCargo(taken->handle)) return reply(item.slot, move, Reason::NotThere, held);
    }
    g_verifying.push_back({item.slot, move, countOf(*held, key), now});
}

void processMoves(ULONGLONG now) {
    std::vector<HostMove> moves;
    {
        std::lock_guard lock(g_mutex);
        moves.swap(g_moves);
    }
    for (const HostMove& item : moves) {
        if (item.notBefore > now) {
            std::lock_guard lock(g_mutex);
            g_moves.push_back(item);
        } else {
            applyMove(item, now);
        }
    }
}

// The answer to a carried out move once the locker shows it (or has not within kApplyGiveUpMs).
void verifyMoves(ULONGLONG now) {
    std::erase_if(g_verifying, [now](const Verifying& v) {
        if (now - v.appliedAt < kApplySettleMs) return false;
        const auto held = game::lockerPieces(v.move.key, v.move.slotKind);
        if (!held) {
            reply(v.slot, v.move, Reason::UnknownLocker, std::nullopt);
            return true;
        }
        const size_t after = countOf(*held, cargo_diff::keyOf(cargo_transfer::fromAdd(v.move.piece)));
        const bool put = v.move.direction == locker_wire::kDirectionPut;
        const bool shown = put ? after > v.before : after < v.before;
        if (!shown && now - v.appliedAt < kApplyGiveUpMs) return false;
        reply(v.slot, v.move, shown ? Reason::Ok : (put ? Reason::Full : Reason::NotThere), held);
        return true;
    });
}

void readLockers() {
    std::vector<game::LockerContents> found = game::lockerContents();
    if (found.empty()) return;
    Contents now;
    for (game::LockerContents& locker : found) now[idOf(locker.key, locker.kind)] = capped(std::move(locker.pieces));
    std::lock_guard lock(g_mutex);
    if (!g_read) {
        logger::write("lockers: the host reads %zu lockers; the first %zu:", now.size(), kLoggedIdentifiers);
        size_t listed = 0;
        for (const auto& [id, pieces] : now) {
            if (listed++ == kLoggedIdentifiers) break;
            logger::write("lockers:   %08x kind %u, %zu pieces", keyOfId(id), kindOfId(id), pieces.size());
        }
    }
    for (auto& [id, pieces] : now) {
        const auto known = g_known.find(id);
        if (g_read && (known == g_known.end() || !cargo_diff::sameContents(known->second, pieces))) {
            g_changed.push_back({keyOfId(id), kindOfId(id), pieces});
        }
        g_known[id] = std::move(pieces);
    }
    g_read = true;
}

void hostTick() {
    static ULONGLONG lastPoll = 0, lastRead = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_host.load() || !sim_tick::worldReady() || now - lastPoll < kHostPollMs) return;
    lastPoll = now;
    processMoves(now);
    verifyMoves(now);
    if (now - lastRead >= kHostReadMs) {
        lastRead = now;
        readLockers();
    }
}

// ---- guest ----

// The host's private lockers are not shared: the world a guest plays is the host's save, so what they hold is the host's.
// Each locker is emptied once when first seen, so what the guest stores there later stays.
void sweepPrivateLockers() {
    for (const game::PrivateLocker& locker : game::privateLockers()) {
        if (!g_sweptPrivate.insert(locker.owner).second || locker.handles.empty()) continue;
        for (const uint64_t handle : locker.handles) game::removeCargoLater(handle);
        logger::write("lockers: emptied the host's private locker at owner %p (%zu pieces)", reinterpret_cast<void*>(locker.owner),
                      locker.handles.size());
    }
}

void applyUndo() {
    std::vector<Undo> undo;
    {
        std::lock_guard lock(g_mutex);
        undo.swap(g_undo);
    }
    remote_apply::Scope applying;
    for (const Undo& item : undo) {
        if (item.move.direction == locker_wire::kDirectionTake) {
            game::removeCargoLater(item.move.guestHandle);
        } else if (game::addCargo(cargo_transfer::fromAdd(item.move.piece)) == game::AddResult::Retry && item.tries + 1 < kMaxUndoTries) {
            std::lock_guard lock(g_mutex);
            g_undo.push_back({item.move, static_cast<uint8_t>(item.tries + 1)});
        }
    }
}

// The lockers this world has that the host's snapshot did not name are empty on the host.
void emptyUnnamedLockers() {
    const std::vector<game::LockerContents> here = game::lockerContents();
    std::lock_guard lock(g_mutex);
    for (const game::LockerContents& locker : here) g_wanted.try_emplace(idOf(locker.key, locker.kind));
}

// Brings one locker to the host's list; the number of changes made.
int followLocker(uint64_t id, const std::vector<game::Cargo>& want, ULONGLONG now, int budget) {
    const auto held = game::lockerPieces(keyOfId(id), kindOfId(id));
    if (!held) {
        if (g_missingLogged.insert(id).second) logger::write("lockers: no locker %08x kind %u here yet", keyOfId(id), kindOfId(id));
        return 0;
    }
    cargo_diff::Handles have;
    for (const game::Cargo& piece : *held) have[cargo_diff::keyOf(piece)].push_back(piece.handle);
    const cargo_diff::Counts wantCounts = cargo_diff::countsOf(want);
    const std::vector<uint64_t> surplus = cargo_diff::extra(have, wantCounts);
    cargo_diff::Counts lacking = cargo_diff::missing(cargo_diff::countsOf(have), wantCounts);
    {
        std::lock_guard lock(g_mutex);
        std::erase_if(lacking, [&](const auto& item) { return g_refused.contains({id, item.first}); });
    }
    if (surplus.empty() && lacking.empty()) return 0;
    if (now - g_lastChange[id] < kFollowSettleMs) return 0;
    g_lastChange[id] = now;
    remote_apply::Scope applying;
    int changes = 0;
    for (size_t i = 0; i < surplus.size() && changes < budget; ++i, ++changes) game::removeCargoLater(surplus[i]);
    for (const auto& [key, shortfall] : lacking) {
        std::vector<game::Cargo> pieces;
        std::copy_if(want.begin(), want.end(), std::back_inserter(pieces), [&](const game::Cargo& c) { return cargo_diff::keyOf(c) == key; });
        for (size_t i = pieces.size() - shortfall; i < pieces.size() && changes < budget; ++i) {
            const game::AddResult result = game::addLockerPiece(keyOfId(id), kindOfId(id), pieces[i]);
            changes += result == game::AddResult::Done ? 1 : 0;
            if (result == game::AddResult::Refused) {
                std::lock_guard lock(g_mutex);
                g_refused.insert({id, key});
                break;
            }
        }
    }
    logger::write("lockers: locker %08x kind %u brought to the host's list: %zu pieces removed, %zu wanted more", keyOfId(id),
                  kindOfId(id), surplus.size(), lacking.size());
    return changes;
}

void followLockers(ULONGLONG now) {
    Contents wanted;
    {
        std::lock_guard lock(g_mutex);
        wanted = g_wanted;
    }
    int budget = kMaxChangesPerRound;
    for (const auto& [id, pieces] : wanted) {
        if (budget <= 0) break;
        {
            std::lock_guard lock(g_mutex);
            if (g_pending.pending(keyOfId(id), kindOfId(id))) continue;
        }
        budget -= followLocker(id, pieces, now, budget);
    }
}

void guestTick() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_guest.load() || !sim_tick::worldReady() || now - last < kGuestPollMs) return;
    last = now;
    if (const uint32_t epoch = sim_tick::gameplayEpoch(); epoch != g_epoch) {
        g_epoch = epoch;
        g_sweptPrivate.clear();
        g_lastChange.clear();
        g_missingLogged.clear();
    }
    {
        std::lock_guard lock(g_mutex);
        const uint64_t before = g_pending.expiredTotal();
        g_pending.expire(now);
        if (g_pending.expiredTotal() != before) logger::write("lockers: %llu moves went unanswered", static_cast<unsigned long long>(g_pending.expiredTotal()));
    }
    sweepPrivateLockers();
    applyUndo();
    if (game::uiMenuOpen()) return;
    bool endPending = false;
    {
        std::lock_guard lock(g_mutex);
        endPending = std::exchange(g_endPending, false);
    }
    if (endPending) emptyUnnamedLockers();
    followLockers(now);
}

}  // namespace

namespace lockers {

void installEarly() {
    sim_tick::add(&hostTick, "lockers host", sim_tick::Gate::Gameplay);
    sim_tick::add(&guestTick, "lockers guest", sim_tick::Gate::Gameplay);
}

void setRole(bool host, bool guest) {
    g_host = host;
    g_guest = guest;
    std::lock_guard lock(g_mutex);
    if (!host) {
        g_known.clear();
        g_read = false;
        g_changed.clear();
        g_moves.clear();
        g_replies.clear();
    }
    if (!guest) {
        g_wanted.clear();
        g_endPending = false;
        g_pending.clear();
        g_outgoing.clear();
        g_undo.clear();
        g_refused.clear();
    }
}

bool watching() { return g_guest.load(); }

std::optional<std::vector<Locker>> snapshot() {
    std::lock_guard lock(g_mutex);
    if (!g_read) return std::nullopt;
    std::vector<Locker> out;
    for (const auto& [id, pieces] : g_known) {
        if (!pieces.empty()) out.push_back({keyOfId(id), kindOfId(id), pieces});
    }
    return out;
}

std::vector<Locker> takeChanged() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_changed, {});
}

void queueMove(uint8_t slot, const locker_wire::LockerMove& move) {
    std::lock_guard lock(g_mutex);
    g_moves.push_back({slot, move, 0, 0});
}

std::vector<Reply> takeReplies() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_replies, {});
}

void follow(Locker list) {
    const uint64_t id = idOf(list.key, list.kind);
    std::lock_guard lock(g_mutex);
    std::erase_if(g_refused, [id](const auto& item) { return item.first == id; });
    g_wanted[id] = capped(std::move(list.pieces));
}

void endSnapshot() {
    std::lock_guard lock(g_mutex);
    g_endPending = true;
}

void noteLocalMove(uint8_t direction, const game::LockerRef& locker, uint64_t handle, const game::Cargo& piece) {
    if (!g_guest.load()) return;
    LockerMove move{};
    move.key = locker.key;
    move.slotKind = locker.kind;
    move.direction = direction;
    move.guestHandle = handle;
    move.piece = cargo_transfer::toAdd(piece);
    std::lock_guard lock(g_mutex);
    move = g_pending.add(move, GetTickCount64());
    g_outgoing.push_back(move);
    logger::write("lockers: the local player %s locker %08x kind %u (kind %u, order %llx), request %u",
                  direction == locker_wire::kDirectionPut ? "put a piece into" : "took a piece out of", locker.key, locker.kind,
                  piece.type, static_cast<unsigned long long>(piece.orderId), move.requestId);
}

std::vector<locker_wire::LockerMove> takeLocalMoves() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_outgoing, {});
}

void onResult(const locker_wire::LockerResult& result) {
    std::lock_guard lock(g_mutex);
    const std::optional<LockerMove> move = g_pending.resolve(result.requestId);
    if (!move) return;
    if (result.accepted) return;
    logger::write("lockers: the host refused move %u (reason %u): undoing it", result.requestId, result.reason);
    g_undo.push_back({*move, 0});
    toast_queue::push(kRefusedText, kToastSeconds);
}

}  // namespace lockers
