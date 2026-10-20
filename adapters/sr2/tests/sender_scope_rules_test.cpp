// The sender scope rules (no game): which net id a look message must name (milestone 9), and when a look is not applied
// again (milestone 11 B2).
#include <cstdio>

#include "../src/fnv1a.h"
#include "../src/sender_scope_rules.h"
#include "check.h"

namespace {

using namespace sender_scope_rules;

constexpr uintptr_t kSession = 0x20000000;
constexpr uintptr_t kOtherSession = 0x20100000;
constexpr size_t kRemotes = 3;

}  // namespace

int main() {
    using sender_scope_rules::kNoNetId;
    using sender_scope_rules::lookNetId;

    check(lookNetId(0x1002, 0x1002) == 0x1002, "a payload that names the sender's human stays as it is (the 2-player case)");
    check(lookNetId(0x3004, 0x2004) == 0x2004, "a payload that names another player is made to name the sender's human");
    check(lookNetId(0x2004, kNoNetId) == 0x2004, "a sender with no net id yet keeps the payload's");
    check(lookNetId(kNoNetId, 0x2004) == 0x2004, "an empty payload id is filled in");

    const uint8_t foobar[] = {'f', 'o', 'o', 'b', 'a', 'r'};
    const uint8_t a = 'a';
    check(fnv1a::hash64(nullptr, 0) == 0xCBF29CE484222325ull && fnv1a::hash64(&a, 1) == 0xAF63DC4C8601EC8Cull &&
              fnv1a::hash64(foobar, sizeof(foobar)) == 0x85944171F73967E8ull,
          "FNV-1a 64 gives the published values for \"\", \"a\" and \"foobar\"");
    check(fnv1a::hash64(foobar + 3, 3, fnv1a::hash64(foobar, 3)) == fnv1a::hash64(foobar, sizeof(foobar)), "a hash continues from its state");

    const uint8_t idA[] = {0x01, 0x10, 'l', 'o', 'o', 'k'};
    const uint8_t idB[] = {0x02, 0x20, 'l', 'o', 'o', 'k'};
    const uint8_t other[] = {0x01, 0x10, 'l', 'o', 'o', 'm'};
    check(lookDigest(idA, sizeof(idA)) == lookDigest(idB, sizeof(idB)), "the digest leaves out the net id word");
    check(lookDigest(idA, sizeof(idA)) != lookDigest(other, sizeof(other)), "a changed look has another digest");
    check(!lookCacheable(0) && !lookCacheable(kNetIdBytes) && lookCacheable(kNetIdBytes + 1) && lookCacheable(kMaxLookPayload) &&
              !lookCacheable(kMaxLookPayload + 1),
          "a payload is digested when it has a look behind the id and fits a message");

    const LookKey key{kSession, 1, LookKind::Components};
    const Look look{0x1234, 77};
    LookCache cache;
    check(!cache.unchanged(key, look, kRemotes), "the first look of a sender is applied");
    check(cache.unchanged(key, look, kRemotes), "the same look again is dropped");
    check(!cache.unchanged(key, {0x1234, 78}, kRemotes), "a changed look passes");
    check(cache.unchanged(key, {0x1234, 78}, kRemotes), "and is the one that is dropped from then on");
    check(!cache.unchanged(key, {0x5678, 78}, kRemotes), "the same look for a new human handle (a rejoin) passes");
    check(!cache.unchanged({kSession, 2, LookKind::Components}, {0x5678, 78}, kRemotes), "another sender slot is independent");
    check(!cache.unchanged({kSession, 1, LookKind::Morph}, {0x5678, 78}, kRemotes), "the other message of the same slot is independent");
    check(cache.unchanged({kSession, 2, LookKind::Components}, {0x5678, 78}, kRemotes), "each slot keeps its own look");
    check(!cache.unchanged({kOtherSession, 1, LookKind::Components}, {0x5678, 78}, kRemotes) &&
              !cache.unchanged({kSession, 1, LookKind::Components}, {0x5678, 78}, kRemotes),
          "a new session forgets every look");

    LookCache lone;
    const LookKey loneKey{kSession, 1, LookKind::Components};
    check(!lone.unchanged(loneKey, look, 1) && !lone.unchanged(loneKey, look, 1) && !lone.unchanged(loneKey, look, 0),
          "with one remote (the 2-player game) nothing is dropped");
    check(!lone.unchanged({kSession, kLookSlots, LookKind::Morph}, look, kRemotes) && !lone.unchanged({kSession, kLookSlots, LookKind::Morph}, look, kRemotes),
          "a sender slot above 7 is never cached");
    return g_failures == 0 ? 0 : 1;
}
