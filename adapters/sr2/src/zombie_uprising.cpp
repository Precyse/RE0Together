#include "zombie_uprising.h"

#include <bit>
#include <span>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "mission_flow.h"
#include "partner_rules.h"
#include "player_visuals_rules.h"
#include "remote_players.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "store_sync.h"
#include "thunk_frame.h"
#include "zombie_uprising_rules.h"
#include "zombie_uprising_sites.h"

namespace {

using partner_rules::Remote;
using partner_rules::Vec3;
using partner_sites::Thunk;
namespace rules = zombie_uprising_rules;

constexpr size_t kMaxRemotes = sr2::kMaxWalkedPlayers;

// Recruit modes of the worker at sr2::kRecruitSetMode: none during the waves, the friendly gang again after them.
constexpr uint32_t kRecruitNone = 0;
constexpr uint32_t kRecruitFriendlyGang = 1;
constexpr uint32_t kFlagOn = 1;
constexpr uint32_t kFlagOff = 0;
// The trailing arguments of the shipped start block's teleport, after the position and the orientation.
constexpr uint32_t kTeleportFlagA = 1;
constexpr uint32_t kTeleportFlagB = 1;
constexpr uint32_t kTeleportFlagC = 0;
constexpr uint32_t kTeleportFlagD = 0;

// Where the wave bonus keeps the local player's health fraction: the game's [esp+0x10], seen from the frame of the block's call.
constexpr size_t kLocalFractionArg = 4;

// The spawn picker's stack, seen from inside a stub called from its loops (the return address is on top): the loop index and the
// array of the local player and the first remote.
enum : int { kStubReturnBytes = 4, kSavedRegisterBytes = 4, kLoopIndexSlot = 0x18, kPlayerPairSlot = 0x48 };

bool g_trace = false;
rules::SpawnPlayers g_spawnExtras{};

uint32_t addressArg(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

Vec3 vec3At(uintptr_t address) {
    Vec3 value{};
    readMemory(address, &value, sizeof(value));
    return value;
}

// ZU1: the wave start's four calls for every remote human, in the shipped order, all at the shipped partner spot (MC5 spreads them).
void __cdecl startEachRemote(GuestFrame* frame) {
    FpuGuard guard;
    const uintptr_t marker = dwordField(frame->regs.esi, sr2::kZombieStartMarkerOffset);
    const Vec3 markerPosition = vec3At(marker + sr2::kMarkerPositionOffset);
    const uint32_t orientation = addressArg(reinterpret_cast<const void*>(marker + sr2::kMarkerOrientationOffset));
    double partnerOffset = 0;
    readMemory(sr2::kZombiePartnerStartOffset, &partnerOffset, sizeof(partnerOffset));
    Remote remotes[kMaxRemotes];
    const size_t count = remote_players::remotesWithHuman(remotes, kMaxRemotes);
    const Vec3 spot = rules::startSpot(markerPosition, static_cast<float>(partnerOffset));
    for (size_t i = 0; i < count; ++i) {
        const uint32_t human = static_cast<uint32_t>(remotes[i].human);
        guest_call::call(sr2::kRecruitSetMode, {.ecx = human}, {kRecruitNone});
        guest_call::call(sr2::kCharacterDontRegenerate, {.edi = human}, {kFlagOn});
        guest_call::call(sr2::kEvacuateFromVehicles, {.edi = human}, {kFlagOn});
        guest_call::call(sr2::kTeleportHuman, {.ecx = human},
                         {addressArg(&spot), orientation, kTeleportFlagA, kTeleportFlagB, kTeleportFlagC, kTeleportFlagD});
        if (g_trace) {
            logger::write("trace: zombie: start, slot %u human 0x%X at %.3f %.3f %.3f: no followers, no regeneration, out of vehicles",
                          static_cast<unsigned>(remotes[i].slot), static_cast<unsigned>(human), static_cast<double>(spot.x), static_cast<double>(spot.y),
                          static_cast<double>(spot.z));
        }
    }
}

// ZU2: the wave end gives every remote human back its regeneration and its followers.
void __cdecl endEachRemote(GuestFrame*) {
    Remote remotes[kMaxRemotes];
    const size_t count = remote_players::remotesWithHuman(remotes, kMaxRemotes);
    uintptr_t humans[kMaxRemotes];
    for (size_t i = 0; i < count; ++i) {
        humans[i] = remotes[i].human;
        guest_call::call(sr2::kCharacterDontRegenerate, {.edi = static_cast<uint32_t>(humans[i])}, {kFlagOff});
        guest_call::call(sr2::kRecruitSetMode, {.ecx = static_cast<uint32_t>(humans[i])}, {kRecruitFriendlyGang});
    }
    if (g_trace && count > 0) {
        logger::write("trace: zombie: end, followers and regeneration restored for humans %s", player_visuals_rules::humansText(humans, count).c_str());
    }
}

// ZU3: the wave bonus takes the mean health fraction of the local player and every remote human. The game's local fraction is rewritten
// in its frame and the game goes on with the load of it.
void __cdecl waveHealthAverage(GuestFrame* frame) {
    FpuGuard guard;
    Remote remotes[kMaxRemotes];
    const size_t count = remote_players::remotesWithHuman(remotes, kMaxRemotes);
    float fractions[kMaxRemotes];
    for (size_t i = 0; i < count; ++i) {
        fractions[i] = rules::healthFraction(readFloat(remotes[i].human + sr2::kHumanHealthOffset),
                                             static_cast<int32_t>(readDword(remotes[i].human + sr2::kHumanMaxHealthOffset)));
    }
    const float local = std::bit_cast<float>(frame->args[kLocalFractionArg]);
    const float average = rules::averageHealth(local, std::span<const float>(fractions, count));
    frame->args[kLocalFractionArg] = std::bit_cast<uint32_t>(average);
    if (g_trace) {
        logger::write("trace: zombie: wave health average %.3f of %zu players (local %.3f, remotes %s)", static_cast<double>(average), count + 1,
                      static_cast<double>(local), rules::fractionsText(std::span<const float>(fractions, count)).c_str());
    }
}

// ZS1: the picker's second player is the first remote human, as the game's inline walk found it; the others wait in `g_spawnExtras`. Outside
// co-op there is nobody.
void __cdecl spawnPlayers(GuestFrame* frame) {
    if (readByte(sr2::kCoopFlag) == 0) {
        frame->regs.eax = 0;
        g_spawnExtras = {};
        return;
    }
    uintptr_t walk[kMaxRemotes];
    const size_t count = remote_players::remoteHumans(walk, kMaxRemotes);
    frame->regs.eax = count > 0 ? static_cast<uint32_t>(walk[0]) : 0;
    g_spawnExtras = rules::spawnExtrasOf(walk, count);
    if (g_trace) logger::writeUnlessRepeated("trace: zombie: spawn view and distance tests cover the local player and %zu remotes", count);
}

// ZS2, ZS4: player `index` of the picker's loops. No float work: the x87 stack holds live values of the picker here.
uint32_t __cdecl spawnPlayerAt(const uint32_t* pair, uint32_t index) { return rules::spawnPlayerAt(pair, g_spawnExtras, index); }

PARTNER_WRAP_STUB(startEachRemoteStub, startEachRemote, 0)
PARTNER_WRAP_STUB(endEachRemoteStub, endEachRemote, 0)
PARTNER_WRAP_STUB(waveHealthAverageStub, waveHealthAverage, 0)
PARTNER_WRAP_STUB(spawnPlayersStub, spawnPlayers, 0)

// ZS2 replaces `mov edx, [esp+0x18]; mov eax, [esp+edx*4+0x48]` of the view loop: eax is the player, edx the index. ecx is dead.
void __declspec(naked) spawnPlayerAtEaxStub() {
    __asm {
        mov edx, [esp + kStubReturnBytes + kLoopIndexSlot]
        lea eax, [esp + kStubReturnBytes + kPlayerPairSlot]
        push edx
        push eax
        call spawnPlayerAt
        add esp, 8
        mov edx, [esp + kStubReturnBytes + kLoopIndexSlot]
        ret
    }
}

// ZS4 replaces `mov ecx, [esp+edx*4+0x48]; cmp ecx, ebx` of the distance loop: ecx is the player, edx the index, and the flags are the
// compare's. eax is dead.
void __declspec(naked) spawnPlayerAtEcxStub() {
    __asm {
        push edx
        lea eax, [esp + kStubReturnBytes + kSavedRegisterBytes + kPlayerPairSlot]
        push edx
        push eax
        call spawnPlayerAt
        add esp, 8
        pop edx
        mov ecx, eax
        cmp ecx, ebx
        ret
    }
}

}  // namespace

namespace zombie_uprising {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::ZombieStartEachRemote: return {reinterpret_cast<void*>(&startEachRemoteStub), nullptr};
        case Thunk::ZombieEndEachRemote: return {reinterpret_cast<void*>(&endEachRemoteStub), nullptr};
        case Thunk::ZombieWaveHealthAverage: return {reinterpret_cast<void*>(&waveHealthAverageStub), nullptr};
        case Thunk::ZombieSpawnPlayers: return {reinterpret_cast<void*>(&spawnPlayersStub), nullptr};
        case Thunk::ZombieSpawnPlayerAtEax: return {reinterpret_cast<void*>(&spawnPlayerAtEaxStub), nullptr};
        case Thunk::ZombieSpawnPlayerAtEcx: return {reinterpret_cast<void*>(&spawnPlayerAtEcxStub), nullptr};
        default: break;
    }
    const partner_thunks::Binding boxTest = store_sync::bindingOf(thunk);
    return boxTest.entry ? boxTest : mission_flow::bind(thunk);
}

void install(bool trace) {
    g_trace = trace;
    for (const zombie_uprising_sites::Set& set : zombie_uprising_sites::kSets) {
        if (!site_installer::sitesMatch(set.sites)) {
            logger::write("zombie: %s not patched, the bytes differ", set.name);
            continue;
        }
        const site_installer::Result result = site_installer::install(set.sites, &bind);
        if (result.installed) {
            logger::write("zombie: %s, %zu sites patched", set.name, result.patched);
        } else {
            logger::write("zombie: %s not patched", set.name);
        }
    }
}

}  // namespace zombie_uprising
