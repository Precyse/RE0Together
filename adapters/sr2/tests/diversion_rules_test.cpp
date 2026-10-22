// The pure rules of milestone 16 wave 2, diversions and activities (no game): the 2+S pin, the first matching remote, the per-slot
// tables, the Snatch geometry, the streaking coat swap and the logging edge.
#include <cstdio>

#include "../src/diversion_rules.h"
#include "check.h"

namespace {

using diversion_rules::kNone;
using partner_rules::Remote;
using partner_rules::Vec3;

constexpr uint32_t kLocalHandle = 0x11;
constexpr uint32_t kOwnerHandle = 0x22;
constexpr uint32_t kPartnerHandle = 0x33;

void theTwoPlusSActivities() {
    bool matchesTheShippedTypes = true;
    for (int32_t type = 0; type <= 0x10; ++type) {
        matchesTheShippedTypes = matchesTheShippedTypes && diversion_rules::isTwoPlusS(type, false) == !activity_rules::isAllPlayersType(type);
    }
    check(matchesTheShippedTypes, "every type 0 to 0x10 is 2+S exactly when it is not an all-players type");
    check(!diversion_rules::isTwoPlusS(-1, false), "no activity is not 2+S");
    check(diversion_rules::isTwoPlusS(-1, true), "the co-op race diversion is 2+S without an activity");
    check(diversion_rules::isTwoPlusS(0xE, false) && diversion_rules::isTwoPlusS(2, false), "racing and escort are 2+S");
    check(!diversion_rules::isTwoPlusS(0xA, false) && diversion_rules::isTwoPlusS(0xA, true), "an all-players type is 2+S only through the race flag");
}

void theParticipantPin() {
    check(diversion_rules::participantPin(kOwnerHandle, kOwnerHandle, kPartnerHandle, true) == kPartnerHandle, "the owner is pinned to the partner");
    check(diversion_rules::participantPin(kPartnerHandle, kOwnerHandle, kPartnerHandle, true) == kOwnerHandle, "the partner is pinned to the owner");
    check(diversion_rules::participantPin(kLocalHandle, kOwnerHandle, kPartnerHandle, true) == 0, "a spectator has no pin");
    check(diversion_rules::participantPin(kOwnerHandle, kOwnerHandle, kPartnerHandle, false) == 0, "no pin outside a 2+S activity");
    check(diversion_rules::participantPin(kOwnerHandle, kOwnerHandle, kOwnerHandle, true) == 0, "a pair of one handle gives no pin");
    check(diversion_rules::participantPin(0, 0, kPartnerHandle, true) == 0, "no local handle gives no pin");
}

Remote remote(uintptr_t human, uint8_t slot) { return {human, static_cast<uintptr_t>(0xB00 + slot), slot, 0}; }

void theFirstMatchingRemote() {
    const Remote remotes[] = {remote(0xA1, 1), remote(0xA2, 2), remote(0, 3), remote(0xA4, 4)};
    auto is = [](uintptr_t wanted) { return [wanted](const Remote& r) { return r.human == wanted; }; };
    check(diversion_rules::firstMatching(remotes, 4, 1, is(0xA2)) == 1, "the default answers first when it matches");
    check(diversion_rules::firstMatching(remotes, 4, 1, [](const Remote&) { return true; }) == 1, "the default answers first when all match");
    check(diversion_rules::firstMatching(remotes, 4, 3, is(0xA1)) == 0, "else the first in pairing order");
    check(diversion_rules::firstMatching(remotes, 4, 3, [](const Remote& r) { return r.human != 0xA4; }) == 0, "the default is skipped when it does not match");
    check(diversion_rules::firstMatching(remotes, 4, 1, is(0xFF)) == 1, "none gives the default");
    check(diversion_rules::firstMatching(remotes, 4, 1, [](const Remote& r) { return r.slot == 3; }) == 1, "a remote without a human never matches");
    check(diversion_rules::findFirst(remotes, 4, 1, is(0xFF)) == kNone, "findFirst says none");
}

void theSlotTable() {
    diversion_rules::SlotTable<int> table;
    check(table.get(2, 0xA2) == nullptr, "an empty slot reads empty");
    table.set(2, 0xA2, 7);
    check(table.get(2, 0xA2) && *table.get(2, 0xA2) == 7, "a stored value is read back");
    check(table.get(2, 0xA9) == nullptr && table.valueOr(2, 0xA9, -1) == -1, "a new human in the slot reads empty");
    *table.get(2, 0xA2) = 9;
    check(table.valueOr(2, 0xA2, -1) == 9, "the stored value can be changed in place");
    table.set(8, 0xA8, 1);
    check(table.get(8, 0xA8) == nullptr && table.get(9, 0xA8) == nullptr, "a slot out of range is ignored");
    table.set(3, 0xA3, 4);
    table.clear(2);
    check(table.get(2, 0xA2) == nullptr && table.get(3, 0xA3) != nullptr, "clear removes one slot");
    table.clearAll();
    check(table.get(3, 0xA3) == nullptr, "clear all removes every slot");
}

void theTeamHits() {
    const Remote remotes[] = {remote(0xA1, 1), remote(0xA2, 2), remote(0xA3, 3)};
    diversion_rules::TeamHits hits;
    check(hits.teammate(remotes, 3, 0, 0x50) == kNone, "nobody has hit anything yet");
    hits.record(3, 0xA3, 0x50);
    check(hits.teammate(remotes, 3, 0, 0x50) == 2, "a remote whose last hit is the local player's is the teammate");
    hits.record(1, 0xA1, 0x50);
    check(hits.teammate(remotes, 3, 0, 0x50) == 0 && hits.teammate(remotes, 3, 2, 0x50) == 2, "the default partner is looked at first");
    check(hits.teammate(remotes, 3, 1, 0x50) == 0, "a default that does not match gives way to the pairing order");
    check(hits.teammate(remotes, 3, 0, 0x51) == kNone, "no match keeps the default's last hit");
    check(hits.lastOf(1, 0xA1) == 0x50, "the default partner's last hit is kept");
    check(hits.teammate(remotes, 3, 0, 0) == kNone, "a zero hit never matches");
    hits.record(1, 0xA1, 0);
    check(hits.teammate(remotes, 3, 1, 0) == kNone, "a remote that hit nothing never matches a zero hit");
    hits.clear();
    check(hits.lastOf(1, 0xA1) == 0, "clear forgets the hits");
}

void theCarryCounts() {
    check(diversion_rules::afterDelivery(5, 2) == 3 && diversion_rules::afterDelivery(2, 2) == 0 && diversion_rules::afterDelivery(1, 4) == 0,
          "the clamp x > 0 ? x : 0 of 0x657C73 to 0x657C7F");
    diversion_rules::CarryCounts counts;
    int32_t field = 0;
    counts.set(1, 0xA1, true, field, 1);
    check(field == 1 && counts.get(1, 0xA1, true, field) == 1, "the default partner is read and written through the game's field");
    counts.set(2, 0xA2, false, field, 1);
    check(field == 1 && counts.get(2, 0xA2, false, field) == 1, "another remote uses the table and leaves the field alone");
    counts.set(2, 0xA2, false, field, 4);
    counts.deliver(2, 0xA2, false, field, 3);
    check(counts.get(2, 0xA2, false, field) == 1 && field == 1, "a delivery reduces the table count");
    counts.deliver(2, 0xA2, false, field, 3);
    check(counts.get(2, 0xA2, false, field) == 0, "a delivery never goes below zero");
    counts.deliver(1, 0xA1, true, field, 3);
    check(field == 0, "a delivery by the default partner reduces the field");
    counts.set(3, 0xA3, false, field, 1);
    counts.set(1, 0xA1, true, field, 1);
    counts.set(3, 0xA3, false, field, 0);
    check(counts.get(3, 0xA3, false, field) == 0 && field == 1, "clearing one remote leaves the others");
    counts.set(3, 0xA3, false, field, 1);
    counts.clearAll(field);
    check(field == 0 && counts.get(3, 0xA3, false, field) == 0, "clear all zeroes the field and the table");
    check(counts.get(4, 0xA4, false, field) == 0, "a remote that never picked up carries nothing");
}

void theStreakMask() {
    diversion_rules::StreakMask mask;
    check(mask.gameByte() == 0 && !mask.isOn(1), "nobody streaks at first");
    mask.set(2, true);
    check(mask.isOn(2) && !mask.isOn(1) && mask.gameByte() == 1, "on sets the slot's bit and the game's byte");
    mask.set(5, true);
    mask.set(2, false);
    check(!mask.isOn(2) && mask.isOn(5) && mask.gameByte() == 1, "off clears one bit; the byte stays while another streaks");
    mask.set(5, false);
    check(mask.gameByte() == 0, "the byte is zero when nobody streaks");
    mask.set(8, true);
    check(mask.gameByte() == 0 && !mask.isOn(8), "a slot out of range is ignored");
    mask.set(1, true);
    mask.clear();
    check(mask.gameByte() == 0, "clear empties the mask");
}

void theCoatSwap() {
    diversion_rules::SwapTable<int> coats;
    int cell = 0;
    coats.follow(1, 0xA1, cell);
    cell = 11;  // the game's code stores the default partner's coat
    coats.follow(1, 0xA1, cell);
    check(cell == 11 && coats.isCanonical(1, 0xA1), "one remote: nothing moves");
    coats.enter(2, 0xA2, cell);
    check(cell == 0, "a remote with no coat yet starts with an empty cell");
    cell = 22;
    coats.leave(2, 0xA2, cell);
    check(cell == 11, "leaving puts the default partner's coat back");
    check(coats.valueOf(2, 0xA2, cell) == 22 && coats.valueOf(1, 0xA1, cell) == 11, "each remote's coat is found where it is kept");
    coats.enter(2, 0xA2, cell);
    check(cell == 22, "a second turn finds the remote's coat in the cell");
    coats.leave(2, 0xA2, cell);
    coats.follow(2, 0xA2, cell);
    check(cell == 22 && coats.isCanonical(2, 0xA2) && coats.valueOf(1, 0xA1, cell) == 11,
          "when the default partner changes, the canonical entry moves first");
    coats.enter(1, 0xA1, cell);
    check(cell == 11, "the old default partner's coat comes back from the table");
    coats.leave(1, 0xA1, cell);
    check(cell == 22, "and the new default's coat returns to the cell");
    coats.follow(2, 0xA9, cell);
    check(cell == 0, "a new human in the canonical slot starts empty");
    coats.forget();
    check(cell == 0 && coats.valueOf(1, 0xA1, cell) == 0 && !coats.isCanonical(2, 0xA9), "forget drops every copy and the canonical slot");
}

void theDeliveryGeometry() {
    check(diversion_rules::deliveryMarker(0) == 0 && diversion_rules::deliveryMarker(1) == 1 && diversion_rules::deliveryMarker(3) == 1,
          "the local player uses the first marker and every remote the second");
    const Vec3 marker{10, 0, 0};
    check(diversion_rules::inDeliveryRange({12, 0, 0}, marker, 2.0f), "exactly at the radius delivers");
    check(!diversion_rules::inDeliveryRange({12.5f, 0, 0}, marker, 2.0f), "beyond the radius does not");
    check(diversion_rules::inDeliveryRange({10, 3, 0}, marker, 3.5f), "the on-foot radius reaches further");
    check(!diversion_rules::inDeliveryRange({10, 3, 0}, marker, 2.0f), "the vehicle radius is shorter");
    check(diversion_rules::inDeliveryRange({11, 1, 1}, marker, 1.75f), "the distance is three dimensional");
}

void theBestSpawn() {
    const Vec3 candidates[] = {{0, 0, 0}, {100, 0, 0}, {50, 0, 0}};
    const Vec3 players[] = {{0, 0, 0}, {120, 0, 0}};
    check(diversion_rules::bestSpawn(candidates, 3, players, 2) == 2, "the farthest from its nearest player wins");
    const Vec3 tie[] = {{10, 0, 0}, {-10, 0, 0}};
    const Vec3 one[] = {{0, 0, 0}};
    check(diversion_rules::bestSpawn(tie, 2, one, 1) == 0, "ties go to the first answer");
    check(diversion_rules::bestSpawn(candidates, 0, players, 2) == kNone, "no candidate, no answer");
    check(diversion_rules::bestSpawn(candidates, 3, players, 0) == 0, "no players: the first candidate");
}

void theLogEdge() {
    diversion_rules::EdgeLog<2> log;
    check(log.changed(0, true), "the first poll logs");
    check(!log.changed(0, true) && !log.changed(0, true), "repeats do not");
    check(log.changed(0, false), "a change logs again");
    check(log.changed(1, false), "another subject logs its first poll");
    check(!log.changed(1, false) && log.changed(1, true), "and its own changes");
    check(!log.changed(2, true), "a subject out of range never logs");
}

}  // namespace

int main() {
    theTwoPlusSActivities();
    theParticipantPin();
    theFirstMatchingRemote();
    theSlotTable();
    theTeamHits();
    theCarryCounts();
    theStreakMask();
    theCoatSwap();
    theDeliveryGeometry();
    theBestSpawn();
    theLogEdge();
    return g_failures == 0 ? 0 : 1;
}
