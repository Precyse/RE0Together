#include "remote_players.h"

#include <atomic>
#include <cstdio>

#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

using partner_rules::HumanEntry;
using partner_rules::kMaxWalked;
using partner_rules::kNoSlot;
using partner_rules::MemberEntry;

uint32_t read32(uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }
uint8_t read8(uintptr_t address) { return *reinterpret_cast<const uint8_t*>(address); }

constexpr uint64_t kHashPrime = 1099511628211ull;

uint64_t mix(uint64_t hash, uint64_t value) { return (hash ^ value) * kHashPrime; }

uint8_t ownerSlotOf(uintptr_t human) {
    const uintptr_t record = read32(human + sr2::kObjectNetRecordOffset);
    return record ? read8(record) : kNoSlot;
}

void takeHumans(remote_players::Snapshot& out) {
    out.humanCount = 0;
    const uintptr_t local = read32(sr2::kLocalPlayer);
    const uintptr_t head = read32(sr2::kPlayerListHead);
    uintptr_t human = head;
    for (size_t steps = 0; human && steps < kMaxWalked; ++steps) {
        if (human != local) out.humans[out.humanCount++] = {human, ownerSlotOf(human)};
        human = read32(human + sr2::kHumanNextOffset);
        if (human == head) break;
    }
}

void pairRemotes(remote_players::Snapshot& out) {
    MemberEntry remoteMembers[kMaxWalked];
    size_t remoteCount = 0;
    for (size_t i = 0; i < out.memberCount; ++i) {
        const remote_players::MemberInfo& member = out.members[i];
        if (!member.local) remoteMembers[remoteCount++] = {member.address, member.slot, member.state};
    }
    partner_rules::pair(out.humans, out.humanCount, remoteMembers, remoteCount, out.pairing);
}

// The first sight of a mapping that cannot be used is logged once; the signature keeps it from repeating.
void logUnmatchedOnce(const remote_players::Snapshot& snapshot) {
    static std::atomic<uint64_t> logged{0};
    uint64_t signature = 0;
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        signature = mix(mix(signature, snapshot.humans[i].human), snapshot.humans[i].ownerSlot);
    }
    if (logged.exchange(signature) == signature) return;
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        const HumanEntry& entry = snapshot.humans[i];
        bool matched = false;
        for (size_t m = 0; m < snapshot.memberCount; ++m) matched = matched || snapshot.members[m].slot == entry.ownerSlot;
        if (!matched) {
            logger::write("partner: human 0x%X owner slot %u matches no member", static_cast<unsigned>(entry.human),
                          entry.ownerSlot);
        }
    }
}

}  // namespace

namespace remote_players {

void takeMembers(Snapshot& out) {
    out.memberCount = 0;
    out.localMember = 0;
    out.hostMember = 0;
    const uintptr_t session = read32(sr2::kSessionPointer);
    if (!session) return;
    const uintptr_t head = read32(session + sr2::kSessionMemberListOffset);
    out.localMember = read32(session + sr2::kSessionLocalMemberOffset);
    out.hostMember = read32(session + sr2::kSessionHostMemberOffset);
    uintptr_t member = head;
    while (member && out.memberCount < kMaxWalked) {
        out.members[out.memberCount++] = {member, read8(member + sr2::kMemberSlotOffset),
                                          read32(member + sr2::kMemberStateOffset), member == out.localMember};
        member = read32(member + sr2::kMemberNextOffset);
        if (member == head) break;
    }
}

void take(Snapshot& out) {
    out.active = false;
    out.humanCount = 0;
    out.pairing.count = 0;
    out.pairing.trusted = true;
    out.memberCount = 0;
    out.localMember = 0;
    out.hostMember = 0;
    if (!read8(sr2::kCoopFlag) || !read32(sr2::kSessionPointer)) return;
    out.active = true;
    takeMembers(out);
    takeHumans(out);
    pairRemotes(out);
    if (!out.pairing.trusted) logUnmatchedOnce(out);
}

Partner defaultPartner(const Snapshot& snapshot) {
    if (!snapshot.active || !snapshot.pairing.trusted) return {false, 0, 0};
    const size_t index = partner_rules::pickDefault(snapshot.pairing, snapshot.hostMember,
                                                    snapshot.localMember == snapshot.hostMember);
    if (index == partner_rules::kNone) return {true, 0, 0};
    return {true, snapshot.pairing.items[index].human, snapshot.pairing.items[index].member};
}

bool anyRemoteMemberAtLeast(uintptr_t session, int32_t minState) {
    const uintptr_t head = read32(session + sr2::kSessionMemberListOffset);
    const uintptr_t local = read32(session + sr2::kSessionLocalMemberOffset);
    uintptr_t member = head;
    for (size_t steps = 0; member && steps < kMaxWalked; ++steps) {
        if (member != local && static_cast<int32_t>(read32(member + sr2::kMemberStateOffset)) >= minState) return true;
        member = read32(member + sr2::kMemberNextOffset);
        if (member == head) break;
    }
    return false;
}

uintptr_t humanOfMember(uintptr_t member) {
    Snapshot snapshot;
    take(snapshot);
    if (!snapshot.pairing.trusted) return 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (snapshot.pairing.items[i].member == member) return snapshot.pairing.items[i].human;
    }
    return 0;
}

uintptr_t memberOfHuman(uintptr_t human) {
    if (!human) return 0;
    Snapshot snapshot;
    take(snapshot);
    if (!snapshot.pairing.trusted) return 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (snapshot.pairing.items[i].human == human) return snapshot.pairing.items[i].member;
    }
    return 0;
}

bool isRemoteMember(uintptr_t member) {
    if (!member) return false;
    Snapshot snapshot;
    takeMembers(snapshot);
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (snapshot.members[i].address == member) return !snapshot.members[i].local;
    }
    return false;
}

size_t remoteHumanCount() {
    Snapshot snapshot;
    take(snapshot);
    return snapshot.humanCount;
}

size_t remoteHumans(uintptr_t* out, size_t capacity) {
    Snapshot snapshot;
    take(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.humanCount && count < capacity; ++i) out[count++] = snapshot.humans[i].human;
    return count;
}

size_t remoteMemberCount() {
    Snapshot snapshot;
    takeMembers(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (!snapshot.members[i].local) ++count;
    }
    return count;
}

uint16_t netIdOf(uintptr_t human) {
    const uintptr_t record = human ? read32(human + sr2::kObjectNetRecordOffset) : 0;
    return record ? *reinterpret_cast<const uint16_t*>(record + sr2::kRecordIdOffset) : sr2::kNoNetId;
}

uintptr_t objectOfHandle(uint32_t handle) {
    if (!handle) return 0;
    const uintptr_t object = readDword(sr2::kObjectTable + (handle & sr2::kHandleIndexMask) * sr2::kObjectTableStride);
    return object && readDword(object + sr2::kObjectHandleOffset) == handle ? object : 0;
}

uintptr_t localHuman() { return read32(sr2::kLocalPlayer); }

bool isRemotePlayerHuman(uintptr_t human) {
    if (!human || human == localHuman()) return false;
    const uintptr_t head = read32(sr2::kPlayerListHead);
    uintptr_t entry = head;
    for (size_t steps = 0; entry && steps < kMaxWalked; ++steps) {
        if (entry == human) return true;
        entry = read32(entry + sr2::kHumanNextOffset);
        if (entry == head) break;
    }
    return false;
}

partner_rules::Vec3 positionOf(uintptr_t object) {
    return *reinterpret_cast<const partner_rules::Vec3*>(object + sr2::kObjectPositionOffset);
}

Visuals visualsOf(uintptr_t human) {
    Visuals visuals{};
    visuals.hidden = static_cast<uint8_t>(readDword(human + sr2::kHumanVisibilityFlagsOffset) & sr2::kHumanHiddenFlags);
    visuals.type = readDword(human + sr2::kHumanTypeOffset);
    const uintptr_t render = readDword(human + sr2::kHumanRenderInstanceOffset);
    uint16_t word = 0;
    if (render && readMemory(render + sr2::kRenderHiddenWordOffset, &word, sizeof(word))) visuals.render = word & sr2::kRenderHiddenBit;
    const uintptr_t charInstance = readDword(human + sr2::kHumanCharInstanceOffset);
    if (charInstance) {
        visuals.skin = readDword(charInstance + sr2::kCharInstanceSkinOffset) != 0;
        const uintptr_t index = readDword(charInstance + sr2::kCharInstanceIndexOffset);
        visuals.record = readDword(sr2::kCharTables + index * sr2::kCharTableStride + sr2::kCharTableRecordOffset);
    }
    return visuals;
}

int32_t downedTimerOf(uintptr_t human) {
    return static_cast<int32_t>(read32(human + sr2::kHumanDownedTimerOffset));
}

bool isJoining(uintptr_t human) {
    return (read8(human + sr2::kHumanJoinFlagsOffset) & sr2::kHumanJoiningBit) != 0;
}

bool nearestRemote(const partner_rules::Vec3& point, partner_rules::Remote& out) {
    Snapshot snapshot;
    take(snapshot);
    const partner_rules::Remote* remotes[kMaxWalked];
    partner_rules::Vec3 positions[kMaxWalked];
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (!snapshot.pairing.items[i].human) continue;
        remotes[count] = &snapshot.pairing.items[i];
        positions[count] = positionOf(snapshot.pairing.items[i].human);
        ++count;
    }
    const size_t nearest = partner_rules::nearestIndex(positions, count, point);
    if (nearest == partner_rules::kNone) return false;
    out = *remotes[nearest];
    return true;
}

uintptr_t nearestRemoteTo(const partner_rules::Vec3& point) {
    partner_rules::Remote remote;
    return nearestRemote(point, remote) ? remote.human : 0;
}

uint64_t signatureOf(const Snapshot& snapshot) {
    uint64_t hash = mix(0, snapshot.localMember);
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        hash = mix(mix(mix(hash, snapshot.members[i].address), snapshot.members[i].slot), snapshot.members[i].state);
    }
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        const uintptr_t human = snapshot.humans[i].human;
        hash = mix(mix(mix(hash, human), snapshot.humans[i].ownerSlot), downedTimerOf(human));
        hash = mix(hash, isJoining(human));
        const Visuals visuals = visualsOf(human);
        hash = mix(mix(mix(hash, visuals.hidden), visuals.render), visuals.skin);
        hash = mix(mix(hash, visuals.type), visuals.record);
    }
    return hash;
}

std::vector<std::string> describeRemotes(const Snapshot& snapshot) {
    const Partner partner = defaultPartner(snapshot);
    std::vector<std::string> lines;
    for (size_t i = 0; i < snapshot.humanCount; ++i) {
        const HumanEntry& entry = snapshot.humans[i];
        uintptr_t member = 0;
        for (size_t m = 0; m < snapshot.memberCount; ++m) {
            if (!snapshot.members[m].local && snapshot.members[m].slot == entry.ownerSlot) member = snapshot.members[m].address;
        }
        char memberText[16] = "none";
        if (member) snprintf(memberText, sizeof(memberText), "0x%X", static_cast<unsigned>(member));
        const Visuals visuals = visualsOf(entry.human);
        char line[320];
        snprintf(line, sizeof(line), "remote human 0x%X owner slot %u member %s flags 0x%02X downed %d hidden 0x%02X render 0x%X skin %u type 0x%X record 0x%X%s\n",
                 static_cast<unsigned>(entry.human), entry.ownerSlot, memberText,
                 read8(entry.human + sr2::kHumanJoinFlagsOffset), downedTimerOf(entry.human), visuals.hidden, visuals.render,
                 visuals.skin ? 1u : 0u, visuals.type, visuals.record, entry.human == partner.human ? " default partner" : "");
        lines.push_back(line);
    }
    return lines;
}

}  // namespace remote_players
