// struct_wire: the STRUCT_CREATE / STRUCT_REMOVE payload round trip and its rejections (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/struct_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct_wire::Placed ladder() {
    struct_wire::Placed placed{};
    placed.create.kind = struct_wire::kKindLadder;
    placed.create.subKind = 3;
    placed.create.level = 1;
    placed.create.tailBytes = struct_wire::kindInfo(struct_wire::kKindLadder)->tailBytes;
    placed.create.id = 11659;
    placed.create.durability = 360000.0f;
    for (size_t i = 0; i < sizeof(placed.create.transform); ++i) placed.create.transform[i] = static_cast<uint8_t>(i);
    placed.tail.assign(struct_wire::kindInfo(struct_wire::kKindLadder)->tailBytes, 0x5A);
    return placed;
}

}  // namespace

int main() {
    using namespace struct_wire;
    const Placed sent = ladder();
    const std::vector<uint8_t> payload = encode(sent);
    check(payload.size() == sizeof(Create) + kindInfo(kKindLadder)->tailBytes, "payload is the fixed part plus the tail");

    Placed got{};
    check(decode(payload, got), "a payload decodes");
    check(std::memcmp(&got.create, &sent.create, sizeof(Create)) == 0 && got.tail == sent.tail, "every field survives");

    std::vector<uint8_t> shorter(payload.begin(), payload.end() - 1);
    check(!decode(shorter, got), "a short payload is rejected");
    std::vector<uint8_t> longer = payload;
    longer.push_back(0);
    check(!decode(longer, got), "a long payload is rejected");

    Placed wrongKind = sent;
    wrongKind.create.kind = 8;  // an enemy post: not player-buildable
    check(!decode(encode(wrongKind), got), "a kind whose fields are not known is rejected");
    Placed anchor{};
    anchor.create.kind = 11;
    anchor.create.tailBytes = 8;
    anchor.tail.assign(8, 0x11);
    check(decode(encode(anchor), got) && got.create.kind == 11 && got.tail.size() == 8, "the climbing anchor (field rope) round trips");
    Placed wrongTail = sent;
    wrongTail.create.tailBytes = 8;
    wrongTail.tail.resize(8);
    check(!decode(encode(wrongTail), got), "a ladder with a different tail length is rejected");

    Remove removal{11659, 1, {}};
    Remove back{};
    check(decode(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&removal), sizeof(removal)), back) &&
              back.id == 11659 && back.factor == 1,
          "a removal round trips");
    check(!decode(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&removal), 5), back), "a short removal is rejected");

    Placed request = sent;
    request.create.id = kAssignId;
    check(decode(encode(request), got) && got.create.id == kAssignId, "a guest's request (id left to the host) round trips");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
