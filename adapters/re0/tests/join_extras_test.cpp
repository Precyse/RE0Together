// Checks the JOIN_EXTRAS wire format (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/session_wire.h"

namespace {

namespace wire = session_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

room_authority_wire::AuthMessage claim(uint64_t scene, uint32_t epoch, uint8_t owner) { return {scene, epoch, owner, {}}; }

prop_wire::Commit commitAt(uint16_t scene, uint8_t unit) {
    prop_wire::Commit commit{};
    commit.serial = 4;
    commit.scene = scene;
    commit.kind = prop_wire::kObstacleKind;
    commit.unitId = unit;
    commit.pos[0] = 10.0f;
    commit.pos[1] = 0.0f;
    commit.pos[2] = -20.0f;
    commit.yaw = 1.5f;
    commit.committer = 1;
    return commit;
}

wire::ItemTake takeAt(uint16_t scene, uint32_t item) { return {scene, 0, item, {100.0f, 0.0f, 200.0f}}; }

wire::JoinExtras sample() {
    wire::JoinExtras extras;
    extras.snapshotSerial = 5;
    extras.flagSerial = 41;
    extras.worldEpoch = 6;
    extras.claims = {claim(0x2d, 2, 0), claim(0x08, 1, 0)};
    extras.props = {commitAt(0x2d, 1)};
    extras.items = {takeAt(0x0109, 0x2b), takeAt(0x010b, 0x14), takeAt(0x010b, 0x0f)};
    return extras;
}

std::vector<uint8_t> raw(const wire::JoinExtras& extras) { return wire::encode(extras); }

void testRoundTrip() {
    const wire::JoinExtras sent = sample();
    const auto bytes = raw(sent);
    check(bytes.size() == sizeof(wire::ExtrasHeader) + 2 * 16 + 28 + 3 * 20,
          "the size is header + 16 per claim + 28 per commit + 20 per item");
    const auto got = wire::decode(bytes);
    check(got.has_value(), "a good message decodes");
    if (!got) return;
    check(got->snapshotSerial == 5 && got->flagSerial == 41, "the serials survive");
    check(got->worldEpoch == 6, "the host's world epoch survives");
    check(got->claims.size() == 2 && got->claims[0].id == 0x2d && got->claims[0].epoch == 2 && got->claims[1].id == 0x08,
          "the claims survive in order");
    check(got->props.size() == 1 && got->props[0].scene == 0x2d && got->props[0].unitId == 1 && got->props[0].yaw == 1.5f &&
              got->props[0].committer == 1,
          "the prop commit survives");
    check(got->items.size() == 3 && got->items[1].itemId == 0x14 && got->items[2].scene == 0x010b,
          "the items survive in order, their rooms (stage << 8 | room, above the scene id limit) too");
    const item_ledger_rule::Key key = wire::keyOf(got->items[0]);
    check(key.scene == 0x0109 && key.itemId == 0x2b && key.pos[2] == 200.0f, "an item becomes a ledger key");
    const wire::ItemTake back = wire::takeOf(key);
    check(back.scene == 0x0109 && back.itemId == 0x2b && back.pos[0] == 100.0f && back.reserved == 0, "and back");
}

void testEmpty() {
    wire::JoinExtras extras;
    extras.snapshotSerial = 1;
    extras.worldEpoch = 1;
    const auto got = wire::decode(raw(extras));
    check(got && got->claims.empty() && got->props.empty() && got->items.empty() && got->flagSerial == 0,
          "a message with no lists decodes");
    check(raw(extras).size() == sizeof(wire::ExtrasHeader), "and is only the header");
}

void testSizeRefusals() {
    auto bytes = raw(sample());
    check(!wire::decode(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)), "a short message is refused");
    bytes.push_back(0);
    check(!wire::decode(bytes), "a long message is refused");
    check(!wire::decode(std::vector<uint8_t>(sizeof(wire::ExtrasHeader) - 1)), "less than a header is refused");
    wire::ExtrasHeader header{};
    header.snapshotSerial = 1;
    header.worldEpoch = 1;
    header.items = static_cast<uint16_t>(wire::kMaxItems + 1);
    std::vector<uint8_t> big(sizeof(header) + header.items * sizeof(wire::ItemTake));
    std::memcpy(big.data(), &header, sizeof(header));
    check(!wire::decode(big), "more items than the ledger holds are refused");
}

void testHeaderRefusals() {
    auto bytes = raw(sample());
    wire::ExtrasHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    wire::ExtrasHeader noSerial = header;
    noSerial.snapshotSerial = 0;
    std::memcpy(bytes.data(), &noSerial, sizeof(header));
    check(!wire::decode(bytes), "snapshot serial 0 is refused");
    wire::ExtrasHeader noEpoch = header;
    noEpoch.worldEpoch = 0;
    std::memcpy(bytes.data(), &noEpoch, sizeof(header));
    check(!wire::decode(bytes), "world epoch 0 is refused: the host's epoch is never 0");
    wire::ExtrasHeader lying = header;
    lying.claims = 3;
    std::memcpy(bytes.data(), &lying, sizeof(header));
    check(!wire::decode(bytes), "counts that do not add up to the size are refused");
}

void testEntryRefusals() {
    wire::JoinExtras extras = sample();
    extras.claims[0].epoch = 0;
    check(!wire::decode(raw(extras)), "a claim of epoch 0 is refused");
    extras = sample();
    extras.claims[1].id = wire::kSceneLimit;
    check(!wire::decode(raw(extras)), "a claim of a scene the game does not have is refused");
    extras = sample();
    extras.claims[0].owner = 0xff;
    check(!wire::decode(raw(extras)), "a claim for no owner is refused");
    extras = sample();
    extras.props[0].unitId = prop_wire::kUnitCount;
    check(!wire::decode(raw(extras)), "a commit of a box the game does not have is refused");
    extras = sample();
    extras.props[0].yaw = std::numeric_limits<float>::quiet_NaN();
    check(!wire::decode(raw(extras)), "a commit with a non-finite pose is refused");
    extras = sample();
    extras.items[0].itemId = item_wire::kMaxItemId + 1;
    check(!wire::decode(raw(extras)), "an item the game does not have is refused");
    extras = sample();
    extras.items[2].pos[1] = std::numeric_limits<float>::infinity();
    check(!wire::decode(raw(extras)), "an item at a non-finite position is refused");
    extras = sample();
    extras = sample();
    extras.items[1].reserved = 1;
    check(!wire::decode(raw(extras)), "a reserved item field must be zero");
}

void testEncodeCutsToLimits() {
    wire::JoinExtras extras;
    extras.snapshotSerial = 2;
    extras.worldEpoch = 1;
    extras.props.assign(wire::kMaxProps + 5, commitAt(0x2d, 0));
    extras.items.assign(wire::kMaxItems + 5, takeAt(0x0109, 0x2b));
    const auto got = wire::decode(raw(extras));
    check(got && got->props.size() == wire::kMaxProps && got->items.size() == wire::kMaxItems,
          "lists beyond their limits are cut to them, so the message still decodes");
}

}  // namespace

int main() {
    testRoundTrip();
    testEmpty();
    testSizeRefusals();
    testHeaderRefusals();
    testEntryRefusals();
    testEncodeCutsToLimits();
    if (g_failures == 0) std::printf("join_extras_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
