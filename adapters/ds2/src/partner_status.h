#pragma once
// A player's health and state as the reserved word of PLAYER_STATE carries it (pure, unit tested): bits 0-7 health
// (0-254 of the maximum), then the state flags. A sender that does not report leaves the word 0 (kKnown clear).
#include <cstdint>
#include <string>

namespace partner_status {

constexpr uint32_t kHealthMask = 0xFF;
constexpr uint8_t kHealthFull = 254;
constexpr uint8_t kHealthUnknown = 255;
constexpr uint32_t kDead = 1u << 8;     // the entity's dead flag is set
constexpr uint32_t kDown = 1u << 9;     // life is 0 and the entity is not dead
constexpr uint32_t kLoading = 1u << 10; // the loading screen is up
constexpr uint32_t kDriving = 1u << 11;
constexpr uint32_t kKnown = 1u << 12;   // the sender fills this word

struct Status {
    bool known = false;
    bool healthKnown = false;
    uint8_t health = 0;  // 0-kHealthFull
    bool dead = false;
    bool down = false;
    bool loading = false;
    bool driving = false;
};

uint32_t encode(const Status& status);
Status decode(uint32_t word);

// The state to show next to the name ("DEAD", "DOWN", "LOADING"), empty when the partner is up and playing.
std::string stateText(const Status& status);

// Health as a percentage, -1 when it is not known.
int healthPercent(const Status& status);

}  // namespace partner_status
