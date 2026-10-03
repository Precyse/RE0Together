// fact_wire: the FACT_SET payload round trip and its rejections (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/fact_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

fact_wire::Entry entry(uint8_t kind, uint8_t flags, uint8_t seed, uint32_t value) {
    fact_wire::Entry e{};
    e.kind = kind;
    e.flags = flags;
    for (size_t i = 0; i < fact_wire::kUuidSize; ++i) e.uuid[i] = static_cast<uint8_t>(seed + i);
    e.value = value;
    return e;
}

}  // namespace

int main() {
    using namespace fact_wire;
    const std::vector<Entry> sent{entry(kKindBool, kFlagArg5, 1, 1), entry(kKindInt, kFlagArg5 | kFlagArg6, 40, 0xFFFFFFFEu)};
    const std::vector<uint8_t> payload = encode(sent);
    check(payload.size() == sizeof(Header) + 2 * sizeof(Entry), "payload is the header plus the entries");

    std::vector<Entry> got;
    check(decode(payload, got) && got.size() == 2, "a payload decodes");
    check(got.size() == 2 && std::memcmp(got.data(), sent.data(), 2 * sizeof(Entry)) == 0, "entries survive unchanged");
    check(got.size() == 2 && static_cast<int32_t>(got[1].value) == -2, "an int keeps its sign");

    check(decode(encode({}), got) && got.empty(), "an empty set is valid");

    std::vector<uint8_t> shortPayload(payload.begin(), payload.end() - 1);
    check(!decode(shortPayload, got) && got.empty(), "a truncated payload is rejected");
    std::vector<uint8_t> longPayload = payload;
    longPayload.push_back(0);
    check(!decode(longPayload, got), "a payload with a stray byte is rejected");
    check(!decode(std::span<const uint8_t>(payload.data(), 3), got), "a payload shorter than the header is rejected");

    std::vector<Entry> badKind{entry(7, 0, 1, 0)};
    check(!decode(encode(badKind), got) && got.empty(), "an unknown kind is rejected whole");

    std::vector<Entry> tooMany(kMaxEntries + 1, entry(kKindBool, 0, 1, 1));
    check(!decode(encode(tooMany), got), "more than the limit is rejected");
    std::vector<Entry> full(kMaxEntries, entry(kKindInt, 0, 1, 5));
    check(decode(encode(full), got) && got.size() == kMaxEntries, "exactly the limit is accepted");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
