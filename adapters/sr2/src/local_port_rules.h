#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

// The pure rules of milestone 5g (unit tested): the host remembers the local port each guest reports and puts it into the
// address records it sends to the other guests, and the trace names senders from private address ranges once.
namespace local_port_rules {

constexpr size_t kMaxSources = 16;
constexpr size_t kMaxSeen = 32;
constexpr size_t kRecordPortOffset = 13;  // in an address record: public IP 4, private IP 4, flag 1, IP 4, then the port

// Guests' local ports by the source address the host saw their join_req come from (IP as stored in memory, port as the
// game stores it). The oldest entry makes room when the table is full.
class PortMap {
public:
    void remember(uint32_t ip, uint16_t port, uint16_t localPort) {
        if (localPort == 0) return;
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].ip == ip && entries_[i].port == port) {
                entries_[i].localPort = localPort;
                return;
            }
        }
        entries_[next_] = {ip, port, localPort};
        next_ = (next_ + 1) % kMaxSources;
        if (count_ < kMaxSources) ++count_;
    }

    std::optional<uint16_t> find(uint32_t ip, uint16_t port) const {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].ip == ip && entries_[i].port == port) return entries_[i].localPort;
        }
        return std::nullopt;
    }

private:
    struct Entry {
        uint32_t ip;
        uint16_t port;
        uint16_t localPort;
    };
    Entry entries_[kMaxSources] = {};
    size_t count_ = 0;
    size_t next_ = 0;
};

// 10.0.0.0/8, 172.16.0.0/12 or 192.168.0.0/16; `ip` as stored in memory (the first octet is the lowest byte).
inline bool isPrivateIpv4(uint32_t ip) {
    const uint8_t first = static_cast<uint8_t>(ip);
    const uint8_t second = static_cast<uint8_t>(ip >> 8);
    return first == 10 || (first == 172 && (second & 0xF0) == 16) || (first == 192 && second == 168);
}

// Addresses already reported: true the first time an address is asked about.
class SeenAddresses {
public:
    bool firstTime(uint32_t ip, uint16_t port) {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].ip == ip && entries_[i].port == port) return false;
        }
        entries_[next_] = {ip, port};
        next_ = (next_ + 1) % kMaxSeen;
        if (count_ < kMaxSeen) ++count_;
        return true;
    }

private:
    struct Entry {
        uint32_t ip;
        uint16_t port;
    };
    Entry entries_[kMaxSeen] = {};
    size_t count_ = 0;
    size_t next_ = 0;
};

}  // namespace local_port_rules
