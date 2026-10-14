#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "game_port_rules.h"

// The code edits of milestones 5e and 5g as data (pure, unit tested): a per-machine game port, so that guests behind one
// NAT keep distinct external ports, and the host passing each guest's local port on to the other guests
// (docs/SR2_NOTES.md, "Game port").
namespace game_port_patches {

constexpr uintptr_t kBindPortImmediate = 0xC1BBAF;  // E1: mov eax, 4200 before the game socket is bound
constexpr uintptr_t kQuickCheckPort = 0x8CDA3B;     // E3: mov word ptr [esi+0x2C], 4200
constexpr size_t kQuickCheckBytes = 6;              // the mov; its replacement is a call and a nop
constexpr uintptr_t kPortWordOffset = 4;            // in an E2 mov: the imm16
constexpr uint8_t kNop = 0x90;

constexpr uintptr_t kReportedPortCapture = 0x815D8C;  // H1: mov eax, [esp+0x14]; mov eax, [eax] in the join_req handler
constexpr size_t kReportedPortBytes = 6;              // its replacement is a call and a nop
constexpr uintptr_t kRecordPortCall = 0x815666;       // H2: call 0x7FB3D0 in the new_conn record builder
constexpr uintptr_t kBuildAddressRecord = 0x7FB3D0;
constexpr uintptr_t kSenderLookupCall = 0x813BF2;     // T11: call 0x7FA070 in the receive loop
constexpr uintptr_t kMapSender = 0x7FA070;

// Where the edits go to.
struct Thunks {
    uintptr_t quickCheck;         // E3
    uintptr_t reportedPort;       // H1
    uintptr_t relayedRecordPort;  // H2
    uintptr_t unmappedSender;     // T11
};

// E2: `mov word ptr [reg+4], 4200` that stores our own advertised address record's port; the byte after 0x66 0xC7 is the
// register form of each.
struct AdvertisedPortSite {
    uintptr_t address;
    uint8_t modrm;
};
inline constexpr AdvertisedPortSite kAdvertisedPortSites[] = {
    {0x811AC0, 0x40}, {0x811B11, 0x46}, {0x811BA4, 0x46}, {0x811C0D, 0x47}, {0x811C7A, 0x47}, {0x811CB7, 0x41},
};

inline uint8_t low(uint32_t port) { return static_cast<uint8_t>(port); }
inline uint8_t high(uint32_t port) { return static_cast<uint8_t>(port >> 8); }

// Every site is checked together; E1 and E2 are written only for a port other than 4200, E2, E3, H1 and H2 only with 3 or
// more players, T11 only with `trace`. Nothing at all when none would be written.
inline std::vector<CodePatch> forPort(uint32_t port, uint32_t maxPlayers, bool trace, const Thunks& thunks) {
    const bool moved = port != game_port_rules::kShippedGamePort;
    const bool replicating = maxPlayers > 2;
    if (!moved && !replicating && !trace) return {};
    const uint8_t shippedLow = low(game_port_rules::kShippedGamePort);
    const uint8_t shippedHigh = high(game_port_rules::kShippedGamePort);

    std::vector<CodePatch> patches;
    patches.push_back({"game_port_bind", kBindPortImmediate, {0xB8, shippedLow, shippedHigh, 0x00, 0x00},
                       moved ? std::vector<uint8_t>{0xB8, low(port), high(port), 0x00, 0x00} : std::vector<uint8_t>{}});
    if (replicating) {
        for (const AdvertisedPortSite& site : kAdvertisedPortSites) {
            const std::vector<uint8_t> shipped = {0x66, 0xC7, site.modrm, 0x04, shippedLow, shippedHigh};
            std::vector<uint8_t> written = shipped;
            written[kPortWordOffset] = low(port);
            written[kPortWordOffset + 1] = high(port);
            patches.push_back({"game_port_advertised", site.address, shipped, moved ? written : std::vector<uint8_t>{}});
        }
        const auto call = relativeBranch(kCallRel32, kQuickCheckPort, thunks.quickCheck);
        std::vector<uint8_t> callAndNop(call.begin(), call.end());
        callAndNop.resize(kQuickCheckBytes, kNop);
        patches.push_back({"game_port_quick_check", kQuickCheckPort, {0x66, 0xC7, 0x46, 0x2C, shippedLow, shippedHigh}, callAndNop});

        const auto reported = relativeBranch(kCallRel32, kReportedPortCapture, thunks.reportedPort);
        std::vector<uint8_t> reportedAndNop(reported.begin(), reported.end());
        reportedAndNop.resize(kReportedPortBytes, kNop);
        patches.push_back({"game_port_reported_port", kReportedPortCapture, {0x8B, 0x44, 0x24, 0x14, 0x8B, 0x00}, reportedAndNop});
        patches.push_back(retargetedCall("game_port_relayed_record", kRecordPortCall, kBuildAddressRecord, thunks.relayedRecordPort));
    }
    if (trace) {
        patches.push_back(retargetedCall("game_port_unmapped_sender", kSenderLookupCall, kMapSender, thunks.unmappedSender));
    }
    return patches;
}

}  // namespace game_port_patches
