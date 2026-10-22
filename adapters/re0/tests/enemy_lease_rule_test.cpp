// Checks the lease table (no game): one lease per enemy, roles, tokens, who streams, holder pose acceptance with its
// token / HP / displacement bounds, and the lapse of an unanswered or silent lease. Exit 0 when every check passes.
#include <cstdint>
#include <cstdio>

#include "../src/enemy_lease_rule.h"

namespace {

namespace rule = enemy_lease_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Lease away(uint8_t slot, uint32_t token) {
    rule::Lease lease;
    lease.token = token;
    lease.slot = slot;
    lease.peer = 1;
    lease.role = rule::Role::Away;
    lease.hp = 80;
    lease.startedMs = 1000;
    return lease;
}

void testTable() {
    rule::LeaseTable table;
    check(table.roleOf(3) == rule::Role::None, "an empty table has no roles");
    check(table.add(away(3, 0x10001)), "a lease is added");
    check(!table.add(away(3, 0x10002)), "a second lease on the enemy is refused");
    check(!table.add(away(37, 0x10003)), "a slot past the pool is refused");
    check(table.roleOf(3) == rule::Role::Away && table.any(rule::Role::Away) && !table.any(rule::Role::Held), "the role is Away");
    rule::Lease held = away(4, 0x10005);
    held.role = rule::Role::Held;
    check(table.add(held) && table.any(rule::Role::Held), "a held lease is added");
    check(!table.remove(3, 0x10002).has_value(), "a wrong token removes nothing");
    check(table.remove(3, 0x10001).has_value() && table.roleOf(3) == rule::Role::None, "the right token removes it");
    table.clear();
    check(!table.any(rule::Role::Held), "clear empties the table");
}

void testTokens() {
    check(rule::nextToken(0) == 1, "the first token is 1");
    check(rule::nextToken(0xfffe) == 0xffff, "0xffff is a token");
    check(rule::nextToken(0xffff) == 0x10001, "a token whose low 16 bits are 0 is skipped");
    check((rule::nextToken(0xffffffffu) & 0xffff) != 0, "the 32-bit wrap keeps the low bits non-zero");
}

void testStreams() {
    check(rule::streams(true, rule::Role::None), "the room's simulator streams an enemy it keeps");
    check(!rule::streams(true, rule::Role::Away), "it stops streaming an enemy it gave away");
    check(rule::streams(true, rule::Role::Held), "it streams one it holds");
    check(!rule::streams(false, rule::Role::None), "an observer streams nothing it does not hold");
    check(rule::streams(false, rule::Role::Held), "an observer streams a held enemy");
}

void testPoses() {
    rule::LeaseTable table;
    rule::Lease lease = away(5, 0x20007);
    lease.lastPos[0] = 100;
    table.add(lease);
    const float close[3] = {200, 0, 0};
    const float distant[3] = {9000, 0, 0};
    check(table.offerPose(6, 7, close, 50, 1100) == rule::PoseVerdict::NoLease, "a pose for an enemy without a lease");
    check(table.offerPose(5, 8, close, 50, 1100) == rule::PoseVerdict::WrongToken, "a pose naming another token");
    check(table.offerPose(5, 7, distant, 50, 1100) == rule::PoseVerdict::Rejected, "a jump beyond the bound");
    check(table.offerPose(5, 7, close, 90, 1100) == rule::PoseVerdict::Rejected, "an HP above the lease's");
    check(table.offerPose(5, 7, close, 60, 1100) == rule::PoseVerdict::Accepted, "a good pose");
    check(table.find(5)->heard && table.find(5)->hp == 60, "the accepted HP lowers the ceiling");
    check(table.offerPose(5, 7, close, 70, 1150) == rule::PoseVerdict::Rejected, "an HP back up is refused");
    check(table.offerPose(5, 7, distant, 50, 1150) == rule::PoseVerdict::Rejected, "the baseline moved only with accepted poses");
    check(table.offerPose(5, 7, distant, 50, 3000) == rule::PoseVerdict::Accepted, "a long silence widens the bound");
}

void testLapse() {
    rule::LeaseTable table;
    table.add(away(1, 0x30001));
    table.add(away(2, 0x30002));
    const float pos[3] = {0, 0, 0};
    table.offerPose(2, 2, pos, 50, 1400);
    check(table.lapsed(2400).empty(), "within the expiry nothing lapses");
    auto out = table.lapsed(2501);
    check(out.size() == 1 && out[0].slot == 1 && out[0].reason == contact_rule::End::Expired, "an unanswered lease expires");
    check(table.roleOf(1) == rule::Role::None && table.roleOf(2) == rule::Role::Away, "only the lapsed lease is removed");
    out = table.lapsed(3901);
    check(out.size() == 1 && out[0].slot == 2 && out[0].reason == contact_rule::End::Silent, "a silent stream lapses");
    rule::Lease held = away(9, 0x30009);
    held.role = rule::Role::Held;
    table.add(held);
    check(table.lapsed(900000).empty(), "a held lease never lapses here");
}

}  // namespace

int main() {
    testTable();
    testTokens();
    testStreams();
    testPoses();
    testLapse();
    if (g_failures == 0) std::printf("enemy_lease_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
