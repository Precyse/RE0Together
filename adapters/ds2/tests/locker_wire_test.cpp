// locker_wire: the LOCKER_LIST, LOCKER_MOVE and LOCKER_RESULT payloads and their rejections (no game).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/locker_wire.h"

namespace {

using namespace locker_wire;
using cargo_transfer::CargoAdd;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

CargoAdd piece(uint32_t type, uint64_t order = 0) { return {type, 2, {}, 0.75f, 0, order, 0}; }

std::vector<CargoAdd> pieces(size_t count) {
    std::vector<CargoAdd> out;
    for (size_t i = 0; i < count; ++i) out.push_back(piece(static_cast<uint32_t>(100 + i), i % 2 ? 0x1000071000018Eull : 0));
    return out;
}

LockerHeader header(uint8_t kind = game::kSlotPublic) { return {0xCAFE1234, kind, 0, 0, 7, 0}; }

LockerMove move() {
    LockerMove out{};
    out.requestId = 5;
    out.key = 0xCAFE1234;
    out.slotKind = game::kSlotPublic;
    out.direction = kDirectionPut;
    out.guestHandle = 0x99;
    out.piece = piece(641900174, 0x1000071000018Eull);
    return out;
}

std::vector<uint8_t> bytes(const LockerMove& m) {
    std::vector<uint8_t> out(sizeof(m));
    std::memcpy(out.data(), &m, sizeof(m));
    return out;
}

std::vector<uint8_t> bytes(const LockerResult& r) {
    std::vector<uint8_t> out(sizeof(r));
    std::memcpy(out.data(), &r, sizeof(r));
    return out;
}

bool listRejected(const std::vector<uint8_t>& payload) {
    LockerHeader got;
    std::vector<CargoAdd> list;
    return !decodeList(payload, got, list);
}

}  // namespace

int main() {
    check(kMsgLockerList == 0x0134 && kMsgLockerMove == 0x0135 && kMsgLockerResult == 0x0136, "the message ids are 0x0134 to 0x0136");

    for (const size_t count : {size_t{0}, size_t{1}, size_t{kMaxLockerPieces}}) {
        const std::vector<CargoAdd> sent = pieces(count);
        const std::vector<uint8_t> payload = encodeList(header(), sent);
        LockerHeader got{};
        std::vector<CargoAdd> list = pieces(3);
        check(payload.size() == sizeof(LockerHeader) + count * sizeof(CargoAdd), "a list is the header and the pieces");
        check(decodeList(payload, got, list) && got.key == 0xCAFE1234 && got.slotKind == game::kSlotPublic && got.count == count &&
                  got.replyTo == 7 && list.size() == count && (count == 0 || std::memcmp(list.data(), sent.data(), count * sizeof(CargoAdd)) == 0),
              "a list round-trips");
    }
    LockerHeader received{};
    std::vector<CargoAdd> receivedList;
    check(decodeList(encodeList(header(game::kSlotPublicReceived), {}), received, receivedList), "a received-locker list (kind 29) is accepted");

    std::vector<uint8_t> payload = encodeList(header(), pieces(2));
    payload.pop_back();
    check(listRejected(payload), "a list one byte short is rejected");
    payload = encodeList(header(), pieces(2));
    payload.push_back(0);
    check(listRejected(payload), "a list one byte long is rejected");
    check(listRejected(std::vector<uint8_t>(sizeof(LockerHeader) - 1)), "a payload shorter than the header is rejected");

    std::vector<CargoAdd> many = pieces(kMaxLockerPieces + 1);
    check(listRejected(encodeList(header(), many)), "129 pieces are rejected");
    check(listRejected(encodeList(header(game::kSlotPrivate), pieces(1))), "kind 27 (private) is rejected");
    check(listRejected(encodeList(header(0), pieces(1))), "kind 0 is rejected");

    LockerHeader flagged = header();
    flagged.flags = 2;
    check(listRejected(encodeList(flagged, {})), "flags bit 1 is rejected");
    flagged = header();
    flagged.reserved = 1;
    check(listRejected(encodeList(flagged, {})), "a set header reserved field is rejected");

    const LockerHeader end{0, 0, kFlagSnapshotEnd, 0, 0, 0};
    LockerHeader got{};
    std::vector<CargoAdd> list;
    check(decodeList(encodeList(end, {}), got, list) && (got.flags & kFlagSnapshotEnd), "the snapshot end is accepted");
    LockerHeader bad = end;
    bad.key = 1;
    check(listRejected(encodeList(bad, {})), "the snapshot end with a key is rejected");
    bad = end;
    check(listRejected(encodeList(bad, pieces(1))), "the snapshot end with a piece is rejected");
    bad = end;
    bad.slotKind = game::kSlotPublic;
    check(listRejected(encodeList(bad, {})), "the snapshot end with a kind is rejected");

    CargoAdd odd = piece(1);
    odd.durability = std::numeric_limits<float>::quiet_NaN();
    check(listRejected(encodeList(header(), std::vector<CargoAdd>{odd})), "a NaN durability is rejected");
    odd = piece(1);
    odd.reserved2 = 1;
    check(listRejected(encodeList(header(), std::vector<CargoAdd>{odd})), "a set piece reserved field is rejected");

    LockerMove decoded{};
    check(decodeMove(bytes(move()), decoded) && decoded.requestId == 5 && decoded.key == 0xCAFE1234 && decoded.direction == kDirectionPut &&
              decoded.guestHandle == 0x99 && decoded.piece.orderId == 0x1000071000018Eull,
          "a move round-trips");
    LockerMove take = move();
    take.direction = kDirectionTake;
    check(decodeMove(bytes(take), decoded), "a take is accepted");
    LockerMove badMove = move();
    badMove.direction = 0;
    check(!decodeMove(bytes(badMove), decoded), "direction 0 is rejected");
    badMove.direction = 3;
    check(!decodeMove(bytes(badMove), decoded), "direction 3 is rejected");
    badMove = move();
    badMove.slotKind = game::kSlotPrivate;
    check(!decodeMove(bytes(badMove), decoded), "a move in a private locker is rejected");
    badMove = move();
    badMove.requestId = 0;
    check(!decodeMove(bytes(badMove), decoded), "request id 0 is rejected");
    badMove = move();
    badMove.reserved = 1;
    check(!decodeMove(bytes(badMove), decoded), "a set move reserved field is rejected");
    std::vector<uint8_t> moveBytes = bytes(move());
    moveBytes.pop_back();
    check(!decodeMove(moveBytes, decoded), "a move of 55 bytes is rejected");
    moveBytes = bytes(move());
    moveBytes.push_back(0);
    check(!decodeMove(moveBytes, decoded), "a move of 57 bytes is rejected");

    LockerResult result{};
    check(decodeResult(bytes(resultOf(5, Reason::Ok)), result) && result.accepted == 1 && result.reason == 0, "an accepted result round-trips");
    check(decodeResult(bytes(resultOf(5, Reason::Rate)), result) && result.accepted == 0 && result.reason == 6, "reason 6 (rate) round-trips");
    LockerResult badResult = resultOf(5, Reason::Full);
    badResult.reason = 7;
    check(!decodeResult(bytes(badResult), result), "reason 7 is rejected");
    badResult = resultOf(5, Reason::Full);
    badResult.accepted = 1;
    check(!decodeResult(bytes(badResult), result), "an accepted result with a refusal reason is rejected");
    badResult = resultOf(5, Reason::Ok);
    badResult.accepted = 0;
    check(!decodeResult(bytes(badResult), result), "a refused result with reason 0 is rejected");
    badResult = resultOf(0, Reason::Ok);
    check(!decodeResult(bytes(badResult), result), "request id 0 is rejected in a result");
    std::vector<uint8_t> resultBytes = bytes(resultOf(5, Reason::Ok));
    resultBytes.push_back(0);
    check(!decodeResult(resultBytes, result), "a result of 9 bytes is rejected");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
