#include "outfit_sync.h"

#include <span>
#include <string>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "outfit_sync_rules.h"
#include "outfit_sync_sites.h"
#include "outfit_sync_trace.h"
#include "partner_rules.h"
#include "remote_players.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using outfit_sync_rules::Route;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr size_t kOptionArg = 1;
constexpr size_t kVariantArg = 2;
constexpr size_t kBroadcastBufferArg = 0;
constexpr size_t kBroadcastLengthArg = 1;
constexpr size_t kNameLimit = 16;  // "#PLAYER12#" is 10 characters

struct Set {
    const char* name;
    std::span<const Site> sites;
};

constexpr Set kSets[] = {
    {"look sync per remote", outfit_sync_sites::kLookSync},
    {"targeted item wear", outfit_sync_sites::kItemWear},
    {"targeted weapon slots", outfit_sync_sites::kWeaponSlots},
};

struct Target {
    uintptr_t human;
    uintptr_t member;
};

bool g_trace = false;

// The member a call is sending to alone while a TargetHold is alive on this thread (0: the game's broadcast), and the session slot
// of the member the last such send went to.
thread_local uintptr_t t_targetMember = 0;
thread_local uint8_t t_sentSlot = partner_rules::kNoSlot;

class TargetHold {
public:
    explicit TargetHold(uintptr_t member) {
        t_targetMember = member;
        t_sentSlot = partner_rules::kNoSlot;
    }
    ~TargetHold() { t_targetMember = 0; }
    TargetHold(const TargetHold&) = delete;
    TargetHold& operator=(const TargetHold&) = delete;
};

// The text a script names a player human by (its name pointer at +0x18B0), empty without one.
std::string scriptNameOf(uintptr_t human) {
    const uintptr_t text = readDword(human + sr2::kHumanScriptNameOffset);
    char name[kNameLimit + 1] = {};
    if (text) readCString(text, name, sizeof(name));
    return name;
}

// The remote player "#PLAYER<number>#": its human and the member that owns it. False when nobody has that name.
bool resolve(uint32_t number, Target& out) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human || !remote.member || !outfit_sync_rules::namesPlayer(scriptNameOf(remote.human), number)) continue;
        out = {remote.human, remote.member};
        return true;
    }
    return false;
}

std::string sentText() {
    return t_sentSlot == partner_rules::kNoSlot ? "not sent" : "sent to slot " + std::to_string(t_sentSlot) + " only";
}

// OF1: customization_item_wear's remote branch, ecx = the item name, ebx = the mask, args (human, option, variant). A mask without a
// player number makes the game's call; with one the item goes to that remote only.
void __cdecl targetedItemWear(GuestFrame* frame) {
    const uint32_t mask = frame->regs.ebx;
    const uint32_t number = outfit_sync_rules::targetNumber(mask);
    Target target{};
    const Route route = outfit_sync_rules::route(mask, number != 0 && resolve(number, target));
    const uint32_t option = frame->args[kOptionArg];
    const uint32_t variant = frame->args[kVariantArg];
    if (route == Route::Original) {
        frame->regs.eax = guest_call::call(sr2::kItemApply, frame->regs, {frame->args[0], option, variant});
    } else if (route == Route::ToTarget) {
        TargetHold hold(target.member);
        frame->regs.eax = guest_call::call(sr2::kItemApply, frame->regs, {static_cast<uint32_t>(target.human), option, variant});
        if (g_trace) {
            logger::write("trace: outfit sync: item wear mask 0x%X for #PLAYER%u#, human 0x%X, %s", mask, number,
                          static_cast<unsigned>(target.human), sentText().c_str());
        }
    } else if (g_trace) {
        logger::write("trace: outfit sync: item wear mask 0x%X for #PLAYER%u#, no such remote player, nothing sent", mask, number);
    }
}

// OF3: inv_weapon_enable_or_disable_all_slots, cdecl(enable, mask).
void __cdecl targetedWeaponSlots(uint32_t enable, uint32_t mask) {
    const uint32_t number = outfit_sync_rules::targetNumber(mask);
    Target target{};
    const Route route = outfit_sync_rules::route(mask, number != 0 && resolve(number, target));
    const char* state = static_cast<uint8_t>(enable) ? "enable" : "disable";
    if (route == Route::Original) {
        guest_call::call(sr2::kWeaponSlotsApply, {}, {enable, mask});
    } else if (route == Route::ToTarget) {
        TargetHold hold(target.member);
        guest_call::call(sr2::kWeaponSlotsApply, {}, {enable, mask});
        if (g_trace) {
            logger::write("trace: outfit sync: weapon slots %s mask 0x%X for #PLAYER%u#, %s", state, mask, number, sentText().c_str());
        }
    } else {
        guest_call::call(sr2::kWeaponSlotsApply, {}, {enable, outfit_sync_rules::localPart(mask)});
        if (g_trace) {
            logger::write("trace: outfit sync: weapon slots %s mask 0x%X for #PLAYER%u#, no such remote player, local part only", state,
                          mask, number);
        }
    }
}

// OF2 and OF4: the game's broadcast of an item or weapon slot packet, cdecl(buffer, length) with edi = the session. While a call
// holds a target the packet goes to that member only.
void __cdecl sendToSyncTarget(GuestFrame* frame) {
    const uint32_t buffer = frame->args[kBroadcastBufferArg];
    const uint32_t length = frame->args[kBroadcastLengthArg];
    if (t_targetMember) {
        t_sentSlot = readByte(t_targetMember + sr2::kMemberSlotOffset);
        guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(t_targetMember)}, {buffer, length, 0, 0});
    } else {
        frame->regs.eax = guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {buffer, length});
    }
}

PARTNER_WRAP_STUB(targetedItemWearStub, targetedItemWear, 0)
PARTNER_WRAP_STUB(sendToSyncTargetStub, sendToSyncTarget, 0)

partner_thunks::Binding bindingOf(Thunk thunk) {
    for (const auto bind : {&outfit_sync::bind, &outfit_sync_trace::bind, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

void installSet(const Set& set) {
    if (!site_installer::sitesMatch(set.sites)) {
        logger::write("outfit sync: %s, sites differ, skipped", set.name);
        return;
    }
    const bool installed = site_installer::install(set.sites, false, &bindingOf).installed;
    logger::write("outfit sync: %s %s", set.name, installed ? "installed" : "not installed");
}

void installTrace(const Site& site) {
    const std::span<const Site> one(&site, 1);
    if (!site_installer::sitesMatch(one)) {
        logger::write("outfit sync: trace %s, bytes differ, skipped", site.id);
        return;
    }
    const bool installed = site_installer::install(one, false, &bindingOf).installed;
    logger::write("outfit sync: trace %s %s", site.id, installed ? "installed" : "not installed");
}

}  // namespace

namespace outfit_sync {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::TargetedItemWear: return {reinterpret_cast<void*>(&targetedItemWearStub), nullptr};
        case Thunk::TargetedWeaponSlots: return {reinterpret_cast<void*>(&targetedWeaponSlots), nullptr};
        case Thunk::SendToSyncTarget: return {reinterpret_cast<void*>(&sendToSyncTargetStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void install(bool trace) {
    g_trace = trace;
    for (const Set& set : kSets) installSet(set);
    if (!trace) return;
    for (const Site& site : outfit_sync_sites::kTraces) installTrace(site);
}

}  // namespace outfit_sync
