// Checks the rules of the characters beside the Co-op page: the outfit picture (the saved outfit shows the character's
// default), the words under each character, the open seat's silhouette and the scene going stale (no game). Exit 0 when
// every check passes.
#include <cmath>
#include <cstdio>

#include "../src/coop_scene_rule.h"

namespace {

using namespace coop_scene_rule;
using coop_menu_rule::Player;
using lobby_wire::Place;
using lobby_wire::Presence;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Presence at(Place place, uint8_t flags = 0) { return {static_cast<uint8_t>(place), flags, lobby_wire::kNoSlot, 0, 0}; }

void testPicture() {
    check(pictureValue(false, "7d") == "7d" && pictureValue(true, "99") == "99", "a chosen outfit shows its own picture");
    check(pictureValue(false, "") == "02", "Rebecca's saved outfit shows her default");
    check(pictureValue(true, "") == "05", "Billy's saved outfit shows his default");
}

void testRoles() {
    const Player self{"Precyse", true, true, at(Place::Page), ""};
    const Player other{"Amy", false, false, at(Place::Page), ""};
    check(hostRole(self) == "You / Host", "the local host");
    check(hostRole(Player{"Amy", true, false, at(Place::Page), ""}) == "Amy / Host", "another player's host");
    check(guestRole(other) == "Amy / Ready", "a guest waiting on the page");
    check(guestRole(Player{"Amy", false, true, at(Place::Page), ""}) == "You / Ready", "the local guest");
    check(guestRole(Player{"Amy", false, false, at(Place::InGame), ""}) == "Amy / Not ready", "a guest already in game");
    check(guestRole(Player{"Amy", false, false, std::nullopt, ""}) == "Amy / Not ready", "a guest that announces nothing");
    check(guestRole(Player{"Amy", false, false, at(Place::Page, lobby_wire::kSaveChecked), ""}) == "Amy / Not ready",
          "a guest whose copy is another save");
}

void testSilhouette() {
    check(std::fabs(silhouetteAlpha(0.0f) - kSilhouetteMinAlpha) < 1e-4f, "the silhouette starts at the low end");
    check(std::fabs(silhouetteAlpha(kSilhouetteSeconds * 0.5f) - kSilhouetteMaxAlpha) < 1e-4f, "peaks at half a period");
    check(silhouetteAlpha(1.1f) >= kSilhouetteMinAlpha && silhouetteAlpha(1.1f) <= kSilhouetteMaxAlpha, "and stays between");
}

void testFresh() {
    check(fresh(0) && fresh(kStaleMs), "a scene the page just refreshed is open");
    check(!fresh(kStaleMs + 1), "one it stopped refreshing is not");
}

}  // namespace

int main() {
    testPicture();
    testRoles();
    testSilhouette();
    testFresh();
    if (g_failures == 0) std::printf("coop_scene_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
