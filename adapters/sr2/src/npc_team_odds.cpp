// AI12: the outnumbered-damage odds of the game (0x9BC5D0, cdecl int(human)): the NPCs that target a player's team minus the size of
// that team. The shipped function counts the team of one player, its followers and "the other player" with its followers; this is
// the same walk with every other player, so players 3 and 4 and their homies are counted too. With fewer than two remotes, or a
// pairing that cannot be trusted, the game's own function runs.

#include <windows.h>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "party_ai_rules.h"
#include "party_ai_thunks.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "squared_distance.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;
using party_ai_rules::HumanList;

constexpr size_t kMaxTeamWalk = sr2::kNpcHumanPool;

using OddsFunction = int(__cdecl*)(uintptr_t human);

void* g_oddsOriginal = nullptr;
rate_limit::RateLimit g_oddsLine;

// What one evaluation of the odds found.
struct Odds {
    int team;
    int enemies;
    uint32_t nearOthers;
};

// The object a human's handle resolves to (0 when the handle is stale or empty).
uintptr_t resolved(uintptr_t human) { return remote_players::objectOfHandle(dwordField(human, sr2::kObjectHandleOffset)); }

bool isHumanOfKind(uintptr_t object, uint8_t kind) {
    return object && dwordField(object, sr2::kObjectTypeOffset) == sr2::kObjectTypeHuman && byteField(object, sr2::kHumanKindOffset) == kind;
}

bool isInPlay(uintptr_t human) {
    const uint32_t flags = dwordField(human, sr2::kHumanVisibilityFlagsOffset);
    return (flags & sr2::kHumanInactiveFlags) == 0 && (flags & sr2::kHumanHiddenBit) == 0;
}

// True when the two humans are within the odds range of each other. Runs under the caller's FpuGuard.
bool isInRange(uintptr_t first, uintptr_t second) {
    return squared_distance::between(sr2::kSquaredDistanceTeam, first + sr2::kObjectPositionOffset,
                                     second + sr2::kObjectPositionOffset) <= sr2::kTeamOddsRangeSquared;
}

// The followers of `leader` that are NPCs in play and free; when `around` is not 0 only those within range of that human.
uint32_t followersOf(uintptr_t leader, uintptr_t around) {
    uint32_t counted = 0;
    const int32_t followers = static_cast<int32_t>(dwordField(leader, sr2::kHumanFollowerCountOffset));
    for (int32_t i = 0; i < followers; ++i) {
        const uint32_t handle = dwordField(leader, sr2::kHumanFollowerSlotsOffset + static_cast<uintptr_t>(i) * sr2::kHumanFollowerStride);
        const uintptr_t follower = remote_players::objectOfHandle(handle);
        if (!isHumanOfKind(follower, sr2::kHumanKindNpc) || !isInPlay(follower) || party_ai_thunks::humanIsBusy(follower)) continue;
        if (around == 0 || isInRange(follower, around)) ++counted;
    }
    return counted;
}

// True when `target` is `player` or follows it: the leader handle of the target is the handle of the player.
bool onTeamOf(uintptr_t target, uintptr_t player) {
    return target == player || dwordField(target, sr2::kHumanLeaderOffset) == dwordField(player, sr2::kObjectHandleOffset);
}

// True when the NPC is alive, in play and its handle is its own.
bool isLiveNpc(uintptr_t npc) {
    return isInPlay(npc) && resolved(npc) == npc && *reinterpret_cast<const float*>(npc + sr2::kHumanHealthOffset) > 0.0f &&
           static_cast<int32_t>(dwordField(npc, sr2::kHumanDownedTimerOffset)) < 0;
}

// The NPCs that target the head's team, or a near other's team, and are hostile and armed.
int enemiesOf(uintptr_t human, uintptr_t head, const HumanList& others) {
    int enemies = 0;
    uintptr_t npc = dwordField(sr2::kNpcListHead, 0);
    const uintptr_t first = npc;
    for (size_t steps = 0; npc && steps < kMaxTeamWalk; ++steps) {
        if (isLiveNpc(npc)) {
            const uintptr_t target = guest_call::call(sr2::kNpcTargetOf, {.eax = npc});
            bool targetsHead = false;
            bool targetsNearOther = false;
            if (target) {
                targetsHead = onTeamOf(target, head);
                for (const uintptr_t other : others.span()) {
                    if (!onTeamOf(target, other)) continue;
                    targetsNearOther = isInRange(human, target);
                    break;
                }
            }
            bool hostile = false;
            bool armed = false;
            if (targetsHead || targetsNearOther) {
                hostile = (guest_call::call(sr2::kNpcHostileTest, {.eax = npc}, {target, 0}) & 0xFF) != 0;
                armed = hostile && guest_call::call(sr2::kNpcArmedTest, {}, {npc + sr2::kNpcArmedStateOffset, 1, 0, 0, 0}) != 0;
            }
            if (party_ai_rules::countsAsEnemy(targetsHead, targetsNearOther, hostile, armed)) ++enemies;
        }
        npc = dwordField(npc, sr2::kNpcHumanLinkOffset);
        if (npc == first) break;
    }
    return enemies;
}

// The odds for `human` whose team head is `head`, counting the players in `others` as the head's friends.
Odds oddsWith(uintptr_t human, uintptr_t head, const HumanList& others) {
    party_ai_rules::OtherTeam teams[party_ai_rules::kMaxPlayerHumans];
    uint32_t nearOthers = 0;
    for (size_t i = 0; i < others.count; ++i) {
        const bool inRange = isInRange(others.items[i], human);
        teams[i] = {inRange, inRange ? followersOf(others.items[i], human) : 0};
        if (inRange) ++nearOthers;
    }
    const int team = static_cast<int>(party_ai_rules::teamSize(followersOf(head, 0), {teams, others.count}));
    return {team, enemiesOf(human, head, others), nearOthers};
}

// The head of the team `human` is on: itself when it is a player, else its effective leader. 0 when that is not a player either.
uintptr_t headOf(uintptr_t human) {
    const uintptr_t head = isHumanOfKind(resolved(human), sr2::kHumanKindPlayer) ? human : guest_call::call(sr2::kEffectiveLeader, {.edx = human});
    return head && isHumanOfKind(resolved(head), sr2::kHumanKindPlayer) ? head : 0;
}

void traceOdds(uintptr_t human, uintptr_t head, const Odds& odds, size_t others) {
    uint32_t lines = 0;
    if (!g_oddsLine.allow(GetTickCount64(), lines)) return;
    logger::write("trace: npc ai: team odds of 0x%08X: head 0x%08X, team %d (others near %u of %zu), enemies %d",
                  static_cast<unsigned>(human), static_cast<unsigned>(head), odds.team, odds.nearOthers, others, odds.enemies);
}

int __cdecl teamOddsEveryPlayer(uintptr_t human) {
    const OddsFunction shipped = reinterpret_cast<OddsFunction>(g_oddsOriginal);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const HumanList remotes = party_ai_rules::remoteHumansOf(snapshot.pairing);
    if (party_ai_rules::shippedPath(snapshot.pairing.trusted, remotes.count)) return shipped(human);
    if (!human) return 0;

    FpuGuard guard;
    const uintptr_t head = headOf(human);
    if (!head) return 0;
    const uintptr_t local = remote_players::localHuman();
    const HumanList others = party_ai_rules::othersOf(head, local, remotes.span());
    const Odds odds = oddsWith(human, head, others);
    const int result = party_ai_rules::teamOdds(odds.enemies, odds.team);
    if (party_ai_thunks::traceOn()) {
        HumanList shippedOthers;
        const uintptr_t other = party_ai_rules::shippedOther(head, local, partner_scope::partnerHuman());
        if (other) shippedOthers.add(other);
        const Odds shippedOdds = oddsWith(human, head, shippedOthers);
        party_ai_thunks::count(&party_ai_rules::AiCounters::odds, result != party_ai_rules::teamOdds(shippedOdds.enemies, shippedOdds.team));
        traceOdds(human, head, odds, others.count);
    }
    return result;
}

}  // namespace

namespace party_ai_thunks {

partner_thunks::Binding bindTeamOdds(Thunk thunk) {
    if (thunk != Thunk::TeamOddsEveryPlayer) return {nullptr, nullptr};
    return {reinterpret_cast<void*>(&teamOddsEveryPlayer), &g_oddsOriginal};
}

}  // namespace party_ai_thunks
