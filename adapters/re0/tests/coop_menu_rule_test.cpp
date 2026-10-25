// Checks the Co-op page's rows and its Options page: what a guest only reads, the start row on host and guest, the
// readiness of a player and the value steps (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/coop_menu_rule.h"

namespace {

using namespace coop_menu_rule;
using lobby_wire::Presence;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Presence at(Place place, uint8_t flags = 0) { return {static_cast<uint8_t>(place), flags, lobby_wire::kNoSlot, 0, 0}; }

Inputs hostInputs() {
    Inputs in;
    in.isHost = true;
    in.character = "Rebecca";
    in.outfit = "Western";
    in.outfitChoosable = true;
    in.save = "Slot 3 / Normal / 0:38:14 / Area 4";
    in.savePicked = true;
    in.savesOffered = true;
    return in;
}

void testRows() {
    const PageView page = view(hostInputs());
    check(page.header == "Co-op", "the header");
    check(page.rows[kCharacter].label == "Character" && page.rows[kOutfit].label == "Outfit" && page.rows[kSave].label == "Save" &&
              page.rows[kOptions].label == "Options" && page.rows[kStart].label == "Start",
          "the rows in order: character, outfit, save, options, start");
    check(kCharacter == 0 && kStart == kRowCount - 1 && kRowCount == 5, "five rows, character first and start last");
    check(!page.rows[kOptions].locked && page.rows[kOptions].value.empty(), "the options row opens the Options page and shows no value");
}

void testHostRows() {
    const PageView page = view(hostInputs());
    check(!page.rows[kCharacter].locked && !page.rows[kSave].locked, "the host changes its options");
    check(page.rows[kStart].label == "Start" && !page.rows[kStart].locked, "the host may start a picked save");
    Inputs none = hostInputs();
    none.savePicked = false;
    none.save = kNone;
    check(view(none).rows[kStart].locked && view(none).rows[kSave].value == "None", "no pick, no start");
    Inputs loading = hostInputs();
    loading.start = StartStatus::Loading;
    const PageView busy = view(loading);
    check(busy.rows[kStart].locked && busy.rows[kStart].value == "Loading" && busy.rows[kSave].locked && busy.rows[kOutfit].locked,
          "a start in progress locks the page");
    check(!busy.rows[kOptions].locked, "but the Options page stays open to look at");
}

void testGuestRows() {
    Inputs in = hostInputs();
    in.isHost = false;
    const PageView page = view(in);
    // The guard of the host-decided rows: a guest sees them, but only the host's choice.
    check(page.rows[kCharacter].locked && page.rows[kSave].locked, "a guest only reads the host's choices");
    check(!page.rows[kOutfit].locked, "a guest picks its own outfit");
    check(!page.rows[kOptions].locked, "a guest opens the Options page to read it");
    check(page.rows[kStart].label == "Waiting for host" && page.rows[kStart].locked, "a guest waits for the host");
    in.start = StartStatus::WaitingForSave;
    check(view(in).rows[kStart].label == "Waiting for save", "a guest whose copy is not the host's yet");
    in.start = StartStatus::Idle;
    in.outfitChoosable = false;
    check(view(in).rows[kOutfit].locked, "no outfit list, no outfit change");
}

void testOptionsPage() {
    const PageView host = optionsView(true, "On");
    check(host.header == "Options", "the Options page's header");
    check(host.rows[kWorldPause].label == "Pause in Menus" && host.rows[kWorldPause].value == "On", "the host's world pause setting");
    check(!host.rows[kWorldPause].locked, "the host changes it");
    check(host.rows[kBack].label == "Back" && !host.rows[kBack].locked && kOptionsRowCount == 2, "and the way back");
    const PageView guest = optionsView(false, "Off");
    check(guest.rows[kWorldPause].locked && guest.rows[kWorldPause].value == "Off", "a guest reads the host's setting");
    check(!guest.rows[kBack].locked, "and may go back");
}

void testReady() {
    Player host{"Precyse", true, true, at(Place::Page), ""};
    Player guest{"Amy", false, false, at(Place::Page, lobby_wire::kSaveChecked | lobby_wire::kSaveMatches), ""};
    check(isReady(host) && isReady(guest), "a game waiting on the page with a matching copy is ready");
    guest.presence = at(Place::Page, lobby_wire::kSaveChecked);
    check(!isReady(guest), "a guest whose copy is another save is not ready");
    guest.presence = at(Place::Page);
    check(isReady(guest), "a guest without a host pick to compare is ready");
    guest.presence = at(Place::InGame);
    check(!isReady(guest), "a guest already in game is not waiting on the page");
    guest.presence = at(Place::Title);
    check(!isReady(guest), "nor one in the title's menus");
    guest.presence.reset();
    check(!isReady(guest), "a guest that announces nothing is not ready");
}

void testStepIndex() {
    check(stepIndex(0, 3, 1) == 1 && stepIndex(2, 3, 1) == 0, "right steps and wraps");
    check(stepIndex(0, 3, -1) == 2, "left wraps to the last");
    check(stepIndex(-1, 3, 1) == 0 && stepIndex(5, 3, -1) == 0, "a value not offered starts from the first");
    check(stepIndex(0, 0, 1) == -1, "no values, no step");
}

}  // namespace

int main() {
    testRows();
    testHostRows();
    testGuestRows();
    testOptionsPage();
    testReady();
    testStepIndex();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
