// Checks the push block wire format (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/prop_wire.h"

namespace {

namespace wire = prop_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

template <typename T>
std::vector<uint8_t> bytesOf(const T& value) {
    std::vector<uint8_t> out(sizeof(T));
    std::memcpy(out.data(), &value, sizeof(T));
    return out;
}

wire::Lease sampleLease() { return {9, 0x2d, 1, static_cast<uint8_t>(wire::LeaseOp::Grant), 1, {}}; }
wire::Move sampleMove() { return {1234, 0x2d, wire::kObstacleKind, 1, {10.0f, 20.0f, 30.0f}, 1.5f}; }
wire::Commit sampleCommit() { return {9, 0x2d, wire::kObstacleKind, 1, {10.0f, 20.0f, 30.0f}, 1.5f, 1, {}}; }

void testIdsAndSizes() {
    check(wire::kMsgLease == 0x0172 && wire::kMsgMove == 0x0173 && wire::kMsgCommit == 0x0174, "message ids");
    check(sizeof(wire::Lease) == 12 && sizeof(wire::Move) == 24 && sizeof(wire::Commit) == 28, "wire sizes");
}

void testRoundTrips() {
    const auto lease = wire::decodeLease(bytesOf(sampleLease()));
    check(lease && lease->serial == 9 && lease->scene == 0x2d && lease->unitId == 1 && lease->holder == 1, "lease fields survive");
    const auto move = wire::decodeMove(bytesOf(sampleMove()));
    check(move && move->senderMs == 1234 && move->pos[1] == 20.0f && move->yaw == 1.5f, "move fields survive");
    const auto commit = wire::decodeCommit(bytesOf(sampleCommit()));
    check(commit && commit->serial == 9 && commit->committer == 1 && commit->pos[2] == 30.0f, "commit fields survive");
}

void testSizeRefusals() {
    std::vector<uint8_t> lease = bytesOf(sampleLease());
    lease.pop_back();
    check(!wire::decodeLease(lease), "a short lease is refused");
    std::vector<uint8_t> move = bytesOf(sampleMove());
    move.push_back(0);
    check(!wire::decodeMove(move), "a long move is refused");
    check(!wire::decodeCommit({}), "an empty commit is refused");
}

void testRangeRefusals() {
    wire::Lease lease = sampleLease();
    lease.op = wire::kLastLeaseOp + 1;
    check(!wire::decodeLease(bytesOf(lease)), "an unknown operation is refused");
    lease = sampleLease();
    lease.unitId = wire::kUnitCount;
    check(!wire::decodeLease(bytesOf(lease)), "a unit past the obstacle table is refused");
    lease = sampleLease();
    lease.scene = wire::kSceneLimit;
    check(!wire::decodeLease(bytesOf(lease)), "a scene past the game's rooms is refused");

    wire::Move move = sampleMove();
    move.kind = 0x71;
    check(!wire::decodeMove(bytesOf(move)), "a move for another unit class is refused");
    move = sampleMove();
    move.pos[0] = std::numeric_limits<float>::infinity();
    check(!wire::decodeMove(bytesOf(move)), "a non-finite position is refused");
    move = sampleMove();
    move.yaw = std::numeric_limits<float>::quiet_NaN();
    check(!wire::decodeMove(bytesOf(move)), "a non-finite yaw is refused");

    wire::Commit commit = sampleCommit();
    commit.kind = 0;
    check(!wire::decodeCommit(bytesOf(commit)), "a commit for another unit class is refused");
    commit = sampleCommit();
    commit.pos[1] = std::numeric_limits<float>::quiet_NaN();
    check(!wire::decodeCommit(bytesOf(commit)), "a commit with a non-finite position is refused");
}

}  // namespace

int main() {
    testIdsAndSizes();
    testRoundTrips();
    testSizeRefusals();
    testRangeRefusals();
    if (g_failures == 0) std::printf("prop_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
