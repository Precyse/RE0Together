#pragma once
#include <optional>
#include <string>

// How the game's UDP reaches the other players (docs/SR2_NOTES.md "Steam tunnel"). Native leaves the game's sockets
// alone (OpenSpy, NAT negotiation); Steam carries every datagram through the launcher's Steam connection.
enum class Transport { Native, Steam };

inline constexpr const char* kTransportNative = "native";
inline constexpr const char* kTransportSteam = "steam";

inline std::optional<Transport> parseTransport(const std::string& value) {
    if (value == kTransportNative) return Transport::Native;
    if (value == kTransportSteam) return Transport::Steam;
    return std::nullopt;
}

inline const char* transportName(Transport transport) {
    return transport == Transport::Steam ? kTransportSteam : kTransportNative;
}
