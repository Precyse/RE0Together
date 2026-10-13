// crater_wire: the CRATER_SET payload round trip and its rejections (no game).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/crater_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

crater_wire::Crater crater(uint8_t seed) {
    crater_wire::Crater out{};
    for (size_t i = 0; i < crater_wire::kUuidSize; ++i) out.uuid[i] = static_cast<uint8_t>(seed + i);
    out.position[0] = 100.5 + seed;
    out.position[1] = -20.25;
    out.position[2] = 3.0;
    out.radius = 42.0f;
    out.area = 7;
    out.kind = 1;
    return out;
}

std::vector<crater_wire::Crater> craters(size_t count) {
    std::vector<crater_wire::Crater> out;
    for (size_t i = 0; i < count; ++i) out.push_back(crater(static_cast<uint8_t>(i * 3)));
    return out;
}

bool rejects(const crater_wire::Crater& bad) {
    std::vector<crater_wire::Crater> got;
    const std::vector<crater_wire::Crater> set{bad};
    return !crater_wire::decode(crater_wire::encode(set), got);
}

}  // namespace

int main() {
    using namespace crater_wire;

    check(kMsgCraterSet == 0x0137, "the message id is 0x0137");

    for (const size_t count : {size_t{0}, size_t{1}, kMaxCraters}) {
        const std::vector<Crater> sent = craters(count);
        const std::vector<uint8_t> payload = encode(sent);
        std::vector<Crater> got = craters(2);
        check(payload.size() == sizeof(CraterSetHeader) + count * sizeof(Crater), "the payload is the header and the craters");
        check(decode(payload, got) && sameSet(got, sent), "a set round-trips");
    }

    std::vector<Crater> got;
    std::vector<uint8_t> payload = encode(craters(2));
    payload.pop_back();
    check(!decode(payload, got), "a short payload is rejected");
    payload = encode(craters(2));
    payload.push_back(0);
    check(!decode(payload, got), "a long payload is rejected");
    check(!decode(std::vector<uint8_t>{1}, got), "a payload shorter than the header is rejected");

    const std::vector<Crater> tooMany = craters(kMaxCraters + 1);
    check(!decode(encode(tooMany), got), "a count over the maximum is rejected");

    payload = encode(craters(1));
    payload[2] = 1;
    check(!decode(payload, got), "a set reserved header field is rejected");

    Crater bad = crater(1);
    bad.position[1] = std::numeric_limits<double>::quiet_NaN();
    check(rejects(bad), "a NaN position is rejected");
    bad = crater(1);
    bad.position[0] = std::numeric_limits<double>::infinity();
    check(rejects(bad), "an infinite position is rejected");
    bad = crater(1);
    bad.radius = 0;
    check(rejects(bad), "radius 0 is rejected");
    bad.radius = kMaxRadius + 1;
    check(rejects(bad), "a radius over the maximum is rejected");
    bad.radius = kMaxRadius;
    std::vector<Crater> edge;
    check(decode(encode(std::vector<Crater>{bad}), edge) && edge.size() == 1, "the maximum radius is accepted");
    bad = crater(1);
    bad.kind = kMaxKind + 1;
    check(rejects(bad), "kind 16 is rejected");
    bad = crater(1);
    bad.reserved = 1;
    check(rejects(bad), "a set reserved crater field is rejected");

    std::vector<Crater> unsorted = craters(3);
    std::vector<Crater> sorted = unsorted;
    std::swap(unsorted[0], unsorted[2]);
    sortByUuid(unsorted);
    check(sameSet(unsorted, sorted), "sorting by UUID gives one order");
    check(!sameSet(craters(2), craters(3)), "sets of different sizes differ");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
