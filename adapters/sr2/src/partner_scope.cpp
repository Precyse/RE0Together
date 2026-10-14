#include "partner_scope.h"

#include <atomic>

#include "fpu_guard.h"
#include "log.h"
#include "member_slots.h"

namespace {

using GetterFn = uintptr_t(__cdecl*)();

thread_local bool t_active = false;
thread_local partner_scope::Target t_target{0, 0};

GetterFn g_originalHuman = nullptr;
GetterFn g_originalMember = nullptr;

bool g_trace = false;
uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;
std::atomic<uint64_t> g_lastSignature{0};
std::atomic<uintptr_t> g_lastPartner{0};

void trace(const remote_players::Snapshot& snapshot, const remote_players::Partner& partner) {
    const uint64_t signature = remote_players::signatureOf(snapshot);
    const bool partnerChanged = g_lastPartner.exchange(partner.human) != partner.human;
    const bool stateChanged = g_lastSignature.exchange(signature) != signature;
    if (!partnerChanged && !stateChanged) return;
    FpuGuard guard;
    if (partnerChanged) {
        logger::write("trace: default partner now human 0x%X member 0x%X%s", static_cast<unsigned>(partner.human),
                      static_cast<unsigned>(partner.member), partner.trusted ? "" : " (the game's own answer)");
    }
    if (stateChanged) member_slots::check(g_maxPlayers);
}

remote_players::Partner currentPartner() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const remote_players::Partner partner = remote_players::defaultPartner(snapshot);
    if (g_trace) trace(snapshot, partner);
    return partner;
}

uintptr_t __cdecl humanDetour() {
    if (t_active) return t_target.human;
    const remote_players::Partner partner = currentPartner();
    return partner.trusted ? partner.human : g_originalHuman();
}

uintptr_t __cdecl memberDetour() {
    if (t_active) return t_target.member;
    const remote_players::Partner partner = currentPartner();
    return partner.trusted && partner.member ? partner.member : g_originalMember();
}

}  // namespace

namespace partner_scope {

Override::Override(Target target) : previousActive_(t_active), previous_(t_target) {
    t_target = target;
    t_active = true;
}

Override::~Override() {
    t_target = previous_;
    t_active = previousActive_;
}

bool overrideActive() { return t_active; }

uintptr_t partnerHuman() { return reinterpret_cast<GetterFn>(sr2::kRemotePlayerGetter)(); }

uintptr_t partnerMember() { return reinterpret_cast<GetterFn>(sr2::kPartnerMemberGetter)(); }

GetterHook humanHook() { return {reinterpret_cast<void*>(&humanDetour), reinterpret_cast<void**>(&g_originalHuman)}; }

GetterHook memberHook() { return {reinterpret_cast<void*>(&memberDetour), reinterpret_cast<void**>(&g_originalMember)}; }

void enableTrace(uint32_t maxPlayers) {
    g_maxPlayers = maxPlayers;
    g_trace = true;
}

}  // namespace partner_scope
