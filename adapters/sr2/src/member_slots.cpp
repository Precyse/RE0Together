#include "member_slots.h"

#include <mutex>
#include <string>
#include <vector>

#include "log.h"
#include "remote_players.h"
#include "replication_sites.h"
#include "replication_switch.h"

namespace {

std::mutex g_mutex;
std::string g_lastLogged;

std::string describe(const remote_players::MemberInfo& member) {
    char text[96];
    snprintf(text, sizeof(text), "member 0x%X slot %u state %u %s", static_cast<unsigned>(member.address), member.slot,
             member.state, member.local ? "local" : "remote");
    return text;
}

}  // namespace

namespace member_slots {

void check(uint32_t maxPlayers) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    if (!snapshot.active) remote_players::takeMembers(snapshot);
    std::string summary;
    for (size_t i = 0; i < snapshot.memberCount; ++i) summary += describe(snapshot.members[i]) + "\n";
    const std::vector<std::string> remotes = remote_players::describeRemotes(snapshot);
    for (const std::string& line : remotes) summary += line + "\n";

    std::lock_guard<std::mutex> lock(g_mutex);
    if (summary != g_lastLogged) {
        g_lastLogged = summary;
        logger::write("trace: session members %zu", snapshot.memberCount);
        for (size_t i = 0; i < snapshot.memberCount; ++i) logger::write("trace: %s", describe(snapshot.members[i]).c_str());
        for (const std::string& line : remotes) logger::write("trace: %s", line.c_str());
    }
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        const remote_players::MemberInfo& member = snapshot.members[i];
        if (member.local || replication_sites::isSessionSlot(member.slot, maxPlayers)) continue;
        logger::write("trace: remote %s is outside the session slots 0..%u", describe(member).c_str(), maxPlayers - 1);
        replication_switch::fallBackToSinglePeer();
    }
}

}  // namespace member_slots
