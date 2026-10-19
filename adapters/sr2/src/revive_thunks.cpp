#include "revive_thunks.h"

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "revive_rules.h"
#include "revive_state.h"
#include "sender_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using revive_rules::Player;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;
constexpr uint32_t kYes = 1;
constexpr uint32_t kNo = 0;

void* g_resetOriginal = nullptr;
const uintptr_t g_killLocal = sr2::kKillLocalHuman;
const uintptr_t g_killResume = sr2::kMiscDeathKillResume;
const uintptr_t g_keepResume = sr2::kMiscDeathKeepResume;
const uintptr_t g_missionStartResume = sr2::kMissionStartDownedResume;

// The remote whose death message the calling thread is handling.
struct Sender {
    uintptr_t human;
    uint8_t slot;
};

// False for a message of this machine, outside any handler or from a member that has no human: the shipped rule then stands.
bool senderOfMessage(Sender& out) {
    const uintptr_t member = sender_scope::currentSender();
    out.human = member ? remote_players::humanOfMember(member) : 0;
    out.slot = member ? readByte(member + sr2::kMemberSlotOffset) : partner_rules::kNoSlot;
    if (!out.human && revive_state::trace()) logger::write("trace: respawn wait: death message without a sender, shipped rule");
    return out.human != 0;
}

// RW1 (misc_19 on the host, esi = the local player): true when the team is wiped now. The game's own test asked the local player's wait
// only; now every remote other than the dead one must be down or waiting too, and the dead one's wait is kept for its slot.
uint32_t __cdecl remoteDeathWait(uintptr_t local) {
    Sender sender;
    if (!senderOfMessage(sender)) return revive_state::gameWaiting(local) ? kYes : kNo;
    Player remotes[kMaxRemotes];
    const size_t count = revive_state::remotes(remotes, kMaxRemotes);
    const bool localWaiting = revive_state::localWaiting();
    const bool others = revive_rules::othersDownOrWaiting(remotes, count, sender.human);
    const bool wipe = localWaiting && others;
    const int32_t now = revive_state::now();
    if (wipe) {
        revive_state::clearWaits();
    } else {
        revive_state::recordDeath(sender.slot);
    }
    if (revive_state::trace()) {
        logger::write("trace: respawn wait: slot %u died, waits until %d (now %d); local waiting %d, others down or waiting %d -> wipe %d",
                      static_cast<unsigned>(sender.slot), revive_rules::deadlineAfter(now, sr2::kRespawnWaitMs), now, localWaiting ? 1 : 0,
                      others ? 1 : 0, wipe ? 1 : 0);
    }
    return wipe ? kYes : kNo;
}

// RW2 (misc_19 tail, every machine, the local player bleeds out): true when the local player dies now, as the game does for any
// death of a remote; a third player who can still revive keeps it alive.
uint32_t __cdecl downedLocalKill() {
    Sender sender;
    if (!senderOfMessage(sender)) return kYes;
    Player remotes[kMaxRemotes];
    const size_t count = revive_state::remotes(remotes, kMaxRemotes);
    const bool kill = revive_rules::killDownedLocal(true, remotes, count, sender.human);
    if (revive_state::trace()) {
        logger::write("trace: downed local: slot %u died; %s -> %s", static_cast<unsigned>(sender.slot),
                      revive_rules::playersText(remotes, count).c_str(), kill ? "kill" : "keep bleeding");
    }
    return kill ? kYes : kNo;
}

// RW3 (local death on the host, esi = the game's partner): true when the team is wiped now, when every remote waits to respawn.
uint32_t __cdecl localDeathWait(uintptr_t partner) {
    Player remotes[kMaxRemotes];
    const size_t count = revive_state::remotes(remotes, kMaxRemotes);
    if (count == 0) return revive_state::gameWaiting(partner) ? kYes : kNo;
    size_t waiting = 0;
    for (size_t i = 0; i < count; ++i) waiting += remotes[i].waiting ? 1 : 0;
    const bool wipe = revive_rules::wipeOnLocalDeath(remotes, count);
    if (revive_state::trace()) {
        logger::write("trace: respawn wait: local died, %zu of %zu remotes waiting -> wipe %d", waiting, count, wipe ? 1 : 0);
    }
    return wipe ? kYes : kNo;
}

// RW4 (entry of the notoriety and wait reset 0x8CBD90): the table is cleared with the game's two timers.
void __cdecl respawnWaitReset(GuestFrame*) {
    revive_state::clearWaits();
    if (revive_state::trace()) logger::write("trace: respawn wait: cleared with the notoriety reset");
}

struct Anchor {
    uintptr_t human;
    const char* how;
};

// The human the revive of `revived` takes its pose from: the sender of the revive message (or the turn of a loop) while one is
// scoped, else the nearest remote that is not in a ragdoll, else the default partner as the game has it.
Anchor reviveAnchorOf(uintptr_t revived) {
    if (partner_scope::overrideActive()) return {partner_scope::partnerHuman(), "sender"};
    FpuGuard guard;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    uintptr_t humans[kMaxRemotes];
    partner_rules::Vec3 positions[kMaxRemotes];
    bool upright[kMaxRemotes];
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (!human || human == revived) continue;
        humans[count] = human;
        positions[count] = remote_players::positionOf(human);
        upright[count] = readByte(human + sr2::kHumanRagdollOffset) == 0;
        ++count;
    }
    const size_t nearest = revive_rules::anchorIndex(positions, upright, count, remote_players::positionOf(revived));
    if (nearest == partner_rules::kNone) return {partner_scope::partnerHuman(), "default"};
    return {humans[nearest], "nearest upright"};
}

// RA1, RA2 (esi = the revived human): the null and ragdoll tests of the pose source.
uintptr_t __cdecl reviveAnchor(uintptr_t revived) { return reviveAnchorOf(revived).human; }

// RA3: the same answer, for the pose itself, with its trace line.
uintptr_t __cdecl reviveAnchorPlace(uintptr_t revived) {
    const Anchor anchor = reviveAnchorOf(revived);
    if (revive_state::trace()) {
        logger::write("trace: revive anchor: human 0x%X placed by human 0x%X (%s)", static_cast<unsigned>(revived),
                      static_cast<unsigned>(anchor.human), anchor.how);
    }
    return anchor.human;
}

// The blip the game made for a downed human is released like the game's block does: when the marker still names its record.
void releaseBlip(uintptr_t human) {
    const uint32_t marker = readDword(human + sr2::kHumanBlipOffset);
    const size_t index = revive_rules::markerIndex(marker);
    if (index != partner_rules::kNone) {
        const uintptr_t record = sr2::kBlipTable + index * sr2::kBlipStride;
        if (readDword(record + sr2::kBlipIdOffset) == marker) {
            guest_call::call(sr2::kRemoveBlip, {.ecx = static_cast<uint32_t>(record), .eax = sr2::kBlipReleaseMode});
        }
    }
    dwordField(human, sr2::kHumanBlipOffset) = 0;
}

// RM1: the mission start block that reset the partner's downed state (timer to `reset`, release, blip) runs for every remote.
void __cdecl resetRemotesDowned(int32_t reset) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human || remote_players::downedTimerOf(remote.human) < 0) continue;
        dwordField(remote.human, sr2::kHumanDownedTimerOffset) = static_cast<uint32_t>(reset);
        guest_call::call(sr2::kReleaseDownedHandle, {},
                         {sr2::kReleaseDownedFlag, readDword(remote.human + sr2::kObjectHandleOffset), sr2::kReleaseDownedFlag, 0});
        releaseBlip(remote.human);
        if (revive_state::trace()) {
            logger::write("trace: mission start: remote 0x%X slot %u downed state reset", static_cast<unsigned>(remote.human),
                          static_cast<unsigned>(remote.slot));
        }
    }
}

PARTNER_REG_STUB(remoteDeathWaitStub, esi, remoteDeathWait)
PARTNER_REG_STUB(localDeathWaitStub, esi, localDeathWait)
PARTNER_REG_STUB(reviveAnchorStub, esi, reviveAnchor)
PARTNER_REG_STUB(reviveAnchorPlaceStub, esi, reviveAnchorPlace)
TRACE_ENTRY_STUB(respawnWaitResetStub, respawnWaitReset, g_resetOriginal)

// Replaces the kill of the downed local at the end of the misc_19 handler (esi = the local player): the kill and the notoriety clear
// after it, or the end of the handler while the local keeps bleeding. popad keeps the flags of the test.
void __declspec(naked) downedLocalKillGateStub() {
    __asm {
        pushad
        call downedLocalKill
        test al, al
        popad
        jz keep
        call dword ptr [g_killLocal]
        jmp dword ptr [g_killResume]
    keep:
        jmp dword ptr [g_keepResume]
    }
}

// Replaces the partner block of the mission start with the block for every remote; edi holds the -1 the block stores.
void __declspec(naked) missionStartRemotesDownedStub() {
    __asm {
        pushad
        push edi
        call resetRemotesDowned
        add esp, 4
        popad
        jmp dword ptr [g_missionStartResume]
    }
}

}  // namespace

namespace revive_thunks {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::RemoteDeathWait: return {reinterpret_cast<void*>(&remoteDeathWaitStub), nullptr};
        case Thunk::DownedLocalKillGate: return {reinterpret_cast<void*>(&downedLocalKillGateStub), nullptr};
        case Thunk::LocalDeathWait: return {reinterpret_cast<void*>(&localDeathWaitStub), nullptr};
        case Thunk::RespawnWaitReset: return {reinterpret_cast<void*>(&respawnWaitResetStub), &g_resetOriginal};
        case Thunk::ReviveAnchor: return {reinterpret_cast<void*>(&reviveAnchorStub), nullptr};
        case Thunk::ReviveAnchorPlace: return {reinterpret_cast<void*>(&reviveAnchorPlaceStub), nullptr};
        case Thunk::MissionStartRemotesDowned: return {reinterpret_cast<void*>(&missionStartRemotesDownedStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace revive_thunks
