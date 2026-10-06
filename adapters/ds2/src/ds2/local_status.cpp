// DEATH STRANDING 2: the local player's health and state as the partner sees them (partner_status.h).
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/loading_screen.h"
#include "ds2/player.h"
#include "enemy_wire.h"
#include "game.h"

namespace game {

partner_status::Status localStatus() {
    partner_status::Status status;
    const uintptr_t entity = ds2::localPlayerEntity();
    if (!entity) return status;
    status.known = true;
    const uint8_t health = enemy_vitals::readHealth(entity);
    status.healthKnown = health != enemy_wire::kHealthUnknown;
    status.health = health;
    status.dead = ds2::entityIsDead(entity);
    status.down = !status.dead && status.healthKnown && health == 0;
    status.loading = loading_screen::shown();
    status.driving = drivenVehicle().has_value();
    return status;
}

}  // namespace game
