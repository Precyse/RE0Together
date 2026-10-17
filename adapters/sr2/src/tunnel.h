#pragma once
#include <cstdint>
#include <optional>

#include "transport.h"

// The Steam tunnel (docs/SR2_NOTES.md "Steam tunnel"): with the steam transport the game's one UDP socket never touches the
// network. Its send, receive and the two selects are retargeted here, and every datagram travels to the other adapters as a
// GAME_DATAGRAM frame through the launcher. Peers are 10.a.b.c addresses derived from their SteamIDs.
namespace tunnel {

// Steam: checks the site table and writes it all or nothing, then starts the launcher link. A difference logs
// `tunnel: sites differ, steam transport off` and leaves the game as it was. Native: logs and does nothing. True when the
// tunnel is installed.
bool install(Transport transport, bool trace);

// The WinSock calls the exe is retargeted to (stdcall, the signatures of sendto, select and recvfrom), as addresses.
struct Calls {
    uintptr_t sendTo;
    uintptr_t selectReadable;
    uintptr_t receiveFrom;
};
Calls calls();

// Starts the launcher link on its own: install does it once the exe edits are written; a stand-in program that calls the
// replacements itself (tests/tunnel_standin.cpp) starts it directly, with the address of its own game socket variable.
void startLink(bool trace, uintptr_t gameSocketVariable);

// Our virtual address, once the launcher's WELCOME carried our SteamID.
std::optional<uint32_t> ownAddress();

// The host's virtual address while the link is up and the host is a known member (our own when we host).
std::optional<uint32_t> hostAddress();

}  // namespace tunnel
