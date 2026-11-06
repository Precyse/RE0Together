#pragma once
// When a guest's Arisen is moved to the host's (pure, unit tested): once after each load on either side, and only when the
// two are far enough apart that the guest is not already with the host.
#include <cmath>
#include <cstdint>

namespace join_warp_rule {

constexpr double kWarpFromM = 30.0;

inline double distanceM(const double a[3], const double b[3]) {
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool needsWarp(double distance) { return distance > kWarpFromM; }

// One warp decision per pair of load serials (our own, the host's): a new pair arms it, `settle` ends it.
class OncePerLoad {
public:
    void observe(uint16_t ownSerial, uint16_t hostSerial) {
        if (seen_ && ownSerial == own_ && hostSerial == host_) return;
        seen_ = true;
        own_ = ownSerial;
        host_ = hostSerial;
        pending_ = true;
    }
    bool pending() const { return pending_; }
    void settle() { pending_ = false; }

private:
    bool seen_ = false, pending_ = false;
    uint16_t own_ = 0, host_ = 0;
};

}  // namespace join_warp_rule
