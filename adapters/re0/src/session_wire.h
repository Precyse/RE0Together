#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "item_ledger_rule.h"
#include "item_wire.h"
#include "prop_wire.h"
#include "protocol.h"
#include "room_authority_wire.h"

// Wire format of the session messages of partner_mode=state (docs/RE0_NOTES.md "State mode: session"). SESSION_READY is
// the guest's "I stand in the host's world" and the host's echo; JOIN_EXTRAS is what the host knows beyond JOIN_SNAPSHOT.
// Pure, no game access, unit tested.
namespace session_wire {

constexpr uint16_t kMsgSessionReady = proto::kFirstGameType + 0x55;  // 0x0155, guest to host and the host's echo, reliable
constexpr uint16_t kMsgJoinExtras = proto::kFirstGameType + 0x56;    // 0x0156, host to guest after JOIN_SNAPSHOT, reliable

constexpr uint16_t kSceneLimit = 0xaf;  // scene ids are below this (scene::kSceneIdLimit)

enum class ReadyPhase : uint8_t { Ready, Echo };
constexpr uint8_t kLastReadyPhase = static_cast<uint8_t>(ReadyPhase::Echo);

// SESSION_READY: the guest's loaded scene and load serial and the serial of the host snapshot it applied. The host's echo
// repeats the three values of the Ready it accepted.
struct SessionReady {
    uint8_t phase;  // ReadyPhase
    uint8_t reserved;
    uint16_t scene;
    uint16_t loadSerial;  // never 0 (load_serial.h)
    uint16_t reserved2;
    uint32_t snapshotSerial;  // the JOIN_EXTRAS serial; never 0
};
static_assert(sizeof(SessionReady) == 12);

// A SESSION_READY of the right size for a scene the game has, a load serial and a snapshot serial that exist.
inline std::optional<SessionReady> decodeReady(std::span<const uint8_t> bytes) {
    SessionReady ready{};
    if (bytes.size() != sizeof(ready)) return std::nullopt;
    std::memcpy(&ready, bytes.data(), sizeof(ready));
    if (ready.phase > kLastReadyPhase || ready.reserved != 0 || ready.reserved2 != 0 || ready.scene >= kSceneLimit ||
        ready.loadSerial == 0 || ready.snapshotSerial == 0) {
        return std::nullopt;
    }
    return ready;
}

constexpr size_t kMaxClaims = kSceneLimit;  // one room claim per scene id
constexpr size_t kMaxProps = 32;            // prop_rule::kMaxJournal (checked where both are visible)
constexpr size_t kMaxItems = item_ledger_rule::kMaxEntries;

// An item the host's ledger knows was taken since the last save or load.
struct ItemTake {
    uint16_t scene;
    uint16_t reserved;
    uint32_t itemId;
    float pos[3];
};
static_assert(sizeof(ItemTake) == 20);

// JOIN_EXTRAS: the header, then `claims` AuthMessages, `props` prop commits and `items` ItemTakes.
struct ExtrasHeader {
    uint32_t snapshotSerial;  // numbers the host's snapshots; never 0
    uint32_t flagSerial;      // the serial of the host's last flag commit when the snapshot was read (0 = none yet)
    uint16_t claims;
    uint16_t props;
    uint16_t items;
    uint16_t reserved;
};
static_assert(sizeof(ExtrasHeader) == 16);

struct JoinExtras {
    uint32_t snapshotSerial = 0;
    uint32_t flagSerial = 0;
    std::vector<room_authority_wire::AuthMessage> claims;
    std::vector<prop_wire::Commit> props;
    std::vector<ItemTake> items;
};

inline item_ledger_rule::Key keyOf(const ItemTake& take) {
    return {take.scene, take.itemId, {take.pos[0], take.pos[1], take.pos[2]}};
}

inline ItemTake takeOf(const item_ledger_rule::Key& key) {
    return {key.scene, 0, key.itemId, {key.pos[0], key.pos[1], key.pos[2]}};
}

namespace detail {

template <typename T>
void append(std::vector<uint8_t>& out, const std::vector<T>& values) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
    out.insert(out.end(), bytes, bytes + values.size() * sizeof(T));
}

template <typename T>
const uint8_t* take(const uint8_t* cursor, size_t count, std::vector<T>& out) {
    out.resize(count);
    if (count) std::memcpy(out.data(), cursor, count * sizeof(T));
    return cursor + count * sizeof(T);
}

inline bool finite(const float (&pos)[3]) { return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]); }

}  // namespace detail

// The bytes to send; a list longer than its limit is cut to it.
inline std::vector<uint8_t> encode(const JoinExtras& extras) {
    JoinExtras kept = extras;
    kept.claims.resize(std::min(kept.claims.size(), kMaxClaims));
    kept.props.resize(std::min(kept.props.size(), kMaxProps));
    kept.items.resize(std::min(kept.items.size(), kMaxItems));
    const ExtrasHeader header{kept.snapshotSerial, kept.flagSerial, static_cast<uint16_t>(kept.claims.size()),
                              static_cast<uint16_t>(kept.props.size()), static_cast<uint16_t>(kept.items.size()), 0};
    std::vector<uint8_t> out(sizeof(header));
    std::memcpy(out.data(), &header, sizeof(header));
    detail::append(out, kept.claims);
    detail::append(out, kept.props);
    detail::append(out, kept.items);
    return out;
}

// JOIN_EXTRAS of exactly the size its header promises, with counts inside the limits and every entry as valid as the
// message it stands for (a claim as ROOM_AUTH_CLAIM, a commit as PROP_COMMIT, an item as TAKE_REQUEST); else nothing.
inline std::optional<JoinExtras> decode(std::span<const uint8_t> bytes) {
    ExtrasHeader header{};
    if (bytes.size() < sizeof(header)) return std::nullopt;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.snapshotSerial == 0 || header.reserved != 0 || header.claims > kMaxClaims || header.props > kMaxProps ||
        header.items > kMaxItems) {
        return std::nullopt;
    }
    const size_t expected = sizeof(header) + header.claims * sizeof(room_authority_wire::AuthMessage) +
                            header.props * sizeof(prop_wire::Commit) + header.items * sizeof(ItemTake);
    if (bytes.size() != expected) return std::nullopt;

    JoinExtras extras;
    extras.snapshotSerial = header.snapshotSerial;
    extras.flagSerial = header.flagSerial;
    const uint8_t* cursor = bytes.data() + sizeof(header);
    cursor = detail::take(cursor, header.claims, extras.claims);
    cursor = detail::take(cursor, header.props, extras.props);
    detail::take(cursor, header.items, extras.items);

    for (const room_authority_wire::AuthMessage& claim : extras.claims) {
        room_authority_wire::AuthMessage checked;
        if (!room_authority_wire::decode(proto::bytesOf(claim), checked)) return std::nullopt;
    }
    for (const prop_wire::Commit& commit : extras.props) {
        if (!prop_wire::decodeCommit(proto::bytesOf(commit))) return std::nullopt;
    }
    for (const ItemTake& take : extras.items) {
        if (take.reserved != 0 || take.scene >= kSceneLimit || take.itemId > item_wire::kMaxItemId ||
            !detail::finite(take.pos)) {
            return std::nullopt;
        }
    }
    return extras;
}

}  // namespace session_wire
