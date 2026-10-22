#include "peer_stall.h"

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "peer_stall_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;
using peer_stall_rules::Change;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;

void* g_rateReportOriginal = nullptr;
peer_stall_rules::Watch g_watch;  // the game thread only: the session tick runs there

// The cutscene's own hide and show of a remote human: local for a player human (0x926860 sends nothing for one).
void setHidden(uintptr_t human, bool hidden) {
    guest_call::call(sr2::kHideHuman, {.ecx = static_cast<uint32_t>(human)}, {hidden ? 1u : 0u, 1, 0});
}

bool isHidden(uintptr_t human) { return (readDword(human + sr2::kHumanVisibilityFlagsOffset) & sr2::kHumanHiddenBit) != 0; }

void apply(const partner_rules::Remote& remote, const peer_stall_rules::Step& step) {
    switch (step.change) {
        case Change::Hide:
            setHidden(remote.human, true);
            logger::write("stall: slot %u sent nothing for %u ms, its human 0x%X is hidden", remote.slot, step.longestMs,
                          static_cast<unsigned>(remote.human));
            return;
        case Change::Show:
            // A cutscene keeps every remote hidden and shows them at its end (PV2).
            if (readDword(sr2::kCutscene) == 0) setHidden(remote.human, false);
            logger::write("stall: slot %u sends again after %u ms, its human 0x%X is shown", remote.slot, step.longestMs,
                          static_cast<unsigned>(remote.human));
            return;
        case Change::None:
            // A cutscene end shows every remote (PV2), a stalled one too: it is hidden again.
            if (g_watch.held({remote.member, remote.slot}) && !isHidden(remote.human) && readDword(sr2::kCutscene) == 0) {
                setHidden(remote.human, true);
            }
            return;
    }
}

// PS2: the rate report (0x17) to every member, as the game sends it, but only with a measurement.
void __cdecl reportRate() {
    if (!peer_stall_rules::reportsRate(guest_call::call(sr2::kReceivedRate, {}))) {
        logger::write("stall: no datagram measured in the last 10 s, the rate report is not sent");
        return;
    }
    reinterpret_cast<void(__cdecl*)()>(g_rateReportOriginal)();
}

}  // namespace

namespace peer_stall {

// Every remote with a human is checked against the time of its last datagram.
void tick() {
    partner_rules::Remote remotes[kMaxRemotes];
    const size_t count = remote_players::remotesWithHuman(remotes, kMaxRemotes);
    g_watch.keepOnly([&](peer_stall_rules::Key key) {
        for (size_t i = 0; i < count; ++i) {
            if (remotes[i].member == key.member && remotes[i].slot == key.slot) return true;
        }
        return false;
    });
    const uint32_t clock = readDword(sr2::kFrameClock);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t silence = peer_stall_rules::silenceOf(clock, readDword(remotes[i].member + sr2::kMemberLastReceiveOffset));
        apply(remotes[i], g_watch.update({remotes[i].member, remotes[i].slot}, silence));
    }
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::RateReportWithMeasurement: return {reinterpret_cast<void*>(&reportRate), &g_rateReportOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace peer_stall
