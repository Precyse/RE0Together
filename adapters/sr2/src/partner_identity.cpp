#include "partner_identity.h"

#include "fpu_guard.h"
#include "partner_scope.h"
#include "remote_players.h"

namespace partner_identity {

uintptr_t __cdecl match(uintptr_t compared) {
    return remote_players::isRemotePlayerHuman(compared) ? compared : partner_scope::partnerHuman();
}

uintptr_t __cdecl nearest(uint32_t point) {
    FpuGuard guard;
    return remote_players::nearestRemoteTo(*reinterpret_cast<const partner_rules::Vec3*>(point));
}

}  // namespace partner_identity
