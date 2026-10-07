#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

struct PeerInfo {
    uint8_t slot = 0;
    uint64_t steamId = 0;
    std::string name;
    uint16_t rttMs = 0;
    uint32_t joinSerial = 0;  // counts the PEER_UPs this client has seen: a peer that replaces one on its slot gets a new number
};

struct SessionSnapshot {
    bool linked = false;  // WELCOME received on the current connection
    uint8_t localSlot = 0;
    uint8_t hostSlot = 0;
    uint8_t maxPlayers = 0;
    uint32_t epoch = 0;
    std::vector<PeerInfo> peers;
};

struct GameFrame {
    uint16_t type = 0;
    uint8_t flags = 0;
    uint8_t slot = 0;  // source slot
    std::vector<uint8_t> payload;
};

// Loopback client of the launcher (docs/CONTRACT.md). Runs on its own thread, never blocks callers.
class NetClient {
public:
    using TickHandler = std::function<void()>;
    using FrameHandler = std::function<void(const GameFrame&)>;

    ~NetClient() { stop(); }

    // onTick runs on the net thread every few milliseconds while connected.
    void start(uint16_t port, std::string gameId, TickHandler onTick = {});
    void stop();

    // Queues a game frame (type >= 0x0100). False while not linked.
    bool send(uint16_t type, bool reliable, uint8_t destSlot, std::span<const uint8_t> payload);

    // Drains received game frames into the handler and returns the current session.
    SessionSnapshot poll(const FrameHandler& handler);

private:
    void run();
    void serve(uintptr_t socket);
    bool handleFrame(uint16_t type, uint8_t flags, uint8_t slot, std::span<const uint8_t> payload);
    bool parseFrames(std::vector<uint8_t>& rx);
    bool flushOutbox(uintptr_t socket);
    void resetSession();
    void sleepWhileRunning(unsigned ms);

    uint16_t port_ = 0;
    std::string gameId_;
    TickHandler onTick_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex mutex_;
    SessionSnapshot session_;
    std::vector<GameFrame> inbox_;
    uint32_t joinCounter_ = 0;  // under mutex_
    std::vector<std::vector<uint8_t>> outbox_;
};
