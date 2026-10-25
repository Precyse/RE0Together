// Checks the world digest canary (no game): the wire format, the flag and authority digests, and when two digests that
// differ are reported. Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/world_digest_rule.h"

namespace {

namespace rule = world_digest_rule;
namespace wire = world_digest_wire;
using flag_diff::Words;
using rule::Event;
using rule::Scope;

constexpr uint32_t kSerial = 40;
constexpr uint32_t kStep = 1000;  // the digest arrives once a second
constexpr uint32_t kPresenceBits[] = {0x37, 0x93, 0x10d};
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

std::vector<uint8_t> bytesOf(const wire::WorldDigest& digest) {
    std::vector<uint8_t> out(sizeof(digest));
    std::memcpy(out.data(), &digest, sizeof(digest));
    return out;
}

wire::WorldDigest sample() {
    wire::WorldDigest digest{};
    digest.flagSerial = kSerial;
    digest.flagCrc = 0x11111111;
    digest.enemyFlagCrc = 0x22222222;
    digest.actFlagCrc = 0x33333333;
    digest.authorityHash = 0x44444444;
    digest.loadSerial = 9;
    digest.flags = wire::kCaughtUp;
    digest.scene = 0x0a05;
    digest.senderMs = 123456;
    return digest;
}

void setBit(Words& words, uint32_t bit) { words[bit / 32] |= 1u << bit % 32; }

void testWire() {
    check(wire::kMsgWorldDigest == 0x0178, "WORLD_DIGEST is 0x0178");
    const wire::WorldDigest sent = sample();
    const auto got = wire::decode(bytesOf(sent));
    check(got.has_value(), "a good digest decodes");
    if (!got) return;
    check(got->flagSerial == kSerial && got->flagCrc == sent.flagCrc && got->enemyFlagCrc == sent.enemyFlagCrc &&
              got->actFlagCrc == sent.actFlagCrc && got->authorityHash == sent.authorityHash,
          "the digests survive");
    check(got->loadSerial == 9 && got->scene == 0x0a05 && got->senderMs == 123456, "the counters survive");
    check(wire::caughtUp(*got), "the caught up bit survives");
}

void testWireRefusals() {
    std::vector<uint8_t> bytes = bytesOf(sample());
    check(!wire::decode(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)), "a short digest is refused");
    bytes.push_back(0);
    check(!wire::decode(bytes), "a long digest is refused");
    check(!wire::decode({}), "an empty digest is refused");
    wire::WorldDigest digest = sample();
    digest.flags |= 0x80;
    check(!wire::decode(bytesOf(digest)), "a flag bit this build does not know is refused");
    digest.flags = 0;
    check(wire::decode(bytesOf(digest)).has_value(), "not caught up is a valid digest");
}

void testCrc() {
    const std::string text = "123456789";
    check(rule::crc32({reinterpret_cast<const uint8_t*>(text.data()), text.size()}) == 0xCBF43926u, "CRC-32 of the check string");
    check(rule::crc32({}) == 0, "CRC-32 of nothing");
}

void testPresenceFlagsDoNotChangeTheCrc() {
    const Words quiet{};
    const rule::FlagCrcs base = rule::flagCrcs(quiet);
    for (const uint32_t bit : kPresenceBits) {
        Words words = quiet;
        setBit(words, bit);
        const rule::FlagCrcs crcs = rule::flagCrcs(words);
        check(crcs.story == base.story && crcs.enemy == base.enemy && crcs.act == base.act, "a presence flag is each machine's own");
    }
    Words other = quiet;
    setBit(other, 0x38);
    check(rule::flagCrcs(other).story != base.story, "a bit next to a presence flag is still story");
}

void testFamiliesAreSeparate() {
    const rule::FlagCrcs base = rule::flagCrcs(Words{});
    const struct {
        uint32_t bit;
        bool story, enemy, act;
        const char* what;
    } cases[] = {
        {0x100, true, false, false, "a story flag changes the story CRC only"},
        {0x311, true, false, false, "the last story bit is story"},
        {0x312, false, true, false, "the first enemy bit changes the enemy CRC only"},
        {0x411, false, true, false, "the last enemy bit is enemy"},
        {0x412, true, false, false, "the first effect bit is story"},
        {0x611, true, false, false, "the last effect bit is story"},
        {0x612, false, false, true, "the first act bit changes the act CRC only"},
        {0x711, false, false, true, "the last act bit is act"},
        {0x712, true, false, false, "the first display bit is story"},
        {0x8df, true, false, false, "the last bgm bit is story"},
    };
    for (const auto& c : cases) {
        Words words{};
        setBit(words, c.bit);
        const rule::FlagCrcs crcs = rule::flagCrcs(words);
        check((crcs.story != base.story) == c.story && (crcs.enemy != base.enemy) == c.enemy && (crcs.act != base.act) == c.act, c.what);
    }
}

// The same bits and rooms give the same values in tools/re0/fake_peer (tests/test_digest.py), which builds the fake's digests.
void testKnownAnswersMatchTheFakePeer() {
    Words words{};
    for (const uint32_t bit : {0x000u, 0x040u, 0x037u, 0x312u, 0x412u, 0x612u, 0x7e1u}) setBit(words, bit);
    const rule::FlagCrcs crcs = rule::flagCrcs(words);
    check(crcs.story == 0x26b68855u && crcs.enemy == 0x390a7f35u && crcs.act == 0xe58eff0cu, "the flag CRCs of a known word set");
    check(rule::authorityHash({{0x0a05, 0, 2}, {0x0102, 1, 5}, {0x0b01, 0, 1}}) == 0xe99d86deu, "the authority hash of a known table");
}

void testAuthorityHash() {
    const std::vector<rule::AuthorityEntry> table = {{0x0a05, 0, 2}, {0x0102, 1, 5}, {0x0b01, 0, 1}};
    const std::vector<rule::AuthorityEntry> shuffled = {table[2], table[0], table[1]};
    check(rule::authorityHash(table) == rule::authorityHash(shuffled), "the order the table lists rooms in does not matter");
    std::vector<rule::AuthorityEntry> other = table;
    other[1].owner = 0;
    check(rule::authorityHash(other) != rule::authorityHash(table), "another owner is another hash");
    other = table;
    other[1].epoch = 6;
    check(rule::authorityHash(other) != rule::authorityHash(table), "another epoch is another hash");
    other = table;
    other.pop_back();
    check(rule::authorityHash(other) != rule::authorityHash(table), "a missing room is another hash");
    check(rule::authorityHash({}) == rule::authorityHash({}), "an empty table has a hash");
}

// Feeds `local` against `peer` once a second from `from` to `until` (inclusive); the verdicts in order.
std::vector<rule::Verdict> run(rule::Canary& canary, const wire::WorldDigest& local, const wire::WorldDigest& peer, uint32_t from,
                               uint32_t until) {
    std::vector<rule::Verdict> all;
    for (uint32_t now = from; now - from <= until - from; now += kStep) {
        for (const rule::Verdict& verdict : canary.compare(local, peer, now)) all.push_back(verdict);
    }
    return all;
}

wire::WorldDigest withEnemyMismatch() {
    wire::WorldDigest digest = sample();
    digest.enemyFlagCrc ^= 1;
    return digest;
}

void testEqualAndShortMismatchSayNothing() {
    rule::Canary canary;
    check(run(canary, sample(), sample(), 0, 30 * kStep).empty(), "equal digests say nothing");
    check(run(canary, sample(), withEnemyMismatch(), 40 * kStep, 40 * kStep + rule::kDivergeMs - kStep).empty(),
          "a mismatch under 3 s says nothing");
    check(run(canary, sample(), sample(), 50 * kStep, 60 * kStep).empty(), "a mismatch that healed says nothing, no convergence either");
}

void testDivergedOnceThenQuietThenRepeat() {
    rule::Canary canary;
    const auto first = run(canary, sample(), withEnemyMismatch(), 0, rule::kDivergeMs);
    check(first.size() == 1 && first[0].scope == Scope::EnemyFlags && first[0].event == Event::Diverged && !first[0].repeat,
          "3 s of mismatch is one Diverged naming the family");
    check(run(canary, sample(), withEnemyMismatch(), rule::kDivergeMs + kStep, rule::kDivergeMs + rule::kHoldOffMs - kStep).empty(),
          "the scope is quiet while a resync lands");
    const auto again = run(canary, sample(), withEnemyMismatch(), rule::kDivergeMs + rule::kHoldOffMs, 2 * rule::kDivergeMs + rule::kHoldOffMs);
    check(again.size() == 1 && again[0].event == Event::Diverged && again[0].repeat, "still different after the hold-off: reported again as a repeat");
}

void testConvergedOnce() {
    rule::Canary canary;
    run(canary, sample(), withEnemyMismatch(), 0, rule::kDivergeMs);
    const auto healed = run(canary, sample(), sample(), rule::kDivergeMs + kStep, rule::kDivergeMs + 5 * kStep);
    check(healed.size() == 1 && healed[0].scope == Scope::EnemyFlags && healed[0].event == Event::Converged, "equal again is one Converged");
    const uint32_t later = rule::kDivergeMs + 10 * kStep;
    const auto next = run(canary, sample(), withEnemyMismatch(), later, later + rule::kDivergeMs);
    check(next.size() == 1 && next[0].event == Event::Diverged && !next[0].repeat, "a new divergence after convergence is reported at once, not a repeat");
}

void testScopesAreIndependent() {
    rule::Canary canary;
    wire::WorldDigest peer = sample();
    peer.actFlagCrc ^= 1;
    peer.enemyFlagCrc ^= 1;
    const auto verdicts = run(canary, sample(), peer, 0, rule::kDivergeMs);
    check(verdicts.size() == 2, "two stores that differ give two verdicts");
    bool act = false;
    bool enemy = false;
    for (const rule::Verdict& verdict : verdicts) {
        act = act || verdict.scope == Scope::ActFlags;
        enemy = enemy || verdict.scope == Scope::EnemyFlags;
    }
    check(act && enemy, "each names its own store");
    wire::WorldDigest authority = sample();
    authority.authorityHash ^= 1;
    rule::Canary other;
    const auto got = run(other, sample(), authority, 0, rule::kDivergeMs);
    check(got.size() == 1 && got[0].scope == Scope::Authority, "the authority hash has its own scope");
    wire::WorldDigest flags = sample();
    flags.flagCrc ^= 1;
    rule::Canary third;
    const auto story = run(third, sample(), flags, 0, rule::kDivergeMs);
    check(story.size() == 1 && story[0].scope == Scope::Flags, "the story CRC has its own scope");
}

void testDifferentSerialsNeverDiverge() {
    rule::Canary canary;
    wire::WorldDigest peer = withEnemyMismatch();
    peer.flagSerial = kSerial + 1;
    check(run(canary, sample(), peer, 0, 10 * rule::kHoldOffMs).empty(), "digests at different flag commits are never compared");
}

void testNotComparableEndsARunningMismatch() {
    rule::Canary canary;
    run(canary, sample(), withEnemyMismatch(), 0, rule::kDivergeMs - kStep);
    wire::WorldDigest moved = withEnemyMismatch();
    moved.flagSerial = kSerial + 1;
    run(canary, sample(), moved, rule::kDivergeMs, rule::kDivergeMs);
    const uint32_t restart = rule::kDivergeMs + kStep;
    check(run(canary, sample(), withEnemyMismatch(), restart, restart + rule::kDivergeMs - kStep).empty(),
          "a mismatch that was interrupted by a commit starts counting again");
    check(run(canary, sample(), withEnemyMismatch(), restart + rule::kDivergeMs, restart + rule::kDivergeMs).size() == 1,
          "and is reported 3 s after it restarted");
}

void testPendingProposalsAndCatchUpSuppress() {
    rule::Canary canary;
    wire::WorldDigest proposing = sample();
    proposing.pendingProposals = 1;
    check(run(canary, proposing, withEnemyMismatch(), 0, 10 * kStep).empty(), "a proposal in flight here suppresses the comparison");
    wire::WorldDigest peerProposing = withEnemyMismatch();
    peerProposing.pendingProposals = 2;
    check(run(canary, sample(), peerProposing, 20 * kStep, 30 * kStep).empty(), "a proposal in flight there suppresses it too");
    wire::WorldDigest joining = sample();
    joining.flags = 0;
    check(run(canary, joining, withEnemyMismatch(), 40 * kStep, 50 * kStep).empty(), "a machine that has not caught up is not compared");
    wire::WorldDigest peerJoining = withEnemyMismatch();
    peerJoining.flags = 0;
    check(run(canary, sample(), peerJoining, 60 * kStep, 70 * kStep).empty(), "nor is a peer that has not");
}

void testWrapAroundTheClock() {
    rule::Canary canary;
    const uint32_t start = 0xffffffffu - 2 * kStep;
    const auto verdicts = run(canary, sample(), withEnemyMismatch(), start, start + rule::kDivergeMs);
    check(verdicts.size() == 1 && verdicts[0].event == Event::Diverged, "3 s of mismatch across the clock wrap is one Diverged");
}

}  // namespace

int main() {
    testWire();
    testWireRefusals();
    testCrc();
    testPresenceFlagsDoNotChangeTheCrc();
    testFamiliesAreSeparate();
    testKnownAnswersMatchTheFakePeer();
    testAuthorityHash();
    testEqualAndShortMismatchSayNothing();
    testDivergedOnceThenQuietThenRepeat();
    testConvergedOnce();
    testScopesAreIndependent();
    testDifferentSerialsNeverDiverge();
    testNotComparableEndsARunningMismatch();
    testPendingProposalsAndCatchUpSuppress();
    testWrapAroundTheClock();
    if (g_failures == 0) std::printf("world_digest_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
