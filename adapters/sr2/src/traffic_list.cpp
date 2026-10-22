#include "traffic_list.h"

#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

constexpr uint8_t kNotOwnedBit = 1;

// The game's ownership test (0x938D80): a vehicle with no net record, or in a session with no local member, is this machine's;
// otherwise it must be recorded under this machine's slot and not be marked foreign.
bool isOwned(uintptr_t record, bool hasLocal, uint8_t local) {
    if (record == 0 || !hasLocal) return true;
    return readByte(record + sr2::kRecordOwnerOffset) == local && (readByte(record + sr2::kRecordNotOwnedOffset) & kNotOwnedBit) == 0;
}

population_rules::Point positionOf(uintptr_t object) {
    const partner_rules::Vec3 position = remote_players::positionOf(object);
    return {position.x, position.y, position.z};
}

}  // namespace

namespace traffic_list {

bool localSlot(uint8_t& slot) {
    const uintptr_t local = remote_players::localMember();
    if (!local) return false;
    slot = readByte(local + sr2::kMemberSlotOffset);
    return true;
}

size_t players(population_rules::Player* out, size_t capacity, uint8_t& localSlotOut) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    localSlotOut = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (snapshot.members[i].local) localSlotOut = snapshot.members[i].slot;
    }
    size_t count = 0;
    const uintptr_t local = remote_players::localHuman();
    if (local && count < capacity) out[count++] = {localSlotOut, positionOf(local)};
    for (size_t i = 0; i < snapshot.pairing.count && count < capacity; ++i) {
        if (!snapshot.pairing.items[i].human) continue;
        out[count++] = {snapshot.pairing.items[i].slot, positionOf(snapshot.pairing.items[i].human)};
    }
    return count;
}

size_t take(Vehicle* out, size_t capacity) {
    uint8_t local = 0;
    const bool hasLocal = localSlot(local);
    const uintptr_t head = readDword(sr2::kTrafficListHead);
    size_t count = 0;
    uintptr_t entry = head;
    for (size_t walked = 0; entry != 0 && walked < sr2::kTrafficListWalkLimit && count < capacity; ++walked) {
        const uintptr_t object = remote_players::objectOfHandle(readDword(entry + sr2::kTrafficEntryHandleOffset));
        if (object != 0 && readDword(object + sr2::kObjectTypeOffset) == sr2::kObjectTypeVehicle) {
            const uintptr_t record = readDword(object + sr2::kObjectNetRecordOffset);
            const bool owned = isOwned(record, hasLocal, local);
            const uint8_t owner = record ? readByte(record + sr2::kRecordOwnerOffset) : local;
            out[count++] = {object, owner, owned, positionOf(object)};
        }
        entry = readDword(entry + sr2::kTrafficEntryNextOffset);
        if (entry == head) break;
    }
    return count;
}

size_t ownedCount() {
    traffic_list::Vehicle vehicles[sr2::kTrafficListWalkLimit];
    const size_t count = take(vehicles, sr2::kTrafficListWalkLimit);
    size_t owned = 0;
    for (size_t i = 0; i < count; ++i) owned += vehicles[i].owned ? 1 : 0;
    return owned;
}

}  // namespace traffic_list
