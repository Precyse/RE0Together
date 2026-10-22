// The remote view rule of milestone 9 (no game): which frustum tests ask every remote.
#include <cstdio>

#include "../src/frustum_rules.h"
#include "check.h"

int main() {
    using namespace frustum_rules;

    constexpr uintptr_t kLocalFrustum = 0x277CBF0;
    check(asksEveryRemote(kPartnerFrustum, 2) && asksEveryRemote(kPartnerFrustum, 3), "a test against the partner frustum asks every remote");
    check(!asksEveryRemote(kPartnerFrustum, 1) && !asksEveryRemote(kPartnerFrustum, 0),
          "with one remote it goes to the game's function untouched (the 2-player result)");
    check(!asksEveryRemote(kLocalFrustum, 3) && !asksEveryRemote(0, 3), "the local frustum and anything else is tested as it is");
    check(kFrustumBytes == 0x210 && kPartnerFrustum - kLocalFrustum == kFrustumBytes, "the two frustums of the game are 0x210 bytes apart");
    return g_failures == 0 ? 0 : 1;
}
