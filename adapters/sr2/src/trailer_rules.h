#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "version.h"

// The pure rules of milestone 8 (unit tested): the trailer of join_req and join_acc, and whether a peer's matches ours.
//
//   +0  4  magic 'C' 'F' 'V' 'G'
//   +4  2  version::kProtocolVersion
//   +6  1  max_players
//   +7  1  flags: bit 0 = the partner_experimental sites are patched
//
// Two builds with another max_players or other patched sites run different code, which is a wire difference at the same
// version, so all three fields must be equal.
namespace trailer_rules {

constexpr uint8_t kFlagPartnerExperimental = 1;

// What this build puts into the trailer.
struct Settings {
    uint8_t maxPlayers = 0;
    bool partnerExperimental = false;
};

struct Trailer {
    uint16_t protocolVersion;
    uint8_t maxPlayers;
    uint8_t flags;

    bool operator==(const Trailer&) const = default;
};

inline Trailer ours(const Settings& settings) {
    return {version::kProtocolVersion, settings.maxPlayers,
            static_cast<uint8_t>(settings.partnerExperimental ? kFlagPartnerExperimental : 0)};
}

inline std::array<uint8_t, version::kTrailerBytes> make(const Settings& settings) {
    const Trailer trailer = ours(settings);
    std::array<uint8_t, version::kTrailerBytes> bytes{};
    memcpy(bytes.data(), &version::kTrailerMagic, sizeof(version::kTrailerMagic));
    memcpy(bytes.data() + 4, &trailer.protocolVersion, sizeof(trailer.protocolVersion));
    bytes[6] = trailer.maxPlayers;
    bytes[7] = trailer.flags;
    return bytes;
}

// The trailer at the end of a message payload of `length` bytes, or none when the payload is too short or ends in
// something else.
inline std::optional<Trailer> read(const uint8_t* payload, size_t length) {
    if (length < version::kTrailerBytes) return std::nullopt;
    const uint8_t* end = payload + length - version::kTrailerBytes;
    uint32_t magic = 0;
    memcpy(&magic, end, sizeof(magic));
    if (magic != version::kTrailerMagic) return std::nullopt;
    Trailer trailer{};
    memcpy(&trailer.protocolVersion, end + 4, sizeof(trailer.protocolVersion));
    trailer.maxPlayers = end[6];
    trailer.flags = end[7];
    return trailer;
}

enum class Verdict { Match, NoTrailer, VersionDiffers, MaxPlayersDiffers, FlagsDiffer };

inline Verdict check(const uint8_t* payload, size_t length, const Settings& settings) {
    const auto theirs = read(payload, length);
    if (!theirs) return Verdict::NoTrailer;
    const Trailer mine = ours(settings);
    if (theirs->protocolVersion != mine.protocolVersion) return Verdict::VersionDiffers;
    if (theirs->maxPlayers != mine.maxPlayers) return Verdict::MaxPlayersDiffers;
    if (theirs->flags != mine.flags) return Verdict::FlagsDiffer;
    return Verdict::Match;
}

// Where the sender's message goes on: how long a message with the trailer may be (the game's send function refuses a
// message of 0x1FF bytes or more).
constexpr size_t kSendLengthLimit = 0x1FF;

inline bool fitsInSend(size_t length) { return length + version::kTrailerBytes < kSendLengthLimit; }

}  // namespace trailer_rules
