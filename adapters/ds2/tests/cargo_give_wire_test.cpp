// cargo_transfer wire: the CARGO_ADD / CARGO_GIVE payload and the ids of the cargo messages (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "cargo_transfer.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> bytes(const cargo_transfer::CargoAdd& add) {
    std::vector<uint8_t> out(sizeof(add));
    std::memcpy(out.data(), &add, sizeof(add));
    return out;
}

}  // namespace

int main() {
    using namespace cargo_transfer;
    check(kMsgCargoGive == 0x0133, "CARGO_GIVE is 0x0133");
    check(kMsgCargoGive != kMsgCargoAdd && kMsgCargoGive != kMsgCargoAsk && kMsgCargoGive != kMsgCargoTake &&
              kMsgCargoGive != kMsgCargoList,
          "CARGO_GIVE differs from the other cargo messages");

    const CargoAdd order{710, 2, {}, 0.75f, 0, 0x0200000100000138ull, 0x42};
    CargoAdd got{};
    check(decodeAdd(bytes(order), got), "a CARGO_GIVE payload decodes");
    check(got.type == 710 && got.category == 2 && got.durability == 0.75f && got.orderId == order.orderId &&
              got.secondId == 0x42,
          "the kind, category, durability and order link survive");

    const CargoAdd plain{12, 0, {}, 0.0f, 0, 0, 0};
    check(decodeAdd(bytes(plain), got) && got.orderId == 0 && got.type == 12, "a plain piece decodes");

    std::vector<uint8_t> shortPayload = bytes(order);
    shortPayload.pop_back();
    CargoAdd untouched = plain;
    check(!decodeAdd(shortPayload, untouched) && untouched.type == plain.type, "a short payload is refused and leaves the output");
    std::vector<uint8_t> longPayload = bytes(order);
    longPayload.push_back(0);
    check(!decodeAdd(longPayload, untouched), "a long payload is refused");
    check(!decodeAdd({}, untouched), "an empty payload is refused");

    std::printf("%s\n", g_failures ? "FAILED" : "all passed");
    return g_failures ? 1 : 0;
}
