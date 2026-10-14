#include "game_port.h"

#include "code_patch.h"
#include "game_port_patches.h"
#include "game_port_rules.h"
#include "local_port_relay.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

constexpr uintptr_t kMemberSessionOffset = 0x1AC;  // pointer to the session the member belongs to

bool g_trace = false;

// E3: the quick check for a peer behind our own public IP (esi = the peer's member). Its address is already its private
// IP; the port word is the one the host relayed, 4200 for the host.
void __cdecl choosePeerPort(uintptr_t member) {
    const uintptr_t session = readDword(member + kMemberSessionOffset);
    const bool peerIsHost = session == 0 || readDword(session + sr2::kSessionHostMemberOffset) == member;
    uint16_t relayed = 0;
    readMemory(member + sr2::kMemberSourcePortOffset, &relayed, sizeof(relayed));
    const uint16_t port = game_port_rules::peerPort(peerIsHost, relayed);
    *reinterpret_cast<uint16_t*>(member + sr2::kMemberSourcePortOffset) = port;
    if (g_trace) {
        logger::write("trace: quick check for member 0x%X, %s, relayed port %u, port used %u", static_cast<unsigned>(member),
                      peerIsHost ? "the host" : "another guest", relayed, port);
    }
}

// Replaces the `mov word ptr [esi+0x2C], 4200` and keeps every register and flag as the mov did.
void __declspec(naked) quickCheckStub() {
    __asm {
        pushfd
        pushad
        push esi
        call choosePeerPort
        add esp, 4
        popad
        popfd
        ret
    }
}

}  // namespace

namespace game_port {

bool install(uint32_t port, uint32_t maxPlayers, bool trace) {
    g_trace = trace;
    local_port_relay::setTrace(trace);
    const local_port_relay::Thunks relay = local_port_relay::thunks();
    const game_port_patches::Thunks thunks{reinterpret_cast<uintptr_t>(&quickCheckStub), relay.reportedPort,
                                           relay.relayedRecordPort, relay.unmappedSender};
    const auto patches = game_port_patches::forPort(port, maxPlayers, trace, thunks);
    if (patches.empty()) return false;
    if (!applyCodePatches(patches)) {
        logger::write("game port: sites differ, the game keeps port %u", game_port_rules::kShippedGamePort);
        return false;
    }
    if (trace) {
        const bool replicating = maxPlayers > sr2::kCoopDefaultPlayers;
        logger::write("game port: %u in use, bind port edit (E1) %s, advertised ports, quick check and local port relay "
                      "(E2, E3, H1, H2) %s, unmapped sender trace (T11) on",
                      port, port != game_port_rules::kShippedGamePort ? "applied" : "not needed",
                      replicating ? "applied" : "off at 2 players");
    }
    return true;
}

}  // namespace game_port
