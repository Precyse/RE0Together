#include "game_socket.h"

#include <winsock2.h>

#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

constexpr DWORD kPollIntervalMs = 100;
constexpr DWORD kMsPerSecond = 1000;
constexpr DWORD kGiveUpAfterSeconds = 120;
constexpr DWORD kSioUdpConnReset = _WSAIOW(IOC_VENDOR, 12);
constexpr uint32_t kSocketReady = 1;

DWORD WINAPI waitAndConfigure(LPVOID) {
    for (DWORD waited = 0; waited < kGiveUpAfterSeconds * kMsPerSecond; waited += kPollIntervalMs) {
        const SOCKET socket = readDword(sr2::kGameSocket);
        if (readDword(sr2::kGameSocketReady) == kSocketReady && socket != INVALID_SOCKET) {
            const BOOL enable = FALSE;
            DWORD returned = 0;
            if (WSAIoctl(socket, kSioUdpConnReset, const_cast<BOOL*>(&enable), sizeof(enable), nullptr, 0, &returned, nullptr,
                         nullptr) == 0) {
                logger::write("game socket: connection reset reports off");
            } else {
                logger::write("game socket: WSAIoctl failed %d", WSAGetLastError());
            }
            return 0;
        }
        Sleep(kPollIntervalMs);
    }
    logger::write("game socket: not created within %lu s, connection reset reports left on", kGiveUpAfterSeconds);
    return 0;
}

}  // namespace

namespace game_socket {

void start() {
    if (HANDLE thread = CreateThread(nullptr, 0, waitAndConfigure, nullptr, 0, nullptr)) CloseHandle(thread);
}

}  // namespace game_socket
