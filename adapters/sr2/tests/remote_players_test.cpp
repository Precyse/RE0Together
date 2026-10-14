// The session and player list reads and the partner getters of milestone 4, against a made-up game state placed at the
// addresses the real game uses (the exe's .data is not mapped in a test, so the pages are allocated here).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/partner_scope.h"
#include "../src/remote_players.h"
#include "../src/sr2_addresses.h"

namespace logger {
void write(const char*, ...) {}
}  // namespace logger

namespace {

constexpr uintptr_t kPlayerPage = 0x2170000;
constexpr uintptr_t kSessionPage = 0x2528000;
constexpr size_t kPageBytes = 0x2000;
constexpr size_t kPlayers = 3;
constexpr size_t kHumanBytes = 0x1400;
constexpr size_t kMemberBytes = 0x2400;
constexpr size_t kSessionBytes = 0x1000;
constexpr size_t kRecordBytes = 0x20;
constexpr uintptr_t kFakeOriginal = 0x77;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

uint32_t& at(void* base, uintptr_t offset) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(base) + offset); }
uint32_t& word(uintptr_t address) { return *reinterpret_cast<uint32_t*>(address); }
uintptr_t addr(void* pointer) { return reinterpret_cast<uintptr_t>(pointer); }

// Three players, slot 0 the host. Humans and members of slot n are humans[n] and members[n].
struct World {
    std::vector<uint8_t> humans[kPlayers];
    std::vector<uint8_t> members[kPlayers];
    std::vector<uint8_t> records[kPlayers];
    std::vector<uint8_t> session = std::vector<uint8_t>(kSessionBytes);

    World() {
        for (size_t slot = 0; slot < kPlayers; ++slot) {
            humans[slot].assign(kHumanBytes, 0);
            members[slot].assign(kMemberBytes, 0);
            records[slot].assign(kRecordBytes, 0);
            records[slot][0] = static_cast<uint8_t>(slot);
            at(humans[slot].data(), sr2::kObjectNetRecordOffset) = addr(records[slot].data());
            members[slot][sr2::kMemberSlotOffset] = static_cast<uint8_t>(slot);
            at(members[slot].data(), sr2::kMemberStateOffset) = sr2::kMemberStateInGame;
        }
    }

    uintptr_t human(size_t slot) { return addr(humans[slot].data()); }
    uintptr_t member(size_t slot) { return addr(members[slot].data()); }

    // The player list and the member list in the given slot order; the member list is circular, the player list ends
    // with a null. `local` is the slot of this machine.
    void place(const size_t (&memberOrder)[kPlayers], const size_t (&humanOrder)[kPlayers], size_t local) {
        for (size_t i = 0; i < kPlayers; ++i) {
            at(members[memberOrder[i]].data(), sr2::kMemberNextOffset) = member(memberOrder[(i + 1) % kPlayers]);
            at(humans[humanOrder[i]].data(), sr2::kHumanNextOffset) = i + 1 < kPlayers ? human(humanOrder[i + 1]) : 0;
        }
        at(session.data(), sr2::kSessionMemberListOffset) = member(memberOrder[0]);
        at(session.data(), sr2::kSessionHostMemberOffset) = member(0);
        at(session.data(), sr2::kSessionLocalMemberOffset) = member(local);
        word(sr2::kSessionPointer) = addr(session.data());
        word(sr2::kPlayerListHead) = human(humanOrder[0]);
        word(sr2::kLocalPlayer) = human(local);
        *reinterpret_cast<uint8_t*>(sr2::kCoopFlag) = 1;
    }
};

uintptr_t __cdecl fakeOriginalGetter() { return kFakeOriginal; }

using GetterFn = uintptr_t(__cdecl*)();

GetterFn humanGetter() {
    const partner_scope::GetterHook hook = partner_scope::humanHook();
    *hook.original = reinterpret_cast<void*>(&fakeOriginalGetter);
    return reinterpret_cast<GetterFn>(hook.detour);
}

GetterFn memberGetter() {
    const partner_scope::GetterHook hook = partner_scope::memberHook();
    *hook.original = reinterpret_cast<void*>(&fakeOriginalGetter);
    return reinterpret_cast<GetterFn>(hook.detour);
}

bool mapPages() {
    return VirtualAlloc(reinterpret_cast<void*>(kPlayerPage), kPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) &&
           VirtualAlloc(reinterpret_cast<void*>(kSessionPage), kPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void hostPicksTheLowestSlot() {
    World world;
    const size_t members[] = {2, 0, 1};
    const size_t humans[] = {2, 1, 0};
    world.place(members, humans, 0);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    check(snapshot.active && snapshot.memberCount == 3 && snapshot.humanCount == 2, "host: 3 members, 2 remote humans");
    check(snapshot.pairing.trusted && snapshot.pairing.count == 2 && snapshot.pairing.items[0].slot == 1,
          "host: remotes paired in slot order whatever the list order");
    const remote_players::Partner partner = remote_players::defaultPartner(snapshot);
    check(partner.trusted && partner.human == world.human(1) && partner.member == world.member(1),
          "host: the default partner is the slot 1 player, not the first of the list");
    check(humanGetter()() == world.human(1) && memberGetter()() == world.member(1), "host: both getters answer it");
}

void guestPicksTheHost() {
    World world;
    const size_t members[] = {1, 2, 0};
    const size_t humans[] = {1, 0, 2};
    world.place(members, humans, 2);
    check(humanGetter()() == world.human(0) && memberGetter()() == world.member(0),
          "guest: the host is the partner although another guest comes first in the list");
    world.place(members, humans, 1);
    check(humanGetter()() == world.human(0), "the other guest agrees");
}

void aJoiningMemberHasNoHumanYet() {
    World world;
    const size_t members[] = {0, 1, 2};
    const size_t humans[] = {0, 1, 2};
    world.place(members, humans, 0);
    at(world.humans[0].data(), sr2::kHumanNextOffset) = 0;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    check(snapshot.pairing.trusted && snapshot.pairing.count == 2 && snapshot.pairing.items[0].human == 0 &&
              snapshot.pairing.items[0].member == world.member(1),
          "host alone with two joining members: the members are listed without humans");
    check(humanGetter()() == 0, "no remote human: the human getter answers none");
    check(memberGetter()() == kFakeOriginal, "no default member: the member getter keeps the game's answer");
}

void anUnmatchedOwnerSlotKeepsTheGamesAnswer() {
    World world;
    const size_t members[] = {0, 1, 2};
    const size_t humans[] = {2, 1, 0};
    world.place(members, humans, 0);
    world.records[1][0] = 9;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    check(!snapshot.pairing.trusted && snapshot.pairing.count == 1 && snapshot.pairing.items[0].human == world.human(2) &&
              snapshot.pairing.items[0].member == world.member(1),
          "an owner slot with no member: the fallback is the first of each list");
    check(humanGetter()() == kFakeOriginal && memberGetter()() == kFakeOriginal,
          "the getters pass the call on to the game");
}

void outsideCoopTheGamesAnswerStands() {
    World world;
    const size_t order[] = {0, 1, 2};
    world.place(order, order, 0);
    *reinterpret_cast<uint8_t*>(sr2::kCoopFlag) = 0;
    check(humanGetter()() == kFakeOriginal && memberGetter()() == kFakeOriginal, "co-op flag clear: the game answers");
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    check(!snapshot.active, "co-op flag clear: the snapshot is inactive");
    remote_players::takeMembers(snapshot);
    check(snapshot.memberCount == 3, "the members can still be read");
}

void overridesAnswerTheirTurn() {
    World world;
    const size_t order[] = {0, 1, 2};
    world.place(order, order, 0);
    {
        partner_scope::Override outer({world.human(2), world.member(2)});
        check(humanGetter()() == world.human(2) && memberGetter()() == world.member(2), "override answers");
        {
            partner_scope::Override inner({world.human(1), world.member(1)});
            check(humanGetter()() == world.human(1), "a nested override answers");
        }
        check(humanGetter()() == world.human(2), "the outer override is back");
    }
    check(humanGetter()() == world.human(1), "no override: the default again");

    uintptr_t seen[4] = {};
    size_t visits = 0;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        seen[visits++] = human;
        seen[visits++] = humanGetter()();
    });
    check(visits == 4 && seen[0] == world.human(1) && seen[1] == world.human(1) && seen[2] == world.human(2) &&
              seen[3] == world.human(2),
          "each remote human in slot order, and the getter answers that human meanwhile");
    check(humanGetter()() == world.human(1), "the override is gone after the loop");

    visits = 0;
    const bool found = partner_scope::anyRemoteHuman([&](uintptr_t human) {
        ++visits;
        return human == world.human(1);
    });
    check(found && visits == 1, "anyRemoteHuman stops at the first true");

    visits = 0;
    uintptr_t members[4] = {};
    partner_scope::eachRemoteMember([&](uintptr_t member) { members[visits++] = member; });
    check(visits == 2 && members[0] == world.member(1) && members[1] == world.member(2), "each remote member in slot order");
}

void withoutRemotesALoopRunsOnceWithoutAnOverride() {
    World world;
    const size_t order[] = {0, 1, 2};
    world.place(order, order, 0);
    at(world.humans[0].data(), sr2::kHumanNextOffset) = 0;
    word(sr2::kPlayerListHead) = world.human(0);
    size_t visits = 0;
    uintptr_t seen = 1;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        ++visits;
        seen = human;
    });
    check(visits == 1 && seen == 0, "no remote human: one call with 0, as the shipped game would run it");
}

void nearestAndIdentity() {
    World world;
    const size_t order[] = {0, 1, 2};
    world.place(order, order, 0);
    const float farAway[3] = {500, 0, 0};
    const float close[3] = {5, 0, 0};
    std::memcpy(world.humans[1].data() + sr2::kObjectPositionOffset, farAway, sizeof(farAway));
    std::memcpy(world.humans[2].data() + sr2::kObjectPositionOffset, close, sizeof(close));
    check(remote_players::nearestRemoteTo({0, 0, 0}) == world.human(2), "the nearest remote human to a point");
    partner_rules::Remote nearest;
    check(remote_players::nearestRemote({600, 0, 0}, nearest) && nearest.human == world.human(1) &&
              nearest.member == world.member(1),
          "the nearest remote with its member");
    check(remote_players::isRemotePlayerHuman(world.human(2)) && !remote_players::isRemotePlayerHuman(world.human(0)) &&
              !remote_players::isRemotePlayerHuman(0) && !remote_players::isRemotePlayerHuman(0x1234),
          "remote player human: in the list and not the local one");
    at(world.humans[1].data(), sr2::kHumanDownedTimerOffset) = static_cast<uint32_t>(-1);
    world.humans[1][sr2::kHumanJoinFlagsOffset] = sr2::kHumanJoiningBit;
    check(remote_players::downedTimerOf(world.human(1)) == -1 && remote_players::isJoining(world.human(1)) &&
              !remote_players::isJoining(world.human(2)),
          "downed timer and join flag");
}

void anyRemoteMemberReadyIgnoresTheLocalAndJoiningOnes() {
    World world;
    const size_t order[] = {0, 1, 2};
    world.place(order, order, 0);
    const uintptr_t session = addr(world.session.data());
    constexpr int32_t kReady = static_cast<int32_t>(sr2::kMemberStateReady);
    check(remote_players::anyRemoteMemberAtLeast(session, kReady), "two remotes in the game: ready");
    at(world.members[1].data(), sr2::kMemberStateOffset) = kReady - 1;
    check(remote_players::anyRemoteMemberAtLeast(session, kReady), "one remote still joining, the other ready: ready");
    at(world.members[2].data(), sr2::kMemberStateOffset) = kReady - 1;
    check(!remote_players::anyRemoteMemberAtLeast(session, kReady), "both remotes joining: not ready");
    at(world.members[2].data(), sr2::kMemberStateOffset) = kReady;
    check(remote_players::anyRemoteMemberAtLeast(session, kReady), "a remote at exactly the ready state counts");
    world.place(order, order, 2);
    at(world.members[1].data(), sr2::kMemberStateOffset) = kReady - 1;
    at(world.members[0].data(), sr2::kMemberStateOffset) = kReady - 1;
    at(world.members[2].data(), sr2::kMemberStateOffset) = sr2::kMemberStateInGame;
    check(!remote_players::anyRemoteMemberAtLeast(session, kReady), "only the local member is in the game: not ready");
    at(world.members[0].data(), sr2::kMemberStateOffset) = sr2::kMemberStateInGame;
    check(remote_players::anyRemoteMemberAtLeast(session, kReady), "a guest sees the host in the game: ready");
}

}  // namespace

int main() {
    if (!mapPages()) {
        std::printf("FAIL the game's addresses 0x%X and 0x%X could not be mapped in this process\n",
                    static_cast<unsigned>(kPlayerPage), static_cast<unsigned>(kSessionPage));
        return 1;
    }
    hostPicksTheLowestSlot();
    guestPicksTheHost();
    aJoiningMemberHasNoHumanYet();
    anUnmatchedOwnerSlotKeepsTheGamesAnswer();
    outsideCoopTheGamesAnswerStands();
    overridesAnswerTheirTurn();
    withoutRemotesALoopRunsOnceWithoutAnOverride();
    nearestAndIdentity();
    anyRemoteMemberReadyIgnoresTheLocalAndJoiningOnes();
    return g_failures == 0 ? 0 : 1;
}
