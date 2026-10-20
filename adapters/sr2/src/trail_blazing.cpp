// Trail Blazing for 3 or 4 players: the game's code does for the local player and "the partner" what every remote now gets. Each
// thunk runs the game's own calls once per remote, in the game's order, after the local player's; with one remote the calls and
// their arguments are the shipped ones.
#include "trail_blazing.h"

#include <bit>

#include "diversion_remotes.h"
#include "diversion_stubs.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "site_installer.h"
#include "squared_distance.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "trail_blazing_rules.h"
#include "trail_blazing_sites.h"

namespace {

using partner_rules::Vec3;
using partner_sites::Thunk;

constexpr uint32_t kEffectMode = 1;          // 0x55A930: the third argument the level start passes
constexpr uint32_t kEffectNoFlags = 0;       // 0x55A930: the fourth
constexpr uint32_t kNoWeaponsFlag = 0;       // 0x9DED00: the second argument
constexpr uint32_t kPutOutWaterFlag = 0;     // 0x98CEB0: the argument of the water fail
constexpr uint32_t kTeleportFlagOn = 1;      // 0x9DB480: the two flags the stage start sets
constexpr uint32_t kTeleportFlagOff = 0;     // and the two it clears
constexpr uint8_t kExitRequestBit = 1;       // exit request flags: leave the vehicle
constexpr uint8_t kFailLevelByte = 1;
constexpr size_t kVec3Dwords = 3;

const uintptr_t g_resumeLevelStart = sr2::kResumeTrailLevelStart;
const uintptr_t g_resumeLevelStep = sr2::kResumeTrailLevelStep;
const uintptr_t g_resumeFireNodes = sr2::kResumeTrailFireNodes;
const uintptr_t g_resumeWaterFail = sr2::kResumeTrailWaterFail;
const uintptr_t g_resumeStartPlace = sr2::kResumeTrailStartPlace;
const uintptr_t g_resumeStartExit = sr2::kResumeTrailStartExit;
const uintptr_t g_resumeCheckpoint = sr2::kResumeTrailCheckpoint;

bool g_trace = false;
rate_limit::OncePerKey g_fireSeen;
rate_limit::OncePerKey g_checkpointSeen;

uint32_t addressOf(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

uint64_t keyOf(uintptr_t object, uintptr_t human) { return (static_cast<uint64_t>(object) << 32) | human; }

// The session slot of a remote human, for the trace lines.
unsigned slotOf(uintptr_t human) {
    diversion_remotes::View view;
    diversion_remotes::take(view);
    const size_t at = view.indexOf(human);
    return at == partner_rules::kNone ? partner_rules::kNoSlot : view.items()[at].slot;
}

double readDouble(uintptr_t address) {
    double value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// The game's own "is in a vehicle" test (0x9B8C50 with no particular vehicle).
bool riding(uintptr_t human) {
    return (guest_call::call(sr2::kHumanInVehicle, {.edi = 0, .esi = static_cast<uint32_t>(human)}) & 0xFF) != 0;
}

void startEffect(uintptr_t human) {
    guest_call::call(sr2::kInventoryApply, {},
                     {static_cast<uint32_t>(human + sr2::kHumanInventoryOffset), sr2::kTrailStartEffectId, kEffectMode, kEffectNoFlags});
}

void putOut(uintptr_t human, uint32_t flag) {
    guest_call::call(sr2::kPutOutHuman, {.edi = readDword(human + sr2::kObjectHandleOffset)}, {flag});
}

// TB1: the fire effect of a node stays while the local player or the answer of this call is close; the answer is the remote nearest to the
// node (the vec3 at the call's [esp + 0x10]), the default partner when there is none.
uintptr_t __cdecl nearestToFire(uint32_t point, uintptr_t node) {
    const uintptr_t defaultPartner = partner_scope::partnerHuman();
    const uintptr_t nearest = partner_identity::nearest(point);
    const uintptr_t chosen = nearest ? nearest : defaultPartner;
    if (g_trace && chosen != defaultPartner && g_fireSeen.firstTime(keyOf(node, chosen))) {
        logger::write("trace: trail blazing: fire effect at node 0x%X follows remote 0x%X slot %u", static_cast<unsigned>(node),
                      static_cast<unsigned>(chosen), slotOf(chosen));
    }
    return chosen;
}

// TB2: the level start's first step for every remote: its weapons, then the effect on its component.
void __cdecl levelStartEachRemote(JumpFrame*) {
    partner_scope::eachRemoteHuman([](uintptr_t human) {
        if (!human) return;
        guest_call::call(sr2::kSnatchSetupWeapons, {}, {static_cast<uint32_t>(human), kNoWeaponsFlag});
        startEffect(human);
        if (g_trace) logger::write("trace: trail blazing: level start for remote 0x%X slot %u", static_cast<unsigned>(human), slotOf(human));
    });
}

// TB3: the second step for every remote: 0x9C3B00 when 0x9C3A90 says so, else the effect again.
void __cdecl levelStepEachRemote(JumpFrame*) {
    partner_scope::eachRemoteHuman([](uintptr_t human) {
        if (!human) return;
        const bool step = (guest_call::call(sr2::kTrailStartCheck, {.edx = static_cast<uint32_t>(human)}) & 0xFF) != 0;
        if (step) {
            guest_call::call(sr2::kTrailStartStep, {}, {static_cast<uint32_t>(human)});
        } else {
            startEffect(human);
        }
        if (g_trace) {
            logger::write("trace: trail blazing: level start step for remote 0x%X slot %u: %s", static_cast<unsigned>(human), slotOf(human),
                          step ? "0x9C3B00" : "effect");
        }
    });
}

// TB4, TB5: the partner's put out (edi = its handle, cdecl(flag), the caller pops) becomes a put out of every remote.
void __cdecl putOutEachRemote(GuestFrame* frame) {
    const uint32_t flag = frame->args[0];
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human) return;
        putOut(human, flag);
        if (g_trace) {
            logger::write("trace: trail blazing: remote 0x%X slot %u put out (flag %u, from 0x%X)", static_cast<unsigned>(human), slotOf(human),
                          flag, frame->returnAddress);
        }
    });
}

// TB6: a fire node that is not lit is lit by every remote that rides within the node radius. The igniter stays the local player's.
void __cdecl fireNodesEachRemote(JumpFrame* frame) {
    FpuGuard guard;
    const uintptr_t node = readDword(frame->regs.ebx + sr2::kTrailNodesOffset) + frame->regs.ebp;
    const uint32_t igniter = readDword(*gameSlot(frame, sr2::kTrailFireLocalSlot) + sr2::kObjectHandleOffset);
    const uint32_t radius = readDword(sr2::kTrailNodeRadius);
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human || !riding(human)) return;
        uint32_t point[kVec3Dwords];
        for (size_t i = 0; i < kVec3Dwords; ++i) point[i] = readDword(human + sr2::kObjectPositionOffset + i * sizeof(uint32_t));
        const bool inRange = (guest_call::call(sr2::kTrailNodeInRange, {.ecx = static_cast<uint32_t>(node)}, {point[0], point[1], point[2], radius}) & 0xFF) != 0;
        if (!inRange) return;
        const uint8_t litBefore = readByte(node + sr2::kTrailNodeLitOffset);
        guest_call::call(sr2::kTrailLightNode, {}, {static_cast<uint32_t>(node), igniter});
        if (g_trace && !litBefore && readByte(node + sr2::kTrailNodeLitOffset)) {
            logger::write("trace: trail blazing: fire node 0x%X lit by remote 0x%X slot %u", static_cast<unsigned>(node), static_cast<unsigned>(human),
                          slotOf(human));
        }
    });
}

// TB7: a flammable object (esi = its entry) is lit near every remote that rides.
void __cdecl objectsEachRemote(BlockFrame* frame) {
    FpuGuard guard;
    const uintptr_t entry = frame->regs.esi;
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human || !riding(human)) return;
        const uint8_t litBefore = readByte(entry + sr2::kTrailObjectLitOffset);
        guest_call::call(sr2::kTrailLightObjectNear, {.edi = static_cast<uint32_t>(human), .esi = static_cast<uint32_t>(entry)});
        if (g_trace && !litBefore && readByte(entry + sr2::kTrailObjectLitOffset)) {
            logger::write("trace: trail blazing: object 0x%X lit near remote 0x%X slot %u", static_cast<unsigned>(entry), static_cast<unsigned>(human),
                          slotOf(human));
        }
    });
}

// TB8: while the course runs, a remote in water is put out and fails the level for everyone (the byte at [esp + 0x13]).
void __cdecl waterFailEachRemote(JumpFrame* frame) {
    if (readDword(frame->regs.ebp + sr2::kTrailLevelStateOffset) != sr2::kTrailLevelRunning) return;
    uint8_t* failByte = reinterpret_cast<uint8_t*>(gameSlot(frame, sr2::kTrailFailByteSlot));
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human) return;
        const auto swimming = static_cast<int32_t>(readDword(human + sr2::kHumanSwimmingOffset));
        const float depth = readFloat(human + sr2::kHumanSubmergedDepthOffset);
        if (!trail_blazing_rules::inWater(swimming, readByte(human + sr2::kHumanSubmergedFlagsOffset), depth)) return;
        putOut(human, kPutOutWaterFlag);
        *failByte = kFailLevelByte;
        if (g_trace) {
            logger::write("trace: trail blazing: remote 0x%X slot %u in water (swimming %d, depth %.2f), the level fails", static_cast<unsigned>(human),
                          slotOf(human), swimming, static_cast<double>(depth));
        }
    });
}

void placeAt(uintptr_t human, uint32_t position, uint32_t orientation) {
    guest_call::call(sr2::kTeleportHuman, {.ecx = static_cast<uint32_t>(human)},
                     {position, orientation, kTeleportFlagOn, kTeleportFlagOn, kTeleportFlagOff, kTeleportFlagOff});
}

// TB9: every remote is warped to the spot the game computed for the default partner ([esp + 0x34]). The spread of players on one
// spot is MC5's (spread_rules, by guest rank), so the spot is not offset here as well.
void __cdecl startPlaceEachRemote(JumpFrame* frame) {
    FpuGuard guard;
    const uintptr_t record = readDword(frame->regs.ebx + sr2::kTrailStartRecordOffset);
    const uint32_t orientation = static_cast<uint32_t>(record + sr2::kTrailStartRightOffset);
    const uint32_t gameSpot = addressOf(gameSlot(frame, sr2::kTrailStartPartnerSlot));
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable()) {
        if (frame->regs.eax) placeAt(frame->regs.eax, gameSpot, orientation);
        return;
    }
    const Vec3 spot = diversion_remotes::vec3At(gameSpot);
    for (size_t i = 0; i < view.count(); ++i) {
        const uintptr_t human = view.items()[i].human;
        if (!human) continue;
        placeAt(human, gameSpot, orientation);
        if (g_trace) {
            logger::write("trace: trail blazing: stage start, remote 0x%X slot %u placed at %.1f %.1f %.1f", static_cast<unsigned>(human),
                          slotOf(human), static_cast<double>(spot.x), static_cast<double>(spot.y), static_cast<double>(spot.z));
        }
    }
}

// TB10: every remote that rides is asked to leave its vehicle with the request the game built for the local player and the partner.
void __cdecl startExitEachRemote(JumpFrame* frame) {
    const uintptr_t request = reinterpret_cast<uintptr_t>(gameSlot(frame, sr2::kTrailExitRequestSlot));
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (!human || !riding(human)) return;
        guest_call::call(sr2::kExitRequestInit, {.eax = static_cast<uint32_t>(request)});
        byteField(request, sr2::kExitRequestFlagsOffset) |= kExitRequestBit;
        guest_call::call(sr2::kRequestVehicleExit, {}, {static_cast<uint32_t>(human), static_cast<uint32_t>(request)});
        if (g_trace) {
            logger::write("trace: trail blazing: stage start, remote 0x%X slot %u asked to leave its vehicle", static_cast<unsigned>(human),
                          slotOf(human));
        }
    });
}

// TB11: the checkpoint distance the game keeps at [esp + 8] (the local player's) becomes the smallest over the default partner and every
// remote that rides. The game's height test and its "everyone in a vehicle" test run unchanged afterwards.
void __cdecl checkpointDistance(JumpFrame* frame) {
    FpuGuard guard;
    uint32_t* slot = gameSlot(frame, sr2::kTrailCheckpointDistanceSlot);
    const uint32_t target = *gameSlot(frame, sr2::kTrailCheckpointTargetSlot);
    float distance = std::bit_cast<float>(*slot);
    const auto distanceOf = [target](uintptr_t human) {
        return squared_distance::between(sr2::kSquaredDistanceTeam, static_cast<uint32_t>(human + sr2::kObjectPositionOffset), target);
    };
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable()) {
        distance = trail_blazing_rules::closer(distance, distanceOf(*gameSlot(frame, sr2::kTrailCheckpointPartnerSlot)));
    } else {
        const double reach = readDouble(sr2::kTrailCheckpointReach);
        for (size_t i = 0; i < view.count(); ++i) {
            const uintptr_t human = view.items()[i].human;
            const bool isDefault = i == view.defaultIndex;
            if (!human || !trail_blazing_rules::countsAtCheckpoint(isDefault, riding(human))) continue;
            const float candidate = distanceOf(human);
            const float next = trail_blazing_rules::closer(distance, candidate);
            const uintptr_t record = target - sr2::kTrailCheckpointPositionOffset;
            if (g_trace && !isDefault && next == candidate && static_cast<double>(candidate) < reach && g_checkpointSeen.firstTime(keyOf(record, human))) {
                logger::write("trace: trail blazing: checkpoint 0x%X nearest is remote 0x%X slot %u (d2 %.1f)", static_cast<unsigned>(record),
                              static_cast<unsigned>(human), view.items()[i].slot, static_cast<double>(candidate));
            }
            distance = next;
        }
    }
    *slot = std::bit_cast<uint32_t>(distance);
}

// Replaces `call 0x9D7BD0` of the fire effect: the point is at [esp + 0x10] of the code that made the call, the node is in edi.
void __declspec(naked) nearestToFireStub() {
    __asm {
        lea eax, [esp + 0x14]
        push edi
        push eax
        call nearestToFire
        add esp, 8
        ret
    }
}

DIVERSION_JUMP_STUB(levelStartStub, levelStartEachRemote, g_resumeLevelStart)
DIVERSION_JUMP_STUB(levelStepStub, levelStepEachRemote, g_resumeLevelStep)
PARTNER_WRAP_STUB(putOutStub, putOutEachRemote, 0)
DIVERSION_JUMP_STUB(fireNodesStub, fireNodesEachRemote, g_resumeFireNodes)
DIVERSION_BLOCK_STUB(objectsStub, objectsEachRemote)
DIVERSION_JUMP_STUB(waterFailStub, waterFailEachRemote, g_resumeWaterFail)
DIVERSION_JUMP_STUB(startPlaceStub, startPlaceEachRemote, g_resumeStartPlace)
DIVERSION_JUMP_STUB(startExitStub, startExitEachRemote, g_resumeStartExit)
DIVERSION_JUMP_STUB(checkpointStub, checkpointDistance, g_resumeCheckpoint)

}  // namespace

namespace trail_blazing {

partner_thunks::Binding bindingOf(Thunk thunk) {
    switch (thunk) {
        case Thunk::TrailNearestToFire: return {reinterpret_cast<void*>(&nearestToFireStub), nullptr};
        case Thunk::TrailLevelStartEachRemote: return {reinterpret_cast<void*>(&levelStartStub), nullptr};
        case Thunk::TrailLevelStepEachRemote: return {reinterpret_cast<void*>(&levelStepStub), nullptr};
        case Thunk::TrailPutOutEachRemote: return {reinterpret_cast<void*>(&putOutStub), nullptr};
        case Thunk::TrailFireNodesEachRemote: return {reinterpret_cast<void*>(&fireNodesStub), nullptr};
        case Thunk::TrailObjectsEachRemote: return {reinterpret_cast<void*>(&objectsStub), nullptr};
        case Thunk::TrailWaterFailEachRemote: return {reinterpret_cast<void*>(&waterFailStub), nullptr};
        case Thunk::TrailStartPlaceEachRemote: return {reinterpret_cast<void*>(&startPlaceStub), nullptr};
        case Thunk::TrailStartExitEachRemote: return {reinterpret_cast<void*>(&startExitStub), nullptr};
        case Thunk::TrailCheckpointDistance: return {reinterpret_cast<void*>(&checkpointStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void install(bool trace) {
    g_trace = trace;
    for (const trail_blazing_sites::Set& set : trail_blazing_sites::kSets) {
        const uintptr_t differs = site_installer::firstDifference(set.sites);
        if (differs) {
            logger::write("trail blazing: set %s differs at 0x%X, not installed", set.name, static_cast<unsigned>(differs));
            continue;
        }
        logger::write("trail blazing: set %s matches (%zu rows)", set.name, set.sites.size());
        const site_installer::Result result = site_installer::install(set.sites, &bindingOf);
        if (result.installed) {
            logger::write("trail blazing: set %s patched (%zu rows)", set.name, result.patched);
        } else {
            logger::write("trail blazing: set %s not patched", set.name);
        }
    }
}

}  // namespace trail_blazing
