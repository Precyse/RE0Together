#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "partner_rules.h"

// Reads of the game's player list and session members for milestone 4. Plain pointer reads (the getters call this on
// every use), every list walk stops at partner_rules::kMaxWalked and every hop is null checked.
namespace remote_players {

struct MemberInfo {
    uintptr_t address;
    uint8_t slot;
    uint32_t state;
    bool local;
};

// The session as it is now. Left unfilled beyond the counts, so taking one costs only the reads.
struct Snapshot {
    bool active;  // co-op flag set and a session object exists
    uintptr_t localMember;
    uintptr_t hostMember;
    MemberInfo members[partner_rules::kMaxWalked];
    size_t memberCount;
    partner_rules::HumanEntry humans[partner_rules::kMaxWalked];  // remote player humans in list order
    size_t humanCount;
    partner_rules::Pairing pairing;  // the remotes of both lists joined by slot
};

// The co-op session as it is now (`active` false outside co-op), or just its members whether or not the flag is set.
void take(Snapshot& out);
void takeMembers(Snapshot& out);

// What the two getters answer when no override is set. `trusted` false: the game's own answer stands (outside
// co-op, or a remote human whose owner slot matches no member).
struct Partner {
    bool trusted;
    uintptr_t human;
    uintptr_t member;
};

Partner defaultPartner(const Snapshot& snapshot);

// True when a member of `session` other than its local one has a state of at least `minState`.
bool anyRemoteMemberAtLeast(uintptr_t session, int32_t minState);

// The player human a remote member owns, 0 when it has none yet or the mapping cannot be trusted.
uintptr_t humanOfMember(uintptr_t member);

// The remote member that owns `human`, 0 for the local player, an unknown human or an untrusted mapping.
uintptr_t memberOfHuman(uintptr_t human);

// True when `member` is a member of the session other than the local one.
bool isRemoteMember(uintptr_t member);

// The host member and this machine's own member of the session, 0 outside a session.
uintptr_t hostMember();
uintptr_t localMember();

// The remotes that have a human, in ascending slot order, at most `capacity` of them; how many were written to `out`.
size_t remotesWithHuman(partner_rules::Remote* out, size_t capacity);

// How many remote player humans there are.
size_t remoteHumanCount();

// The remote player humans in list order (the list walk, not the pairing: a human whose member is not known yet is included), at
// most `capacity` of them; how many were written to `out`.
size_t remoteHumans(uintptr_t* out, size_t capacity);

// How many members of the session are not the local one.
size_t remoteMemberCount();

// The net id every machine knows a human by (its net record +2), sr2::kNoNetId while it has no net record.
uint16_t netIdOf(uintptr_t human);

// The object a handle names (looked up in the object table), 0 when the handle is stale or empty.
uintptr_t objectOfHandle(uint32_t handle);

// The game's local player human, 0 outside a session.
uintptr_t localHuman();

// True for a human in the player list that is not the local player.
bool isRemotePlayerHuman(uintptr_t human);

// What the trace shows of how a human is drawn (milestone 14 PV-T): its hidden flags (+0x80 & 0x14), whether its render instance is
// hidden (bit 3 of the word at +0x244), whether its char instance has a skin composite object, its character type and the record
// the char table holds for its pool index. All zero for a human without the pointers.
struct Visuals {
    uint8_t hidden;
    uint16_t render;
    bool skin;
    uint32_t type;
    uint32_t record;
};
Visuals visualsOf(uintptr_t human);

// Fields of a human.
partner_rules::Vec3 positionOf(uintptr_t object);
int32_t downedTimerOf(uintptr_t human);
bool isJoining(uintptr_t human);

// The remote closest to `point` among those that have a human: false without one. Float math: callers inside game
// code guard the FPU.
bool nearestRemote(const partner_rules::Vec3& point, partner_rules::Remote& out);

// The human of nearestRemote, 0 without one.
uintptr_t nearestRemoteTo(const partner_rules::Vec3& point);

// Everything the trace logs (members, their slots and states, remote humans, slots, join flags, downed timers) folded into
// one number, so that the getters can tell cheaply that nothing changed.
uint64_t signatureOf(const Snapshot& snapshot);

// One line per remote human for the trace: owner slot, the member it maps to, join flags, downed timer, default.
std::vector<std::string> describeRemotes(const Snapshot& snapshot);

}  // namespace remote_players
