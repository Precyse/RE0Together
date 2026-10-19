// Checks the native rooms wire format (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/room_move_wire.h"

namespace {

namespace wire = room_move_wire;

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

wire::RoomMove sampleMove() { return {5, 0x2d, 3, 7, 1, 2}; }

wire::DoorFollow sampleFollow() { return {9, 0x2a, 0x2d, 4, 0x6f, 1, 1, 0, 0, 0}; }

wire::DoorFollowResult sampleResult() { return {9, static_cast<uint8_t>(wire::Result::Busy), 1, 0}; }

void testSizesAndIds() {
    check(sizeof(wire::RoomMove) == 12, "ROOM_MOVE is 12 bytes");
    check(sizeof(wire::DoorFollow) == 28, "DOOR_FOLLOW is 28 bytes");
    check(sizeof(wire::DoorFollowResult) == 8, "DOOR_FOLLOW_RESULT is 8 bytes");
    check(wire::kMsgRoomMove == 0x0150, "ROOM_MOVE is 0x0150");
    check(wire::kMsgDoorFollow == 0x0151, "DOOR_FOLLOW is 0x0151");
    check(wire::kMsgDoorFollowResult == 0x0152, "DOOR_FOLLOW_RESULT is 0x0152");
}

void testMoveRoundTrip() {
    const auto got = wire::decodeMove(bytesOf(sampleMove()));
    check(got.has_value(), "a good ROOM_MOVE decodes");
    if (!got) return;
    check(got->seq == 5 && got->scene == 0x2d && got->entry == 3 && got->loadSerial == 7 && got->character == 1 &&
              got->mode == 2,
          "ROOM_MOVE fields survive");
}

void testMoveRefusals() {
    std::vector<uint8_t> bytes = bytesOf(sampleMove());
    check(!wire::decodeMove(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)), "a short ROOM_MOVE is refused");
    bytes.push_back(0);
    check(!wire::decodeMove(bytes), "a long ROOM_MOVE is refused");

    wire::RoomMove move = sampleMove();
    move.scene = scene::kSceneIdLimit;
    check(!wire::decodeMove(bytesOf(move)), "a scene the game does not have is refused");
    move.scene = scene::kSceneIdLimit - 1;
    check(wire::decodeMove(bytesOf(move)).has_value(), "the highest scene is accepted");
    move = sampleMove();
    move.entry = wire::kMaxEntry + 1;
    check(!wire::decodeMove(bytesOf(move)), "an entry the engine would assert on is refused");
    move.entry = wire::kMaxEntry;
    check(wire::decodeMove(bytesOf(move)).has_value(), "the highest entry is accepted");
    move = sampleMove();
    move.loadSerial = 0;
    check(!wire::decodeMove(bytesOf(move)), "a zero load serial is refused");
    move = sampleMove();
    move.character = character_owner::kCharacterCount;
    check(!wire::decodeMove(bytesOf(move)), "an unknown character is refused");
    move = sampleMove();
    move.mode = spot_rule::kModes;
    check(!wire::decodeMove(bytesOf(move)), "a spot mode the entry table does not have is refused");
    move.mode = wire::kAutoSpot;
    check(wire::decodeMove(bytesOf(move)).has_value(), "the automatic spot is accepted");
}

void testFollowRoundTrip() {
    const auto got = wire::decodeFollow(bytesOf(sampleFollow()));
    check(got.has_value(), "a good DOOR_FOLLOW decodes");
    if (!got) return;
    check(got->serial == 9 && got->sourceScene == 0x2a && got->scene == 0x2d && got->entry == 4 && got->type == 0x6f &&
              got->motion == 1 && got->flag == 1 && got->character == 0,
          "DOOR_FOLLOW fields survive");
}

void testFollowRefusals() {
    std::vector<uint8_t> bytes = bytesOf(sampleFollow());
    check(!wire::decodeFollow(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)), "a short DOOR_FOLLOW is refused");
    bytes.push_back(0);
    check(!wire::decodeFollow(bytes), "a long DOOR_FOLLOW is refused");

    wire::DoorFollow follow = sampleFollow();
    follow.serial = 0;
    check(!wire::decodeFollow(bytesOf(follow)), "serial 0 is refused");
    follow = sampleFollow();
    follow.scene = scene::kSceneIdLimit;
    check(!wire::decodeFollow(bytesOf(follow)), "a target scene the game does not have is refused");
    follow = sampleFollow();
    follow.sourceScene = scene::kNone;
    check(!wire::decodeFollow(bytesOf(follow)), "a source scene of a room still loading is refused");
    follow = sampleFollow();
    follow.entry = wire::kMaxEntry + 1;
    check(!wire::decodeFollow(bytesOf(follow)), "an entry the engine would assert on is refused");
    follow = sampleFollow();
    follow.flag = 0x100;
    check(!wire::decodeFollow(bytesOf(follow)), "a flag wider than the engine's byte is refused");
    follow = sampleFollow();
    follow.character = character_owner::kCharacterCount;
    check(!wire::decodeFollow(bytesOf(follow)), "an unknown character is refused");
    follow = sampleFollow();
    follow.reason = wire::kReasonCount;
    check(!wire::decodeFollow(bytesOf(follow)), "an unknown reason is refused");
    follow = sampleFollow();
    follow.reserved = 1;
    check(!wire::decodeFollow(bytesOf(follow)), "a set reserved field is refused");
    follow = sampleFollow();
    follow.reason = static_cast<uint8_t>(wire::Reason::Script);
    check(wire::decodeFollow(bytesOf(follow)).has_value(), "a script reason is accepted");
}

void testResult() {
    const auto got = wire::decodeResult(bytesOf(sampleResult()));
    check(got.has_value() && got->serial == 9 && got->result == static_cast<uint8_t>(wire::Result::Busy) && got->character == 1,
          "a good DOOR_FOLLOW_RESULT decodes");
    wire::DoorFollowResult result = sampleResult();
    result.result = wire::kResultCount;
    check(!wire::decodeResult(bytesOf(result)), "an unknown result is refused");
    result = sampleResult();
    result.result = static_cast<uint8_t>(wire::Result::Timeout);
    check(wire::decodeResult(bytesOf(result)).has_value(), "the last result is accepted");
    result = sampleResult();
    result.serial = 0;
    check(!wire::decodeResult(bytesOf(result)), "serial 0 is refused");
    result = sampleResult();
    result.character = character_owner::kCharacterCount;
    check(!wire::decodeResult(bytesOf(result)), "an unknown character is refused");
    result = sampleResult();
    result.reserved = 2;
    check(!wire::decodeResult(bytesOf(result)), "a set reserved field is refused");
    std::vector<uint8_t> bytes = bytesOf(sampleResult());
    bytes.pop_back();
    check(!wire::decodeResult(bytes), "a short result is refused");
}

void testNames() {
    check(std::strcmp(wire::name(wire::Result::Commit), "commit") == 0, "commit has its name");
    check(std::strcmp(wire::name(wire::Result::Busy), "busy") == 0, "busy has its name");
    check(std::strcmp(wire::name(wire::Result::OtherRoom), "other room") == 0, "other room has its name");
    check(std::strcmp(wire::name(wire::Result::NoPeer), "no peer") == 0, "no peer has its name");
    check(std::strcmp(wire::name(wire::Result::Timeout), "timeout") == 0, "timeout has its name");
}

}  // namespace

int main() {
    testSizesAndIds();
    testMoveRoundTrip();
    testMoveRefusals();
    testFollowRoundTrip();
    testFollowRefusals();
    testResult();
    testNames();
    if (g_failures == 0) std::printf("room_move_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
