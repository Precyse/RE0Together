#pragma once
// DS2-internal: the shared locker, engine side (docs/DS2_NOTES.md, "Shared locker"; the wire is locker_wire.h). The host
// reads every locker of its world once a second and hands the changed ones to the net thread, and carries out a guest's
// move (put or take) on its own world. A guest brings each locker to the host's list on the simulation tick, reports the
// moves its own game makes (partner_cargo's slot hooks) and undoes the ones the host refused. A guest never keeps the
// host's private lockers (slot kind 27): their pieces are deleted in memory when the world is first seen, never in a save.
#include <cstdint>
#include <optional>
#include <vector>

#include "game.h"
#include "locker_wire.h"

namespace lockers {

struct Locker {
    uint32_t key = 0;
    uint8_t kind = 0;
    std::vector<game::Cargo> pieces;  // in canonical order, at most locker_wire::kMaxLockerPieces
};

// What the host answers a guest's move with: the result, and the locker as it stands after the move (none when this world
// has no such locker).
struct Reply {
    uint8_t slot = 0;
    uint32_t requestId = 0;
    locker_wire::Reason reason = locker_wire::Reason::Ok;
    std::optional<Locker> locker;
};

// Start-up: the host's read and the guest's follow on the simulation tick. They do nothing until setRole says so.
void installEarly();

// The session role, set from the net thread each tick.
void setRole(bool host, bool guest);

// A guest: the game's slot hooks report local moves (partner_cargo). Any thread.
bool watching();

// Host, net thread. Every locker the last read found non-empty, or nothing before the first read.
std::optional<std::vector<Locker>> snapshot();

// Host, net thread. The lockers whose contents changed since the last call (a locker that became empty too).
std::vector<Locker> takeChanged();

// Host, net thread: a guest's move, carried out on the simulation thread; the answer comes back through takeReplies.
void queueMove(uint8_t slot, const locker_wire::LockerMove& move);
std::vector<Reply> takeReplies();

// Guest, net thread: the host's list for one locker, and the end of a snapshot (the lockers not named are empty).
void follow(Locker list);
void endSnapshot();

// Guest. A piece moved between the local player's reach and a shared locker by the game itself: registers the move as waiting
// for the host's answer and queues it to send. Any thread.
void noteLocalMove(uint8_t direction, const game::LockerRef& locker, uint64_t handle, const game::Cargo& piece);
std::vector<locker_wire::LockerMove> takeLocalMoves();

// Guest, net thread: the host's answer; a refusal undoes the move on the next simulation tick.
void onResult(const locker_wire::LockerResult& result);

}  // namespace lockers
