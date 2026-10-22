// Checks the room authority rules on two AuthorityTables wired to each other (no game, no network). Exit 0 when every
// check passes.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/room_authority_rule.h"
#include "../src/room_authority_wire.h"

namespace {

namespace rule = room_authority_rule;
namespace wire = room_authority_wire;
using authority::AuthorityTable;
using authority::Outcome;
using rule::Outgoing;
using rule::Step;

constexpr uint8_t kHost = 0;
constexpr uint8_t kGuest = 1;
constexpr uint8_t kNoOwner = authority_wire::kNoOwner;
constexpr uint16_t kRoomA = 0x05;
constexpr uint16_t kRoomB = 0x2a;
constexpr uint16_t kLoading = scene::kNone;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// One machine: its slot and its copy of the table.
struct Machine {
    explicit Machine(uint8_t slot) : slot(slot) { table.setSession(slot, kHost); }

    // The messages of one game tick with the own character in `loaded` (or not in it).
    std::vector<Outgoing> tick(uint16_t loaded, bool ownInRoom = true) {
        return rule::reconcile(table, slot, loaded, ownInRoom);
    }

    Outcome receive(const Outgoing& message, uint8_t sender) {
        return table.onMessage(wire::tableKind(message.type), sender, message.message);
    }

    void receive(const std::vector<Outgoing>& messages, uint8_t sender) {
        for (const Outgoing& message : messages) receive(message, sender);
    }

    uint8_t slot;
    AuthorityTable table;
};

bool agree(const Machine& a, const Machine& b, uint16_t scene) {
    return a.table.ownerOf(scene) == b.table.ownerOf(scene) && a.table.epochOf(scene) == b.table.epochOf(scene);
}

void testStepRule() {
    check(rule::stepFor(kRoomA, kRoomA, true, kNoOwner, kGuest) == Step::Claim, "an unowned loaded room is claimed");
    check(rule::stepFor(kRoomA, kRoomA, true, kHost, kGuest) == Step::None, "a room the peer owns is left to it");
    check(rule::stepFor(kRoomA, kRoomA, true, kGuest, kGuest) == Step::None, "an owned room in use is kept");
    check(rule::stepFor(kRoomA, kRoomA, false, kNoOwner, kGuest) == Step::None, "no claim without the own character");
    check(rule::stepFor(kRoomA, kRoomA, false, kGuest, kGuest) == Step::Release,
          "the owner whose character is not in the room releases it");
    check(rule::stepFor(kRoomA, kRoomB, true, kGuest, kGuest) == Step::Release, "leaving the room releases it");
    check(rule::stepFor(kRoomA, kLoading, true, kGuest, kGuest) == Step::Release, "a room load releases the old room");
    check(rule::stepFor(kLoading, kLoading, true, kNoOwner, kGuest) == Step::None, "nothing is claimed while loading");
    check(rule::stepFor(kRoomA, kRoomB, true, kHost, kGuest) == Step::None, "a room the peer owns is not released");
    check(rule::stepFor(kRoomB, kRoomA, true, kNoOwner, kGuest) == Step::None, "a room that is not loaded is not claimed");
}

void testArrivalClaimAndObserver() {
    Machine host(kHost), guest(kGuest);
    const auto claim = host.tick(kRoomA);
    check(claim.size() == 1 && claim[0].type == wire::kMsgClaim, "the first arrival sends one CLAIM");
    check(host.table.isOwner(kRoomA) && host.table.epochOf(kRoomA) == 1, "the first arrival owns the room at epoch 1");
    guest.receive(claim, kHost);
    check(guest.table.ownerOf(kRoomA) == kHost, "the peer learns the owner");
    check(guest.tick(kRoomA).empty(), "the second arrival sends nothing and observes");
    check(!guest.table.isOwner(kRoomA), "the second arrival does not own the room");
    check(host.tick(kRoomA).empty(), "an unchanged room sends nothing");
}

void testSimultaneousArrivalSettlesOnTheLowerSlot() {
    Machine host(kHost), guest(kGuest);
    const auto fromHost = host.tick(kRoomA);
    const auto fromGuest = guest.tick(kRoomA);
    check(host.table.isOwner(kRoomA) && guest.table.isOwner(kRoomA), "each claims before hearing the other");
    host.receive(fromGuest, kGuest);
    guest.receive(fromHost, kHost);
    check(host.table.ownerOf(kRoomA) == kHost && guest.table.ownerOf(kRoomA) == kHost, "the lower slot owns it on both");
    check(agree(host, guest, kRoomA), "owner and epoch agree after the race");
    check(host.tick(kRoomA).empty() && guest.tick(kRoomA).empty(), "the settled race sends nothing more");
}

void testSimultaneousArrivalWithGuestFirstOnTheWire() {
    Machine host(kHost), guest(kGuest);
    const auto fromGuest = guest.tick(kRoomB);
    host.receive(fromGuest, kGuest);
    check(host.table.ownerOf(kRoomB) == kGuest, "a claim that arrives before the own tick is accepted");
    check(host.tick(kRoomB).empty(), "the host then observes");
    check(agree(host, guest, kRoomB), "both agree on the early claim");
}

void testReleaseThenOtherClaims() {
    Machine host(kHost), guest(kGuest);
    guest.receive(host.tick(kRoomA), kHost);
    check(guest.tick(kRoomA).empty(), "the second arrival observes");
    const auto released = host.tick(kRoomB);
    check(released.size() == 2 && released[0].type == wire::kMsgStop && released[1].type == wire::kMsgClaim,
          "walking out releases the old room and claims the new one");
    guest.receive(released, kHost);
    check(guest.table.ownerOf(kRoomA) == kNoOwner && guest.table.epochOf(kRoomA) == 2, "the release frees the room");
    const auto taken = guest.tick(kRoomA);
    check(taken.size() == 1 && taken[0].type == wire::kMsgClaim, "the one left in the room claims it");
    host.receive(taken, kGuest);
    check(guest.table.isOwner(kRoomA) && guest.table.epochOf(kRoomA) == 3, "the claim is the next epoch");
    check(agree(host, guest, kRoomA), "both agree after the hand over");
}

void testReleaseWhileLoadingAndOutOfRoom() {
    Machine host(kHost);
    host.tick(kRoomA);
    const auto loading = host.tick(kLoading);
    check(loading.size() == 1 && loading[0].type == wire::kMsgStop, "a room load releases the room");
    check(host.tick(kRoomA, false).empty(), "a character outside the loaded room claims nothing");
    check(host.table.ownerOf(kRoomA) == kNoOwner, "the room stays unowned");
}

void testPeerDropFreesRooms() {
    Machine host(kHost), guest(kGuest);
    guest.receive(host.tick(kRoomA), kHost);
    guest.table.dropSlot(kHost);
    check(guest.table.ownerOf(kRoomA) == kNoOwner && guest.table.epochOf(kRoomA) == 2, "a dropped peer's room is unowned");
    check(guest.tick(kRoomA).size() == 1 && guest.table.isOwner(kRoomA), "the one left claims it");
}

void testStaleAndForeignMessagesAreRefused() {
    Machine host(kHost), guest(kGuest);
    const auto first = host.tick(kRoomA);
    guest.receive(first, kHost);
    guest.receive(host.tick(kLoading), kHost);
    check(guest.receive(first[0], kHost) == Outcome::StaleEpoch, "an old claim is refused as stale");
    guest.receive(host.tick(kRoomB), kHost);
    const Outgoing foreignStop{wire::kMsgStop, {kRoomB, guest.table.epochOf(kRoomB), kHost, {}}};
    check(guest.receive(foreignStop, kGuest) == Outcome::WrongSender, "a stop from a machine that is not the owner is refused");
    const Outgoing claimForOther{wire::kMsgClaim, {kRoomA, 9, kHost, {}}};
    check(guest.receive(claimForOther, kGuest) == Outcome::WrongSender, "a guest's claim naming the host is refused");
    check(guest.table.ownerOf(kRoomB) == kHost, "the refused messages changed nothing");
}

void testClaimsReplayToAJoiner() {
    Machine host(kHost);
    host.tick(kRoomA);
    host.table.claim(kRoomB);
    const auto claims = host.table.claimsOf(kHost);
    check(claims.size() == 2, "the host owns two rooms");
    Machine joiner(kGuest);
    for (const auto& claim : claims) {
        joiner.receive(Outgoing{wire::kMsgClaim, claim}, kHost);
    }
    check(agree(host, joiner, kRoomA) && agree(host, joiner, kRoomB), "a joiner ends with the same table");
}

void testWire() {
    check(wire::kMsgClaim == 0x0166 && wire::kMsgDecline == 0x0167 && wire::kMsgStop == 0x0168, "the wire ids are 0x0166..0x0168");
    check(wire::tableKind(wire::kMsgClaim) == authority_wire::kMsgAuthClaim, "CLAIM maps to the table's claim");
    check(wire::tableKind(wire::kMsgDecline) == authority_wire::kMsgAuthDecline, "DECLINE maps to the table's decline");
    check(wire::tableKind(wire::kMsgStop) == authority_wire::kMsgAuthStop, "STOP maps to the table's stop");
    check(wire::tableKind(authority_wire::kMsgAuthClaim) == 0, "the table's own numbers are not wire ids");
    check(wire::isRoomAuthType(wire::kMsgStop) && !wire::isRoomAuthType(0x0169), "only the three ids are room authority");

    Machine host(kHost);
    const auto claim = host.tick(kRoomA);
    wire::AuthMessage decoded{};
    check(wire::decode(proto::bytesOf(claim[0].message), decoded) && decoded.id == kRoomA && decoded.owner == kHost &&
              decoded.epoch == 1, "a claim round-trips");
    const std::vector<uint8_t> shortPayload(sizeof(wire::AuthMessage) - 1);
    check(!wire::decode(shortPayload, decoded), "a short payload is refused");
    wire::AuthMessage bad = claim[0].message;
    bad.id = wire::kSceneIdLimit;
    check(!wire::decode(proto::bytesOf(bad), decoded), "a scene id out of range is refused");
    bad = claim[0].message;
    bad.owner = wire::kSlotLimit;
    check(!wire::decode(proto::bytesOf(bad), decoded), "an owner slot out of range is refused");
    bad = claim[0].message;
    bad.owner = kNoOwner;
    check(!wire::decode(proto::bytesOf(bad), decoded), "a message about no owner is refused");
    bad = claim[0].message;
    bad.epoch = 0;
    check(!wire::decode(proto::bytesOf(bad), decoded), "epoch 0 is refused");
}

void testView() {
    check(rule::viewOf(kNoOwner, kHost) == rule::View::None, "an unowned room shows none");
    check(rule::viewOf(kHost, kHost) == rule::View::Here, "an own room shows here");
    check(rule::viewOf(kGuest, kHost) == rule::View::Peer, "the peer's room shows peer");
    check(std::string(rule::name(rule::View::Here)) == "here", "view names");
}

}  // namespace

int main() {
    testStepRule();
    testArrivalClaimAndObserver();
    testSimultaneousArrivalSettlesOnTheLowerSlot();
    testSimultaneousArrivalWithGuestFirstOnTheWire();
    testReleaseThenOtherClaims();
    testReleaseWhileLoadingAndOutOfRoom();
    testPeerDropFreesRooms();
    testStaleAndForeignMessagesAreRefused();
    testClaimsReplayToAJoiner();
    testWire();
    testView();
    if (g_failures == 0) std::printf("room_authority_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
