// Drives NetClient against a running launcher.
// Usage: net_test [--port N] [--seconds N] [--expect-peer]
// Exit 0 when WELCOME arrived and, with --expect-peer, a PLAYER_STATE and a PAD_FRAME from another slot round-tripped.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/net_client.h"
#include "../src/pad_frame.h"
#include "../src/state_sync.h"

namespace {

constexpr uint16_t kDefaultPort = 27960;
constexpr int kDefaultSeconds = 8;
constexpr DWORD kSendIntervalMs = 33;
constexpr DWORD kSleepMs = 5;
constexpr float kPosBase = 100.0f;
constexpr uint16_t kTestRoom = 0x0203;

struct Options {
    uint16_t port = kDefaultPort;
    int seconds = kDefaultSeconds;
    bool expectPeer = false;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) o.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--seconds" && i + 1 < argc) o.seconds = std::atoi(argv[++i]);
        else if (arg == "--expect-peer") o.expectPeer = true;
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseArgs(argc, argv);
    NetClient net;
    net.start(options.port, "re0");

    bool welcomed = false;
    bool roundTripped = false;
    bool padRoundTripped = false;
    uint32_t seq = 0;
    const DWORD begin = GetTickCount();
    DWORD lastSend = 0;

    while (GetTickCount() - begin < static_cast<DWORD>(options.seconds) * 1000) {
        const SessionSnapshot session = net.poll([&](const GameFrame& frame) {
            if (frame.type == pad::kMsgPadFrame) {
                padRoundTripped = padRoundTripped || frame.payload.size() == sizeof(uint32_t) + sizeof(pad::PadFrame);
                return;
            }
            if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState)) {
                std::printf("GAME type=0x%04x from=%u len=%zu\n", frame.type, frame.slot, frame.payload.size());
                return;
            }
            state_sync::PlayerState s;
            std::memcpy(&s, frame.payload.data(), sizeof(s));
            std::printf("PLAYER_STATE from=%u seq=%u room=0x%04x pos=(%.1f, %.1f, %.1f) quat_w=%.1f\n", frame.slot,
                        s.seq, s.room, s.pos[0], s.pos[1], s.pos[2], s.quat[3]);
            roundTripped = s.room == kTestRoom;
        });
        if (session.linked && !welcomed) {
            welcomed = true;
            std::printf("WELCOME local=%u host=%u max=%u epoch=%u peers=%zu\n", session.localSlot, session.hostSlot,
                        session.maxPlayers, session.epoch, session.peers.size());
        }
        if (session.linked && GetTickCount() - lastSend >= kSendIntervalMs) {
            lastSend = GetTickCount();
            state_sync::PlayerState state{++seq, {kPosBase + session.localSlot, 2.0f, 3.0f}, {0, 0, 0, 1.0f}};
            state.room = kTestRoom;
            net.send(state_sync::kMsgPlayerState, false, proto::kSlotAll,
                     {reinterpret_cast<const uint8_t*>(&state), sizeof(state)});
            pad::PadPacket packet{1, {{seq, {}, {}, {}}}};
            net.send(pad::kMsgPadFrame, false, proto::kSlotAll,
                     {reinterpret_cast<const uint8_t*>(&packet), sizeof(packet.count) + sizeof(pad::PadFrame)});
        }
        Sleep(kSleepMs);
    }

    net.stop();
    const bool ok = welcomed && (!options.expectPeer || (roundTripped && padRoundTripped));
    std::printf("%s welcomed=%d roundtrip=%d pad_roundtrip=%d sent=%u\n", ok ? "PASS" : "FAIL", welcomed,
                roundTripped, padRoundTripped, seq);
    return ok ? 0 : 1;
}
