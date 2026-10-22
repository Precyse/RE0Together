// partner_status: the PLAYER_STATE reserved word round trip and the texts shown (no game).
#include <cstdio>

#include "../src/partner_status.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace partner_status;

    check(!decode(0).known, "a zero word is a sender that does not report");
    check(encode(Status{}) == 0, "an unknown status encodes to zero");

    Status up;
    up.known = up.healthKnown = true;
    up.health = kHealthFull;
    const Status roundTrip = decode(encode(up));
    check(roundTrip.known && roundTrip.healthKnown && roundTrip.health == kHealthFull && !roundTrip.dead &&
              !roundTrip.down && !roundTrip.loading && !roundTrip.driving,
          "full health, no flags");
    check(healthPercent(roundTrip) == 100, "full health is 100 percent");
    check(stateText(roundTrip).empty(), "an up partner has no state text");

    Status hurt = up;
    hurt.health = 127;
    hurt.driving = true;
    const Status hurtBack = decode(encode(hurt));
    check(hurtBack.health == 127 && hurtBack.driving && healthPercent(hurtBack) == 50, "half health while driving");

    Status unknownHealth;
    unknownHealth.known = true;
    const Status unknownBack = decode(encode(unknownHealth));
    check(unknownBack.known && !unknownBack.healthKnown && healthPercent(unknownBack) == -1, "unknown health stays unknown");

    Status down;
    down.known = down.healthKnown = down.down = true;
    check(stateText(decode(encode(down))) == "DOWN", "down text");
    Status dead = down;
    dead.dead = true;
    check(stateText(decode(encode(dead))) == "DEAD", "dead wins over down");
    Status loading;
    loading.known = loading.loading = true;
    check(stateText(decode(encode(loading))) == "LOADING", "loading text");

    Status menu;
    menu.known = menu.menu = true;
    const Status menuBack = decode(encode(menu));
    check(menuBack.menu && !menuBack.dead && encode(menu) & kMenu, "menu bit round trip");
    check(stateText(menuBack) == "MENU", "menu text");
    Status loadingMenu = menu;
    loadingMenu.loading = true;
    check(stateText(loadingMenu) == "LOADING", "loading wins over menu");
    check(decode(0x10fe).health == kHealthFull && !decode(0x10fe).menu, "an old sender's word has no menu");

    Status alive;
    alive.known = true;
    Status dying = alive;
    dying.dead = true;
    check(transition(alive, dying) == Change::Died, "alive then dead is a death");
    check(transition(dying, dying) == Change::None, "dead then dead is nothing new");
    check(transition(dying, alive) == Change::Back, "dead then up is back");
    check(transition(dying, loading) == Change::None, "dead then loading is not back yet");
    check(transition(dying, Status{}) == Change::None, "an unknown report changes nothing");
    check(transition(Status{}, dying) == Change::Died, "the first report can be a death");
    check(transition(alive, alive) == Change::None, "up then up is nothing");

    check(away(dying) && away(loading) && !away(alive) && !away(menu), "dead and loading park the body");
    check(!away(Status{}), "an unknown status does not park the body");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
