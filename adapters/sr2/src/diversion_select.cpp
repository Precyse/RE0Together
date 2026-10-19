// Selection thunks of the diversions: a call of the partner getter becomes the remote that the test around it is about (a
// remote in the towed car, in the fraud zone, in the district, nearest to the ho). One remote gives the shipped answer.
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_thunks.h"
#include "diversion_trace.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

rate_limit::OncePerKey g_towSeen;
rate_limit::OncePerKey g_dealerSeen;
rate_limit::OncePerKey g_districtSeen;
rate_limit::RateLimit g_nearestHoLimit;
rate_limit::RateLimit g_destructionLimit;
rate_limit::RateLimit g_ownerLimit;

// The first remote human that satisfies `test(human)`, the default partner first; the default partner when none does. Under
// an override (a sender scope or an each-remote turn) the override's remote.
template <typename Test>
uintptr_t anyRemote(Test test) {
    if (partner_scope::overrideActive()) return partner_scope::partnerHuman();
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable()) return partner_scope::partnerHuman();
    const size_t at = diversion_rules::firstMatching(view.items(), view.count(), view.defaultIndex,
                                                     [&](const partner_rules::Remote& remote) { return test(remote.human); });
    return at < view.count() ? view.items()[at].human : partner_scope::partnerHuman();
}

uint32_t districtOf(uintptr_t human) { return readDword(human + sr2::kHumanDistrictOffset); }

// DVW1: the towed car stays valid while any remote sits in it.
uintptr_t __cdecl anyRemoteInVehicleOf(uintptr_t vehicle) {
    bool inside = false;
    const uintptr_t human = anyRemote([&](uintptr_t candidate) {
        inside = (guest_call::call(sr2::kVehicleHolds, {.ecx = vehicle, .eax = readDword(candidate + sr2::kObjectHandleOffset)}) & 0xFF) != 0;
        return inside;
    });
    if (inside && diversion_trace::firstTime(g_towSeen, vehicle)) {
        logger::write("trace: tow: remote 0x%X inside the towed vehicle 0x%X", static_cast<unsigned>(human), static_cast<unsigned>(vehicle));
    }
    return human;
}

// DVF4: the point is in the fraud zone at `zone` (ebp).
uintptr_t __cdecl anyRemoteInFraudZoneOf(uintptr_t zone) {
    return anyRemote([&](uintptr_t human) {
        return (guest_call::call(sr2::kFraudInZone, {.edx = zone, .ecx = human + sr2::kObjectPositionOffset}) & 0xFF) != 0;
    });
}

// DVF5: the remote stands in the fraud area.
uintptr_t __cdecl anyRemoteInFraudArea() {
    return anyRemote([](uintptr_t human) {
        return (guest_call::call(sr2::kFraudInArea, {.edx = human + sr2::kObjectPositionOffset}) & 0xFF) != 0;
    });
}

// DVF7: the remote has a fraud zone.
uintptr_t __cdecl anyRemoteWithFraudZone() {
    return anyRemote([](uintptr_t human) { return guest_call::call(sr2::kFraudZoneOfHuman, {}, {static_cast<uint32_t>(human)}) != 0; });
}

// DVF14, DVF15: the remote that owns the ragdoll (the object in esi), else the default partner.
uintptr_t __cdecl ownerRemote(uintptr_t object) {
    if (remote_players::isRemotePlayerHuman(object)) return object;
    const uintptr_t record = object ? readDword(object + sr2::kObjectNetRecordOffset) : 0;
    if (record) {
        const uint8_t slot = readByte(record);
        diversion_remotes::View view;
        diversion_remotes::take(view);
        for (size_t i = 0; view.usable() && i < view.count(); ++i) {
            if (view.items()[i].human && view.items()[i].slot == slot) {
                if (diversion_trace::dueNow(g_ownerLimit)) {
                    logger::write("trace: fraud: owner of 0x%X is human 0x%X", static_cast<unsigned>(object), static_cast<unsigned>(view.items()[i].human));
                }
                return view.items()[i].human;
            }
        }
    }
    return partner_scope::partnerHuman();
}

// The remote nearest to the vec3 inside `object` at `pointOffset`, else the default partner (no object, or no remote).
uintptr_t nearestToPointOf(uintptr_t object, uintptr_t pointOffset) {
    if (partner_scope::overrideActive() || !object) return partner_scope::partnerHuman();
    const uintptr_t nearest = partner_identity::nearest(static_cast<uint32_t>(object + pointOffset));
    return nearest ? nearest : partner_scope::partnerHuman();
}

// DVP2: the partner keeps the ho's car by being near the object at instance +0x8C (esi is the instance).
uintptr_t __cdecl nearestToHoCar(uintptr_t instance) {
    return nearestToPointOf(readDword(instance + sr2::kSnatchPartnerTargetOffset), sr2::kObjectPositionOffset);
}

// DVP6: nearest to the ho's home (ebx is the level).
uintptr_t __cdecl nearestToHoHome(uintptr_t level) {
    const uintptr_t instance = readDword(level + sr2::kLevelInstanceOffset);
    const uintptr_t nearest = nearestToPointOf(readDword(instance + sr2::kSnatchHoHomeOffset), sr2::kSnatchGroupPointOffset);
    if (diversion_trace::dueNow(g_nearestHoLimit)) {
        logger::write("trace: snatch: nearest remote 0x%X to home 0x%X", static_cast<unsigned>(nearest), static_cast<unsigned>(instance));
    }
    return nearest;
}

// DVP4a: the remote that is in a vehicle.
uintptr_t __cdecl anyRemoteInVehicle() {
    return anyRemote([](uintptr_t human) { return diversion_remotes::vehicleOf(human) != 0; });
}

// DVK1: the remote the dealer follows (esi of the level's instance holds the object that knows the dealer).
uintptr_t __cdecl remoteLeadingDealer(uintptr_t level) {
    const uintptr_t instance = readDword(level + sr2::kLevelInstanceOffset);
    const uintptr_t source = instance ? readDword(instance + sr2::kTraffickingDealerSource) : 0;
    const uintptr_t vtable = source ? readDword(source) : 0;
    uint32_t leader = 0;
    if (vtable && (guest_call::call(readDword(vtable + sr2::kDealerSourceHasSlot), {.ecx = source}) & 0xFF) != 0) {
        const uintptr_t dealer = guest_call::call(readDword(vtable + sr2::kDealerSourceGetSlot), {.ecx = source});
        leader = dealer ? readDword(dealer + sr2::kDealerLeaderOffset) : 0;
    }
    bool follows = false;
    const uintptr_t human = anyRemote([&](uintptr_t candidate) {
        follows = leader != 0 && readDword(candidate + sr2::kObjectHandleOffset) == leader;
        return follows;
    });
    if (follows && diversion_trace::firstTime(g_dealerSeen, human)) {
        logger::write("trace: trafficking: dealer follows remote 0x%X", static_cast<unsigned>(human));
    }
    return human;
}

// DVM4: the destroyer (the holder's field at +0x30) is any remote: its destruction counts in the remote side's score block.
uintptr_t __cdecl matchDestroyerOf(uintptr_t holder) {
    const uintptr_t destroyer = readDword(holder + sr2::kMayhemDestroyerOffset);
    const uintptr_t matched = partner_identity::match(destroyer);
    if (matched == destroyer && diversion_trace::dueNow(g_destructionLimit)) {
        logger::write("trace: mayhem: destruction by remote 0x%X counted in block 1", static_cast<unsigned>(destroyer));
    }
    return matched;
}

// DVO1 to DVO5: the remote that stands in the district record `district`.
uintptr_t remoteInDistrict(uint32_t district) {
    uint32_t seen = 0;
    const uintptr_t human = anyRemote([&](uintptr_t candidate) {
        seen = districtOf(candidate);
        return seen == district;
    });
    if (seen == district && diversion_trace::firstTime(g_districtSeen, human ^ (static_cast<uint64_t>(district) << 32))) {
        logger::write("trace: district objective: remote 0x%X in district 0x%X", static_cast<unsigned>(human), district);
    }
    return human;
}

uintptr_t __cdecl remoteInDistrictOf(uint32_t district) { return remoteInDistrict(district); }
uintptr_t __cdecl remoteInDistrictAt(uintptr_t address) { return remoteInDistrict(readDword(address)); }
uintptr_t __cdecl remoteInDistrictObjective() { return remoteInDistrict(readDword(sr2::kDistrictObjectiveTarget)); }

PARTNER_REG_STUB(anyRemoteInVehicleEsiStub, esi, anyRemoteInVehicleOf)
PARTNER_REG_STUB(anyRemoteInFraudZoneEbpStub, ebp, anyRemoteInFraudZoneOf)
PARTNER_REG_STUB(ownerRemoteEsiStub, esi, ownerRemote)
PARTNER_REG_STUB(nearestToHoCarEsiStub, esi, nearestToHoCar)
PARTNER_REG_STUB(nearestToHoHomeEbxStub, ebx, nearestToHoHome)
PARTNER_REG_STUB(remoteLeadingDealerEbxStub, ebx, remoteLeadingDealer)
PARTNER_REG_STUB(matchDestroyerEdiStub, edi, matchDestroyerOf)
PARTNER_REG_STUB(remoteInDistrictEdiStub, edi, remoteInDistrictOf)
PARTNER_REG_STUB(remoteInDistrictAtEsiStub, esi, remoteInDistrictAt)
PARTNER_REG_STUB(remoteInDistrictAtEbpStub, ebp, remoteInDistrictAt)

}  // namespace

namespace diversion_thunks {

partner_thunks::Binding bindSelect(Thunk thunk) {
    switch (thunk) {
        case Thunk::AnyRemoteInVehicleEsi: return {reinterpret_cast<void*>(&anyRemoteInVehicleEsiStub), nullptr};
        case Thunk::AnyRemoteInFraudZoneEbp: return {reinterpret_cast<void*>(&anyRemoteInFraudZoneEbpStub), nullptr};
        case Thunk::AnyRemoteInFraudArea: return {reinterpret_cast<void*>(&anyRemoteInFraudArea), nullptr};
        case Thunk::AnyRemoteWithFraudZone: return {reinterpret_cast<void*>(&anyRemoteWithFraudZone), nullptr};
        case Thunk::OwnerRemoteEsi: return {reinterpret_cast<void*>(&ownerRemoteEsiStub), nullptr};
        case Thunk::NearestRemoteToEsi8C: return {reinterpret_cast<void*>(&nearestToHoCarEsiStub), nullptr};
        case Thunk::AnyRemoteInVehicle: return {reinterpret_cast<void*>(&anyRemoteInVehicle), nullptr};
        case Thunk::NearestRemoteToHoHome: return {reinterpret_cast<void*>(&nearestToHoHomeEbxStub), nullptr};
        case Thunk::RemoteLeadingDealer: return {reinterpret_cast<void*>(&remoteLeadingDealerEbxStub), nullptr};
        case Thunk::MatchRemoteEdi30: return {reinterpret_cast<void*>(&matchDestroyerEdiStub), nullptr};
        case Thunk::RemoteInZoneEdi: return {reinterpret_cast<void*>(&remoteInDistrictEdiStub), nullptr};
        case Thunk::RemoteInZoneAtEsi: return {reinterpret_cast<void*>(&remoteInDistrictAtEsiStub), nullptr};
        case Thunk::RemoteInZoneAtEbp: return {reinterpret_cast<void*>(&remoteInDistrictAtEbpStub), nullptr};
        case Thunk::RemoteInZoneE9E3F4: return {reinterpret_cast<void*>(&remoteInDistrictObjective), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace diversion_thunks
