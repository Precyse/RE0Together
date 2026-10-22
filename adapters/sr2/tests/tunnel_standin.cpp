// A stand-in for the game in the one-PC tests of the Steam tunnel (tools/sr2/tunnel_adapter_test.py; not run by ctest).
// It starts the real tunnel (the launcher link, the inbox, the routing) and calls the three replaced WinSock calls the way
// the game's psnet does: sendto to 255.255.255.255:4200 or to a member, select on the game socket, recvfrom. Its datagrams
// only look like the game's (a ping is 20 bytes); the live tests use the game's own.
//
// Usage: tunnel_standin.exe guest|host <seconds>
//   guest  sends a ping to the broadcast address once a second, and a join request to the sender of the first pong
//   host   answers every ping with a pong to the broadcast address
// Every datagram sent and received is printed.
#include <winsock2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "../src/sr2_addresses.h"
#include "../src/tunnel.h"
#include "../src/tunnel_rules.h"

namespace {

constexpr SOCKET kFakeSocket = 0x1234;
SOCKET g_gameSocket = kFakeSocket;  // the variable the tunnel reads the game socket from, like the exe's at 0xE824C0
constexpr DWORD kPollMs = 10;
constexpr DWORD kPingIntervalMs = 1000;
constexpr DWORD kMsPerSecond = 1000;
constexpr long kMicrosPerMs = 1000;
constexpr size_t kReceiveBytes = 0x516;  // the game's receive buffer

// The game's messages start with a 3 byte header: the message id, then the u16 payload length.
constexpr uint8_t kPingId = 0;
constexpr uint8_t kPongId = 1;
constexpr uint8_t kJoinRequestId = 2;
constexpr size_t kPingBytes = 20;
constexpr size_t kPongBytes = 120;
constexpr size_t kJoinRequestBytes = 115;

using SendToFn = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using SelectFn = int(WSAAPI*)(int, fd_set*, fd_set*, fd_set*, const timeval*);
using RecvFromFn = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);

struct GameCalls {
    SendToFn sendTo;
    SelectFn select;
    RecvFromFn receiveFrom;
};

struct Received {
    uint32_t source;
    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> message(uint8_t id, size_t size) {
    std::vector<uint8_t> bytes(size, 0);
    bytes[0] = id;
    const uint16_t payload = static_cast<uint16_t>(size - 3);
    memcpy(bytes.data() + 1, &payload, sizeof(payload));
    return bytes;
}

void send(const GameCalls& game, uint32_t destination, const std::vector<uint8_t>& bytes) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(sr2::kGamePort));
    to.sin_addr.s_addr = htonl(destination);
    game.sendTo(kFakeSocket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    std::printf("standin: sent %zu bytes, id %u, to %s\n", bytes.size(), bytes[0], tunnel_rules::formatAddress(destination).c_str());
    std::fflush(stdout);
}

// The pump of psnet: select on the game socket, then recvfrom.
std::optional<Received> receive(const GameCalls& game) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(kFakeSocket, &readable);
    const timeval timeout{0, static_cast<long>(kPollMs) * kMicrosPerMs};
    if (game.select(0, &readable, nullptr, nullptr, &timeout) != 1 || readable.fd_count != 1) return std::nullopt;
    std::vector<uint8_t> buffer(kReceiveBytes);
    sockaddr_in from{};
    int fromLength = sizeof(from);
    const int size = game.receiveFrom(kFakeSocket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                      reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (size <= 0) return std::nullopt;
    buffer.resize(static_cast<size_t>(size));
    std::printf("standin: received %d bytes, id %u, from %s:%u\n", size, buffer[0],
                tunnel_rules::formatAddress(ntohl(from.sin_addr.s_addr)).c_str(), ntohs(from.sin_port));
    std::fflush(stdout);
    return Received{ntohl(from.sin_addr.s_addr), buffer};
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || (std::string(argv[1]) != "guest" && std::string(argv[1]) != "host")) {
        std::printf("usage: tunnel_standin.exe guest|host <seconds>\n");
        return 2;
    }
    const bool guest = std::string(argv[1]) == "guest";
    const DWORD runMs = static_cast<DWORD>(std::atoi(argv[2])) * kMsPerSecond;

    tunnel::startLink(false, reinterpret_cast<uintptr_t>(&g_gameSocket));
    const tunnel::Calls calls = tunnel::calls();
    const GameCalls game{reinterpret_cast<SendToFn>(calls.sendTo), reinterpret_cast<SelectFn>(calls.selectReadable),
                         reinterpret_cast<RecvFromFn>(calls.receiveFrom)};

    bool ownReported = false;
    bool joinSent = false;
    DWORD lastPing = 0;
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < runMs) {
        if (!ownReported) {
            if (const auto own = tunnel::ownAddress()) {
                std::printf("standin: own address %s\n", tunnel_rules::formatAddress(*own).c_str());
                ownReported = true;
            }
        }
        if (guest && GetTickCount() - lastPing >= kPingIntervalMs) {
            lastPing = GetTickCount();
            send(game, tunnel_rules::kBroadcast, message(kPingId, kPingBytes));
        }
        const auto received = receive(game);
        if (!received) continue;
        if (!guest && received->bytes[0] == kPingId) send(game, tunnel_rules::kBroadcast, message(kPongId, kPongBytes));
        if (guest && received->bytes[0] == kPongId && !joinSent) {
            joinSent = true;
            send(game, received->source, message(kJoinRequestId, kJoinRequestBytes));
        }
    }
    return 0;
}
