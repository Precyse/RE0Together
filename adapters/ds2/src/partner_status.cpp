#include "partner_status.h"

namespace partner_status {

uint32_t encode(const Status& status) {
    if (!status.known) return 0;
    uint32_t word = kKnown;
    word |= status.healthKnown ? status.health : kHealthUnknown;
    if (status.dead) word |= kDead;
    if (status.down) word |= kDown;
    if (status.loading) word |= kLoading;
    if (status.driving) word |= kDriving;
    return word;
}

Status decode(uint32_t word) {
    Status status;
    if (!(word & kKnown)) return status;
    status.known = true;
    status.health = static_cast<uint8_t>(word & kHealthMask);
    status.healthKnown = status.health <= kHealthFull;
    status.dead = (word & kDead) != 0;
    status.down = (word & kDown) != 0;
    status.loading = (word & kLoading) != 0;
    status.driving = (word & kDriving) != 0;
    return status;
}

std::string stateText(const Status& status) {
    if (status.dead) return "DEAD";
    if (status.down) return "DOWN";
    if (status.loading) return "LOADING";
    return {};
}

int healthPercent(const Status& status) {
    if (!status.known || !status.healthKnown) return -1;
    return status.health * 100 / kHealthFull;
}

}  // namespace partner_status
