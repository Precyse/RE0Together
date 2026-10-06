// DEATH STRANDING 2: the engine streams tiles and activates entities around ONE point, the streaming manager's observer (global
// 0x146266958). Its script export SetObserverPositionOverride (0x14071fdb0: manager +0x400 = 1, position at +0x408) is the only way
// to move it, ClearObserverPositionOverride (0x14071fd80) puts it back. While the partner is beyond the loaded world the
// observer is moved toward the partner, by half the distance and never more than kMaxShiftMetres, so this machine's player
// stays well inside the near radius of the observer (entity tables 768 / 800 m) while the partner comes within reach of it.
// A test switch for now (test command observer.txt), off by default: it thins the detail ring around this machine's player.
#include "ds2/observer_midpoint.h"

#include <atomic>
#include <cmath>

#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/far_partner.h"
#include "ds2/partner_focus.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kSetObserverOverride = 0x14071fdb0;    // (const WorldPosition*)
constexpr uintptr_t kClearObserverOverride = 0x14071fd80;  // ()
constexpr double kMaxShiftMetres = 300.0;                   // the observer never leaves this radius around the local player

std::atomic<bool> g_enabled{false};
bool g_applied = false;  // simulation thread: the override is set

void clearOverride() {
    if (!g_applied) return;
    g_applied = false;
    reinterpret_cast<void (*)()>(ds2::at(kClearObserverOverride))();
    logger::write("observer_midpoint: the observer is back on the local player");
}

void moveObserver(const decima::WorldPosition& self, const decima::WorldPosition& partner, double metres) {
    const double shift = std::fmin(metres / 2.0, kMaxShiftMetres) / metres;
    decima::WorldPosition observer{self.x + (partner.x - self.x) * shift, self.y + (partner.y - self.y) * shift,
                                   self.z + (partner.z - self.z) * shift};
    reinterpret_cast<void (*)(const decima::WorldPosition*)>(ds2::at(kSetObserverOverride))(&observer);
    if (!g_applied) logger::write("observer_midpoint: the partner is %.0f m away, the observer moves %.0f m toward it", metres, shift * metres);
    g_applied = true;
}

void tick() {
    const auto self = game::localPlayer();
    decima::WorldPosition partner;
    if (!g_enabled.load() || !self || !partner_focus::partnerPosition(partner)) {
        clearOverride();
        return;
    }
    const decima::WorldPosition here{self->position.x, self->position.y, self->position.z};
    const double metres = std::sqrt(partner_focus::squaredDistance(here, partner));
    if (metres <= far_partner::kBeyondLoadedWorldMetres) {
        clearOverride();
        return;
    }
    moveObserver(here, partner, metres);
}

}  // namespace

namespace observer_midpoint {

void installEarly() { sim_tick::add(&tick, "observer midpoint", sim_tick::Gate::Gameplay); }

void setEnabled(bool enabled) {
    g_enabled = enabled;
    logger::write("observer_midpoint: %s", enabled ? "on" : "off");
}

}  // namespace observer_midpoint
