#pragma once
#include <cstddef>
#include <cstdint>

#include "remote_players.h"

// Owns the two partner getters of the game, get_remote_player (0x9D7BD0) and the partner member getter (0x8CA380).
// Without an override they answer the same partner on every machine (remote_players::defaultPartner); while a thunk
// loops over the remotes, an Override makes them answer the remote of that turn on this thread.
namespace partner_scope {

struct Target {
    uintptr_t human;
    uintptr_t member;
};

// While alive, the two getters of this thread answer `target`. The earlier answer returns on destruction.
class Override {
public:
    explicit Override(Target target);
    ~Override();
    Override(const Override&) = delete;
    Override& operator=(const Override&) = delete;

private:
    bool previousActive_;
    Target previous_;
};

// What the getters answer while a message from `member` is handled: that member and its human (0 until it has one).
// False, so the handler runs as it is, for the local member, an unknown member or a pairing that cannot be trusted.
bool targetOfSender(uintptr_t member, Target& out);

// A detour for hooks::install and the slot that receives the trampoline of the original getter.
struct GetterHook {
    void* detour;
    void** original;
};

GetterHook humanHook();
GetterHook memberHook();

// True while an Override is alive on this thread.
bool overrideActive();

// What the game's getters answer to this thread now: the override of the turn, else the default partner.
uintptr_t partnerHuman();
uintptr_t partnerMember();

// With trace on, the getters log a change of the default partner or of any remote player and run
// member_slots::check on it.
void enableTrace(uint32_t maxPlayers);

// Runs fn(human) once per remote player human, with the getters answering that remote, until fn returns true.
// Without a remote human it runs once with 0 and no override, so the game's own answer stands.
template <typename Fn>
bool anyRemoteHuman(Fn fn) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    bool found = false;
    bool ran = false;
    for (size_t i = 0; i < snapshot.pairing.count && !found; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human) continue;
        ran = true;
        Override scope({remote.human, remote.member});
        found = fn(remote.human);
    }
    return ran ? found : fn(uintptr_t{0});
}

// Runs fn(human) for every remote player human, as anyRemoteHuman does.
template <typename Fn>
void eachRemoteHuman(Fn fn) {
    anyRemoteHuman([&fn](uintptr_t human) {
        fn(human);
        return false;
    });
}

// Runs fn(member) once per remote member, with the getters answering that member (and its human, 0 while it has none).
// Without a remote member it runs once with 0 and no override.
template <typename Fn>
void eachRemoteMember(Fn fn) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    bool ran = false;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.member) continue;
        ran = true;
        Override scope({remote.human, remote.member});
        fn(remote.member);
    }
    if (!ran) fn(uintptr_t{0});
}

}  // namespace partner_scope
