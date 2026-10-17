#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "force_join_sites.h"
#include "lan_address_patches.h"
#include "partner_sites.h"
#include "transport.h"

// The exe edits of the Steam tunnel as data (pure, unit tested; docs/SR2_NOTES.md "Steam tunnel"). Every byte was read from
// SR2_pc.exe. The game has one UDP socket, one send (psnet at 0xC1B5A2) and one receive (0xC1BDBC, gated by two selects),
// and all are WinSock stdcall calls, so a call retarget needs no naked stub. In native mode no site is written.
namespace tunnel_sites {

// The WinSock import thunks the game's calls go through.
constexpr uintptr_t kSendToThunk = 0xC2356E;
constexpr uintptr_t kRecvFromThunk = 0xC2357A;
constexpr uintptr_t kSelectThunk = 0xC23598;
constexpr uintptr_t kIsSetThunk = 0xC2359E;

constexpr uintptr_t kSendCall = 0xC1B5A2;         // S1: the one sendto of the game socket
constexpr uintptr_t kPumpSelectCall = 0xC1BD73;   // S2: the receive pump's select, timeout 0
constexpr uintptr_t kPumpIsSetCall = 0xC1BD89;    // S3: its __WSAFDIsSet (sees the unchanged fd_set)
constexpr uintptr_t kReceiveCall = 0xC1BDBC;      // S4: the one recvfrom
constexpr uintptr_t kThreadSelectCall = 0x814375; // S5: the net thread's select, 10 ms
constexpr uintptr_t kThreadIsSetCall = 0x81438A;  // S6: its __WSAFDIsSet
constexpr uintptr_t kPrivateEqualsPublic = 0x810CAF;  // A1: je that skips the public address of a logged in player

constexpr uint8_t kJeShort = 0x74;
constexpr uint8_t kJmpShort = 0xEB;
constexpr uint8_t kJeDisplacement = 0x69;

// Where the retargets go to.
struct Thunks {
    uintptr_t sendTo;
    uintptr_t selectReadable;
    uintptr_t receiveFrom;
    uintptr_t hostByName;  // the five own-address calls (lan_address)
};

// Game functions the tunnel runs through: checked only.
inline constexpr force_join_sites::Site kFunctionChecks[] = {
    {"psnet_send", 0xC1B540, "81 EC 30 05 00 00 A1 50"},
    {"psnet_pump", 0xC1BD10, "81 EC 5C 06 00 00 A1 50"},
    {"net_thread", 0x814320, "55 8B EC 83 E4 F8 81 EC"},
    {"own_address", 0x810C70, "83 EC 58 A1 50 0B E8 00"},
};

inline CodePatch checkedCall(const char* name, uintptr_t address, uintptr_t original) {
    const auto call = relativeBranch(kCallRel32, address, original);
    return {name, address, {call.begin(), call.end()}, {}};
}

// All sites, checked together and written together. None for the native transport.
inline std::vector<CodePatch> patches(Transport transport, const Thunks& thunks) {
    if (transport == Transport::Native) return {};
    std::vector<CodePatch> list = {
        retargetedCall("tunnel_send", kSendCall, kSendToThunk, thunks.sendTo),
        retargetedCall("tunnel_pump_select", kPumpSelectCall, kSelectThunk, thunks.selectReadable),
        checkedCall("tunnel_pump_is_set", kPumpIsSetCall, kIsSetThunk),
        retargetedCall("tunnel_receive", kReceiveCall, kRecvFromThunk, thunks.receiveFrom),
        retargetedCall("tunnel_thread_select", kThreadSelectCall, kSelectThunk, thunks.selectReadable),
        checkedCall("tunnel_thread_is_set", kThreadIsSetCall, kIsSetThunk),
        {"tunnel_public_address", kPrivateEqualsPublic, {kJeShort, kJeDisplacement}, {kJmpShort, kJeDisplacement}},
    };
    for (const CodePatch& call : lan_address_patches::forTunnel(thunks.hostByName)) list.push_back(call);
    for (const force_join_sites::Site& site : kFunctionChecks) {
        list.push_back({site.id, site.address, partner_sites::parseHex(site.expected), {}});
    }
    for (const CodePatch& check : force_join_sites::lanChecks()) list.push_back(check);
    return list;
}

}  // namespace tunnel_sites
