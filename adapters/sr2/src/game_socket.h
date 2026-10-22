#pragma once

// The game's one UDP socket (psnet, created once at game start): on Windows an ICMP port unreachable for an earlier
// datagram makes the next receive on a UDP socket fail with WSAECONNRESET, so a player who quit causes wake-ups that
// yield nothing and a delayed real datagram. SIO_UDP_CONNRESET turns that off. Both transports; no code is patched.
namespace game_socket {

// Starts a thread that waits for the socket to exist, then turns the reports off once. Logs the result.
void start();

}  // namespace game_socket
