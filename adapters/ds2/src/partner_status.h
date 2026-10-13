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
constexpr uint32_t kMenu = 1u << 13;    // a pausing game menu or the adapter's cargo menu is open

struct Status {
    bool known = false;
    bool healthKnown = false;
    uint8_t health = 0;  // 0-kHealthFull
    bool dead = false;
    bool down = false;
    bool loading = false;
    bool driving = false;
    bool menu = false;
};

// What a new report changes about the partner, judged against the last report that was known.
enum class Change { None, Died, Back };

uint32_t encode(const Status& status);
Status decode(uint32_t word);

// The state to show next to the name ("DEAD", "DOWN", "LOADING", "MENU"), empty when the partner is up and playing.
std::string stateText(const Status& status);

// Died: now is dead and the last known status was not. Back: the last known status was dead and now is up (known, not
// dead, not loading). A report that is not known never changes anything.
Change transition(const Status& lastKnown, const Status& now);

// Whether the partner's body has nothing to stand for: dead or on a loading screen.
bool away(const Status& status);

// Health as a percentage, -1 when it is not known.
int healthPercent(const Status& status);

}  // namespace partner_status
