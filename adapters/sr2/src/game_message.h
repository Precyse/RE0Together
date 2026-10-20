#pragma once
#include <cstddef>

// The frame of every message the game's transport sends: the id and the u16 payload length come first.
namespace game_message {

constexpr size_t kHeaderBytes = 3;         // the id and the length before a payload
constexpr size_t kWideHeaderBytes = 4;     // a message whose header flag byte is set carries one more header byte
constexpr size_t kSendLengthLimit = 0x1FF;  // the game refuses to send a message of this many bytes or more
constexpr size_t kMaxMessageBytes = kSendLengthLimit + 1;  // room for the longest message the game builds

}  // namespace game_message
