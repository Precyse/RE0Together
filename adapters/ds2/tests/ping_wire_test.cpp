// ping_wire: the PING_SET payload round trip and its rejections (no game).
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/ping_wire.h"

namespace {

using namespace ping_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Ping ping(uint64_t id) {
    Ping out{};
    out.id = id;
    out.position[0] = 1500.5f + static_cast<float>(id);
    out.position[1] = -2000.25f;
    out.position[2] = 312.0f;
    out.group = 2;
    out.index = 7;
    out.sequence = 1;
    return out;
}

std::vector<Ping> pings(size_t count) {
    std::vector<Ping> out;
    for (size_t i = 0; i < count; ++i) out.push_back(ping(10 + i * 3));
    return out;
}

bool rejects(const Ping& bad) {
    std::vector<Ping> got;
    return !decode(encode(std::vector<Ping>{bad}), got);
}

}  // namespace

int main() {
    check(kMsgPingSet == 0x013A, "the message id is 0x013A");

    for (const size_t count : {size_t{0}, size_t{1}, kMaxPings}) {
        const std::vector<Ping> sent = pings(count);
        const std::vector<uint8_t> payload = encode(sent);
        std::vector<Ping> got = pings(2);
        check(payload.size() == sizeof(PingSetHeader) + count * sizeof(Ping), "the payload is the header and the pings");
        check(decode(payload, got) && sameSet(got, sent), "a set round-trips");
    }

    std::vector<Ping> got;
    std::vector<uint8_t> payload = encode(pings(2));
    payload.pop_back();
    check(!decode(payload, got), "a size of 4 + 23 per ping is rejected");
    payload = encode(pings(1));
    payload.push_back(0);
    check(!decode(payload, got), "a payload one byte long is rejected");
    check(!decode(std::vector<uint8_t>{1, 0, 0}, got), "a payload shorter than the header is rejected");
    check(!decode(encode(pings(kMaxPings + 1)), got), "a count of 101 is rejected");

    payload = encode(pings(1));
    payload[2] = 1;
    check(!decode(payload, got), "a set header reserved field is rejected");

    Ping bad = ping(1);
    bad.position[1] = std::numeric_limits<float>::quiet_NaN();
    check(rejects(bad), "a NaN position is rejected");
    bad = ping(1);
    bad.position[0] = std::numeric_limits<float>::infinity();
    check(rejects(bad), "an infinite position is rejected");
    bad = ping(1);
    bad.position[2] = -(kMaxWorldMetres + 1.0f);
    check(rejects(bad), "a position beyond 100 km is rejected");
    bad.position[2] = kMaxWorldMetres;
    check(!rejects(bad), "a position at 100 km is accepted");
    bad = ping(1);
    bad.group = kMaxGroup + 1;
    check(rejects(bad), "group 5 is rejected");
    bad = ping(1);
    bad.index = kMaxIndex + 1;
    check(rejects(bad), "index 20 is rejected");
    bad = ping(1);
    bad.sequence = 0;
    check(rejects(bad), "sequence 0 is rejected");
    bad = ping(1);
    bad.reserved = 1;
    check(rejects(bad), "a set ping reserved byte is rejected");
    check(!decode(encode(std::vector<Ping>{ping(4), ping(9), ping(4)}), got), "two pings with one id are rejected");

    // The bytes tools/ds2/fake_launcher.py sends for --ping "3,0,1" from (100, 200, 10).
    const std::vector<uint8_t> fromFakeLauncher = {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                   0xCE, 0x42, 0x00, 0x00, 0x48, 0x43, 0x00, 0x00, 0x30, 0x41, 0x00, 0x00, 0x01, 0x00};
    check(decode(fromFakeLauncher, got) && got.size() == 1 && got[0].id == 1 && got[0].position[0] == 103.0f &&
              got[0].position[1] == 200.0f && got[0].position[2] == 11.0f && got[0].sequence == 1,
          "the fake launcher's PING_SET decodes");

    std::vector<Ping> unsorted = pings(3);
    std::vector<Ping> sorted = unsorted;
    std::swap(unsorted[0], unsorted[2]);
    sortById(unsorted);
    check(sameSet(unsorted, sorted), "sorting by id gives one order");
    check(!sameSet(pings(2), pings(3)), "sets of different sizes differ");
    std::vector<Ping> moved = pings(3);
    moved[1].position[0] += 1.0f;
    check(!sameSet(moved, pings(3)), "a moved position is a different set");
    moved = pings(3);
    moved[2].id += 1;
    check(!sameSet(moved, pings(3)), "a changed id is a different set");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
