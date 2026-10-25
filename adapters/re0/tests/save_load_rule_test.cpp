// Checks which save slot a load request really loads (no game): the launcher's order parsing, the redirect and its hash
// guard. Exit 0 when every check passes.
#include <cstdio>
#include <vector>

#include "../src/fnv1a32.h"
#include "../src/save_load_rule.h"

namespace {

namespace rule = save_load_rule;
using rule::Reason;

constexpr int32_t kOrdered = 7;
constexpr int32_t kOther = 3;
constexpr int32_t kCoop = 19;
constexpr uint32_t kHash = 0xdeadbeefu;
constexpr int32_t kSystemSlot = 0x15;  // the title's boot read of the system data
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Order order(std::optional<uint32_t> hash = kHash) {
    rule::Order o;
    o.slot = kOrdered;
    o.hash = hash;
    return o;
}

void testParse() {
    check(!rule::parse("", "").order.active() && rule::parse("", "").valid, "no load_slot is no order and not an error");
    check(!rule::parse("", "cafe").order.active() && rule::parse("", "cafe").valid, "a hash without a slot is no order");
    const rule::Parsed ok = rule::parse("7", "DEADBEEF");
    check(ok.valid && ok.order.slot == kOrdered && ok.order.hash == kHash, "slot 7 with an upper-case hash parses");
    check(rule::parse("0", "0").order.hash == 0u && rule::parse("19", "").order.slot == kCoop, "the slot range ends 0 and 19 parse");
    check(rule::parse("19", "").valid && !rule::parse("19", "").order.hash, "an empty hash leaves the slot unchecked");
    for (const char* bad : {"20", "-1", "x", "7 ", "007", "1e1", " 7"}) {
        const rule::Parsed p = rule::parse(bad, "");
        check(!p.valid && !p.order.active(), "an unusable load_slot switches the order off");
    }
    for (const char* bad : {"xyz", "123456789", "0x12", "-1"}) {
        const rule::Parsed p = rule::parse("7", bad);
        check(!p.valid && !p.order.active(), "an unusable load_hash switches the order off");
    }
}

void testRedirect() {
    const rule::Pick first = rule::pick(order(), false, kOther, rule::kUnknownSlot);
    check(first.slot == kOrdered && first.reason == Reason::Order, "the first load of the session loads the ordered slot");
    check(rule::pick(order(), false, kOrdered, rule::kUnknownSlot).reason == Reason::None, "the ordered slot itself passes");
    check(rule::pick(rule::Order{}, false, kOther, rule::kUnknownSlot).reason == Reason::None, "no order: the player's slot");
    check(rule::pick(order(), false, kSystemSlot, rule::kUnknownSlot).reason == Reason::None, "the system data load passes");
    check(rule::pick(order(), false, rule::kUnknownSlot, rule::kUnknownSlot).slot == rule::kUnknownSlot, "no slot passes");
}

void testSessionSlot() {
    const rule::Pick later = rule::pick(order(), false, kOther, kCoop);
    check(later.slot == kCoop && later.reason == Reason::SessionSlot, "after a save the host's loads return to the co-op slot");
    check(rule::pick(order(), false, kCoop, kCoop).reason == Reason::None, "the session slot itself passes");
    const rule::Pick guest = rule::pick(order(), true, kOther, kCoop);
    check(guest.slot == kCoop && guest.reason == Reason::HostSlot, "a guest follows the host's slot");
    check(rule::pick(rule::Order{}, true, kOther, kCoop).reason == Reason::HostSlot, "a guest follows the host without an order");
    check(rule::pick(rule::Order{}, false, kOther, kCoop).reason == Reason::None, "a host without an order is free");
    check(rule::pick(order(), true, kOther, rule::kUnknownSlot).reason == Reason::Order, "a guest before the host plays loads the order");
}

void testHashGuard() {
    check(rule::hashAccepts(order(), kHash), "the ordered bytes are accepted");
    check(!rule::hashAccepts(order(), kHash + 1), "other bytes are refused");
    check(!rule::hashAccepts(order(), std::nullopt), "bytes that cannot be read are refused");
    check(rule::hashAccepts(order(std::nullopt), kHash + 1) && rule::hashAccepts(order(std::nullopt), std::nullopt),
          "an order without a hash trusts the slot");
}

void testFnv() {
    check(fnv1a32::hash({}) == fnv1a32::kOffsetBasis, "the empty hash is the offset basis");
    const std::vector<uint8_t> a = {'a'};
    check(fnv1a32::hash(a) == 0xe40c292cu, "FNV-1a 32 of 'a' is the published vector");
    const std::vector<uint8_t> foobar = {'f', 'o', 'o', 'b', 'a', 'r'};
    check(fnv1a32::hash(foobar) == 0xbf9cf968u, "FNV-1a 32 of 'foobar' is the published vector");
    const std::vector<uint8_t> foo = {'f', 'o', 'o'};
    const std::vector<uint8_t> bar = {'b', 'a', 'r'};
    check(fnv1a32::hash(bar, fnv1a32::hash(foo)) == fnv1a32::hash(foobar), "a hash continues from its state");
}

}  // namespace

int main() {
    testParse();
    testRedirect();
    testSessionSlot();
    testHashGuard();
    testFnv();
    if (g_failures == 0) std::printf("save_load_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
