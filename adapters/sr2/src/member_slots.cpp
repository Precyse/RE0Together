#include "member_slots.h"

#include <mutex>
#include <string>
#include <vector>

#include "log.h"
#include "memory.h"
#include "replication_sites.h"
#include "replication_switch.h"
#include "sr2_addresses.h"

namespace {

// The session is built for 12 members; the list is circular, so the walk also stops at a repeat.
constexpr size_t kMaxMembersWalked = 12;

struct Member {
    uintptr_t address;
    uint8_t slot;
    uint32_t state;
    bool local;
};

std::mutex g_mutex;
std::string g_lastLogged;

uint32_t readU32(uintptr_t address) {
    uint32_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

std::vector<Member> readMembers() {
    std::vector<Member> members;
    const uintptr_t session = readU32(sr2::kSessionPointer);
    if (!session) return members;
    const uintptr_t head = readU32(session + sr2::kSessionMemberListOffset);
    const uintptr_t local = readU32(session + sr2::kSessionLocalMemberOffset);
    uintptr_t member = head;
    while (member && members.size() < kMaxMembersWalked) {
        uint8_t slot = 0;
        readMemory(member + sr2::kMemberSlotOffset, &slot, sizeof(slot));
        members.push_back({member, slot, readU32(member + sr2::kMemberStateOffset), member == local});
        member = readU32(member + sr2::kMemberNextOffset);
        if (member == head) break;
    }
    return members;
}

std::string describe(const Member& member) {
    char text[96];
    snprintf(text, sizeof(text), "member 0x%X slot %u state %u %s", static_cast<unsigned>(member.address), member.slot,
             member.state, member.local ? "local" : "remote");
    return text;
}

}  // namespace

namespace member_slots {

void check(uint32_t maxPlayers) {
    const std::vector<Member> members = readMembers();
    std::string summary;
    for (const Member& member : members) summary += describe(member) + "\n";

    std::lock_guard<std::mutex> lock(g_mutex);
    if (summary != g_lastLogged) {
        g_lastLogged = summary;
        logger::write("trace: session members %zu", members.size());
        for (const Member& member : members) logger::write("trace: %s", describe(member).c_str());
    }
    for (const Member& member : members) {
        if (member.local || replication_sites::isSessionSlot(member.slot, maxPlayers)) continue;
        logger::write("trace: remote %s is outside the session slots 0..%u", describe(member).c_str(), maxPlayers - 1);
        replication_switch::fallBackToSinglePeer();
    }
}

}  // namespace member_slots
