// Checks the text of the Co-op page's rows (no game). Exit 0 when every check passes.
#include <cstdio>
#include <string>

#include "../src/coop_page_rule.h"

namespace {

using character_owner::Character;
using coop_page_rule::Local;
using coop_page_rule::Row;

constexpr uint8_t kHostSlot = 0;
constexpr uint8_t kGuestSlot = 1;
constexpr size_t kRowCount = 7;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

const Row* rowOf(const std::vector<Row>& rows, const char* label) {
    for (const Row& row : rows) {
        if (row.label == label) return &row;
    }
    return nullptr;
}

std::string value(const std::vector<Row>& rows, const char* label) {
    const Row* row = rowOf(rows, label);
    return row ? row->value : "<no row>";
}

bool bad(const std::vector<Row>& rows, const char* label) {
    const Row* row = rowOf(rows, label);
    return row && row->bad;
}

PeerInfo peer(uint8_t slot, const char* name, uint16_t rttMs) {
    PeerInfo info;
    info.slot = slot;
    info.name = name;
    info.rttMs = rttMs;
    return info;
}

// A linked guest (slot 1) with the host (slot 0) as its partner.
debug_stats::Snapshot guestView(bool stateMode) {
    debug_stats::Snapshot stats;
    stats.linked = true;
    stats.stateMode = stateMode;
    stats.localSlot = kGuestSlot;
    stats.hostSlot = kHostSlot;
    stats.peers = {peer(kHostSlot, "Ahmad", 42), peer(kGuestSlot, "Me", 0)};
    return stats;
}

void testTogether() {
    const auto rows = coop_page_rule::rows(guestView(true), Local{Character::Rebecca, 0x7d});
    check(rows.size() == kRowCount, "seven status rows");
    check(value(rows, "Partner") == "Ahmad", "partner name");
    check(value(rows, "Connection") == "connected", "connected");
    check(value(rows, "Ping") == "42 ms", "ping");
    check(value(rows, "Mode") == "state", "state mode");
    check(value(rows, "Character") == "Rebecca", "character");
    check(value(rows, "Outfit") == "Western", "outfit name");
    check(value(rows, "Host") == "Ahmad", "the host is the partner");
    check(!bad(rows, "Connection"), "a connected link is not red");
}

void testHostSide() {
    debug_stats::Snapshot stats;
    stats.linked = true;
    stats.localSlot = kHostSlot;
    stats.hostSlot = kHostSlot;
    stats.peers = {peer(kHostSlot, "Me", 0), peer(kGuestSlot, "Guest", 7)};
    const auto rows = coop_page_rule::rows(stats, Local{Character::Billy, 0x05});
    check(value(rows, "Partner") == "Guest", "the partner is the other slot");
    check(value(rows, "Ping") == "7 ms", "the partner's ping");
    check(value(rows, "Host") == "you", "the local host");
    check(value(rows, "Mode") == "input", "input mode");
    check(value(rows, "Outfit") == "Prisoner", "Billy's default outfit");
}

void testWaiting() {
    debug_stats::Snapshot stats;
    stats.linked = true;
    stats.localSlot = kHostSlot;
    stats.hostSlot = kHostSlot;
    stats.peers = {peer(kHostSlot, "Me", 0)};
    const auto rows = coop_page_rule::rows(stats, Local{Character::Unknown, appearance_rule::kNoOutfit});
    check(value(rows, "Partner") == "none", "no partner yet");
    check(value(rows, "Connection") == "waiting for partner", "waiting for the partner");
    check(value(rows, "Ping") == "none", "no ping without a partner");
    check(value(rows, "Character") == "none", "no character before ownership");
    check(value(rows, "Outfit") == "none", "no outfit without a character");
    check(value(rows, "Host") == "you", "a host alone");
}

void testDown() {
    const auto rows = coop_page_rule::rows(debug_stats::Snapshot{}, Local{Character::Unknown, appearance_rule::kNoOutfit});
    check(value(rows, "Connection") == "link down", "link down");
    check(bad(rows, "Connection"), "a down link is red");
    check(value(rows, "Host") == "none", "no host without a link");
    check(value(rows, "Mode") == "input", "input mode by default");
}

void testNamesAndOutfits() {
    debug_stats::Snapshot stats = guestView(false);
    stats.peers[0].name = "AVeryLongSteamNameIndeedMoreThanTwentyChars";
    const auto rows = coop_page_rule::rows(stats, Local{Character::Billy, 0x99});
    check(value(rows, "Partner") == "AVeryLongSteamNameIn", "long names are cut");
    check(value(rows, "Outfit") == "Wolf Force", "a DLC outfit");
    check(value(coop_page_rule::rows(stats, Local{Character::Billy, 0x42}), "Outfit") == "0x42",
          "an unlisted model is shown as its byte");
    check(value(coop_page_rule::rows(stats, Local{Character::Rebecca, 0x95}), "Outfit") == "0x95",
          "an opposite-sex model is not named");
}

void testFresh() {
    check(coop_page_rule::fresh(0), "a page just published is shown");
    check(coop_page_rule::fresh(coop_page_rule::kStaleMs), "up to the limit is shown");
    check(!coop_page_rule::fresh(coop_page_rule::kStaleMs + 1), "a page nobody refreshes goes away");
}

}  // namespace

int main() {
    testTogether();
    testHostSide();
    testWaiting();
    testDown();
    testNamesAndOutfits();
    testFresh();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
