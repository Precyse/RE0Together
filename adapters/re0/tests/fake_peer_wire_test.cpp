// Runs the adapter's own decoders over the golden frames of the one-PC fake peer (tools/re0/fake_peer, `golden`): every
// accepted sample must decode and every refused sample must be refused, and each state type must have both. Exit 0 when
// every check passes.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <vector>

#include "../src/boss_wire.h"
#include "../src/demo_skip_wire.h"
#include "../src/door_travel.h"
#include "../src/enemy_lease_wire.h"
#include "../src/enemy_observer_wire.h"
#include "../src/enemy_protocol.h"
#include "../src/flag_sequencer_wire.h"
#include "../src/floor_items_sync.h"
#include "../src/floor_snapshot.h"
#include "../src/item_wire.h"
#include "../src/join_sync.h"
#include "../src/pose_wire.h"
#include "../src/prop_wire.h"
#include "../src/room_authority_wire.h"
#include "../src/room_move_wire.h"
#include "../src/session_wire.h"
#include "../src/state_sync.h"
#include "../src/weapon_fx_wire.h"

#ifndef FAKE_PEER_GOLDEN
#error FAKE_PEER_GOLDEN must name the golden frames file
#endif

namespace {

using Bytes = std::span<const uint8_t>;

constexpr size_t kRecordHeaderSize = 8;
constexpr int kNoDecoder = -1;

struct Record {
    uint16_t type;
    bool expect;
    std::vector<uint8_t> payload;
};

int g_failures = 0;

void fail(const char* what, uint16_t type, size_t index) {
    std::printf("FAIL: %s (type 0x%04x, record %zu)\n", what, type, index);
    ++g_failures;
}

bool readRecords(const char* path, std::vector<Record>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t at = 0;
    while (at + kRecordHeaderSize <= bytes.size()) {
        uint16_t type;
        uint32_t length;
        std::memcpy(&type, &bytes[at], sizeof(type));
        std::memcpy(&length, &bytes[at + 4], sizeof(length));
        if (at + kRecordHeaderSize + length > bytes.size()) return false;
        const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(at + kRecordHeaderSize);
        out.push_back({type, bytes[at + 2] != 0, std::vector<uint8_t>(first, first + length)});
        at += kRecordHeaderSize + length;
    }
    return at == bytes.size();
}

bool exactSize(Bytes payload, size_t size) { return payload.size() == size; }

template <class Message>
bool leaseDecodes(Bytes payload) {
    return enemy_lease_wire::decode<Message>(payload).has_value();
}

bool roomAuthDecodes(Bytes payload) {
    room_authority_wire::AuthMessage message;
    return room_authority_wire::decode(payload, message);
}

// 1 accepted, 0 refused, kNoDecoder for a type this test has no decoder for.
int judge(uint16_t type, Bytes p) {
    switch (type) {
        case state_sync::kMsgPlayerState: return exactSize(p, sizeof(state_sync::PlayerState));
        case proto::kMsgRoomState: return exactSize(p, sizeof(door_travel::RoomState));
        case proto::kMsgFloorPut: return exactSize(p, sizeof(floor_items_sync::FloorPut));
        case proto::kMsgFloorTake: return exactSize(p, sizeof(floor_items_sync::FloorTake));
        case enemy_protocol::kMsgHitRequest: return exactSize(p, sizeof(enemy_protocol::HitPayload));
        case proto::kMsgJoinSnapshot: return exactSize(p, sizeof(join_sync::JoinSnapshot));
        case proto::kMsgFloorSnapshot: return floor_snapshot::decode(p).has_value();
        case pose_wire::kMsgPoseState: return pose_wire::decodePose(p).has_value();
        case pose_wire::kMsgMotionEvent: return pose_wire::decodeEvent(p).has_value();
        case weapon_fx_wire::kMsgWeaponFx: return weapon_fx_wire::decode(p).has_value();
        case room_move_wire::kMsgRoomMove: return room_move_wire::decodeMove(p).has_value();
        case room_move_wire::kMsgDoorFollow: return room_move_wire::decodeFollow(p).has_value();
        case room_move_wire::kMsgDoorFollowResult: return room_move_wire::decodeResult(p).has_value();
        case demo_skip_wire::kMsgDemoSkipRequest:
        case demo_skip_wire::kMsgDemoSkip: return demo_skip_wire::decode(p).has_value();
        case session_wire::kMsgSessionReady: return session_wire::decodeReady(p).has_value();
        case session_wire::kMsgJoinExtras: return session_wire::decode(p).has_value();
        case enemy_observer_wire::kMsgEnemyPose: return enemy_observer_wire::decode<enemy_observer_wire::PoseEntry>(p).has_value();
        case enemy_observer_wire::kMsgEnemyMotion: return enemy_observer_wire::decode<enemy_observer_wire::MotionEntry>(p).has_value();
        case enemy_lease_wire::kMsgEnemyLease: return leaseDecodes<enemy_lease_wire::EnemyLease>(p);
        case enemy_lease_wire::kMsgEnemyLeaseEnd: return leaseDecodes<enemy_lease_wire::EnemyLeaseEnd>(p);
        case enemy_lease_wire::kMsgPlayerHit: return leaseDecodes<enemy_lease_wire::PlayerHit>(p);
        case room_authority_wire::kMsgClaim:
        case room_authority_wire::kMsgDecline:
        case room_authority_wire::kMsgStop: return roomAuthDecodes(p);
        case boss_wire::kMsgBossHandover: return boss_wire::decode(p).has_value();
        case item_wire::kMsgTakeRequest: return item_wire::decodeRequest(p).has_value();
        case item_wire::kMsgTakeResult: return item_wire::decodeResult(p).has_value();
        case prop_wire::kMsgLease: return prop_wire::decodeLease(p).has_value();
        case prop_wire::kMsgMove: return prop_wire::decodeMove(p).has_value();
        case prop_wire::kMsgCommit: return prop_wire::decodeCommit(p).has_value();
        case flag_sequencer_wire::kMsgFlagPropose: return flag_sequencer_wire::decodePropose(p).has_value();
        case flag_sequencer_wire::kMsgFlagCommit: return flag_sequencer_wire::decodeCommit(p).has_value();
        default: return kNoDecoder;
    }
}

}  // namespace

int main() {
    std::vector<Record> records;
    if (!readRecords(FAKE_PEER_GOLDEN, records) || records.empty()) {
        std::printf("FAIL: cannot read the golden frames %s\n", FAKE_PEER_GOLDEN);
        return 1;
    }
    std::map<uint16_t, std::pair<int, int>> seen;  // type -> {accepted samples, refused samples}
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& record = records[i];
        const int verdict = judge(record.type, record.payload);
        if (verdict == kNoDecoder) {
            fail("no decoder for this type", record.type, i);
            continue;
        }
        (record.expect ? seen[record.type].first : seen[record.type].second)++;
        if ((verdict != 0) != record.expect) fail(record.expect ? "adapter refused an accepted sample" : "adapter accepted a refused sample", record.type, i);
    }
    for (const auto& [type, counts] : seen) {
        if (counts.first == 0 || counts.second == 0) fail("a type needs one accepted and one refused sample", type, 0);
    }
    std::printf("%zu golden frames, %zu types, %d failures\n", records.size(), seen.size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
