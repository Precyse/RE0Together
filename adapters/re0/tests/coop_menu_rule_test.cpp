// Checks the Co-op page's rows: the players' text, what a guest only reads, the start row on host and guest, and the
// value steps (no game). Exit 0 when every check passes.
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
    in.host = Player{"Precyse", true, true, at(Place::Page)};
    in.guest = Player{"Amy", false, false, at(Place::Page, lobby_wire::kSaveChecked | lobby_wire::kSaveMatches)};
    in.character = "Rebecca";
    in.outfit = "Western";
    in.outfitChoosable = true;
    in.worldPause = "Never";
    in.partnerMode = "State";
    in.save = "Slot 3 / Normal / 0:38:14 / Area 4";
    in.savePicked = true;
    in.savesOffered = true;
    return in;
}

void testPlayers() {
    const PageView page = view(hostInputs());
    check(page.header == "Co-op", "the header");
    check(page.rows[kHostPlayer].label == "Precyse (you)" && page.rows[kHostPlayer].value == "Host / Ready", "the host on its page");
    check(page.rows[kGuestPlayer].label == "Amy" && page.rows[kGuestPlayer].value == "Guest / Ready", "a guest whose copy matches");
    Inputs differs = hostInputs();
    differs.guest->presence = at(Place::Page, lobby_wire::kSaveChecked);
    check(view(differs).rows[kGuestPlayer].value == "Guest / Save differs", "a guest whose copy is another save");
    differs.guest->presence = at(Place::Page);
    check(view(differs).rows[kGuestPlayer].value == "Guest / Ready", "a guest without a host pick to compare is ready");
    differs.guest->presence = at(Place::InGame);
    check(view(differs).rows[kGuestPlayer].value == "Guest / In game", "a guest in game");
    differs.guest->presence.reset();
    check(view(differs).rows[kGuestPlayer].value == "Guest / Away", "a guest that announces nothing is away");
    differs.guest.reset();
    check(view(differs).rows[kGuestPlayer].label == "Open", "no guest yet");
}

void testHostRows() {
    const PageView page = view(hostInputs());
    check(!page.rows[kCharacter].locked && !page.rows[kWorldPause].locked && !page.rows[kSave].locked, "the host changes its options");
    check(page.rows[kPartnerMode].locked, "the partner mode is read only in game");
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
}

void testGuestRows() {
    Inputs in = hostInputs();
    in.isHost = false;
    const PageView page = view(in);
    // The guard of the host-decided rows: a guest sees them, but only the host's choice.
    check(page.rows[kCharacter].locked && page.rows[kWorldPause].locked && page.rows[kSave].locked, "a guest only reads the host's choices");
    check(!page.rows[kOutfit].locked, "a guest picks its own outfit");
    check(page.rows[kStart].label == "Waiting for host" && page.rows[kStart].locked, "a guest waits for the host");
    in.start = StartStatus::WaitingForSave;
    check(view(in).rows[kStart].label == "Waiting for save", "a guest whose copy is not the host's yet");
    in.start = StartStatus::Idle;
    in.outfitChoosable = false;
    check(view(in).rows[kOutfit].locked, "no outfit list, no outfit change");
}

void testStepIndex() {
    check(stepIndex(0, 3, 1) == 1 && stepIndex(2, 3, 1) == 0, "right steps and wraps");
    check(stepIndex(0, 3, -1) == 2, "left wraps to the last");
    check(stepIndex(-1, 3, 1) == 0 && stepIndex(5, 3, -1) == 0, "a value not offered starts from the first");
    check(stepIndex(0, 0, 1) == -1, "no values, no step");
}

}  // namespace

int main() {
    testPlayers();
    testHostRows();
    testGuestRows();
    testStepIndex();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
