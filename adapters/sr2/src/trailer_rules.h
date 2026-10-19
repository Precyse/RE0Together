#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "fnv1a.h"
#include "force_join_names.h"
#include "version.h"

// The pure rules of the version gate (unit tested): the trailer of join_req, join_acc and join_deny, whether a peer's
// matches ours, the words shown to a refused player and the browser tag.
//
//   +0   8  content hash (u64 LE, FNV-1a 64 of dinput8.dll and coop\scripts, content_hash_rules.h)
//   +8   4  magic 'C' 'F' 'V' 'G'
//   +12  2  version::kProtocolVersion
//   +14  1  max_players
//   +15  1  flags: bit 1 = the steam transport (the game's UDP goes through the launcher), so two machines on different
//           transports refuse each other; bit 0 was partner_experimental and is always 0 now
//
// The last 8 bytes are the milestone 8 trailer, unchanged, so a version 2 peer still reads a version it does not know.
// The hash is read only from version 3 on. Two builds with another max_players, another transport or other files run
// different code, which is a wire difference at the same version, so all four fields must be equal.
namespace trailer_rules {

constexpr uint8_t kFlagSteamTransport = 2;
constexpr size_t kDigestFieldBytes = 12;  // what the tag's digest covers: version, max_players, flags, hash
constexpr int kDigestHexDigits = 8;

// What this build puts into the trailer.
struct Settings {
    uint8_t maxPlayers = 0;
    bool steamTransport = false;
    uint64_t contentHash = 0;
};

struct Trailer {
    uint16_t protocolVersion;
    uint8_t maxPlayers;
    uint8_t flags;
    uint64_t contentHash;  // 0 in a trailer older than version::kFirstHashedVersion

    bool operator==(const Trailer&) const = default;
};

inline Trailer ours(const Settings& settings) {
    return {version::kProtocolVersion, settings.maxPlayers,
            static_cast<uint8_t>(settings.steamTransport ? kFlagSteamTransport : 0),
            settings.contentHash};
}

inline std::array<uint8_t, version::kTrailerBytes> make(const Settings& settings) {
    const Trailer trailer = ours(settings);
    std::array<uint8_t, version::kTrailerBytes> bytes{};
    uint8_t* base = bytes.data() + version::kHashBytes;
    memcpy(bytes.data(), &trailer.contentHash, sizeof(trailer.contentHash));
    memcpy(base, &version::kTrailerMagic, sizeof(version::kTrailerMagic));
    memcpy(base + 4, &trailer.protocolVersion, sizeof(trailer.protocolVersion));
    base[6] = trailer.maxPlayers;
    base[7] = trailer.flags;
    return bytes;
}

// The trailer at the end of a message payload of `length` bytes, or none when the payload is too short, ends in something
// else, or says it has a hash that is not there.
inline std::optional<Trailer> read(const uint8_t* payload, size_t length) {
    if (length < version::kBaseTrailerBytes) return std::nullopt;
    const uint8_t* base = payload + length - version::kBaseTrailerBytes;
    uint32_t magic = 0;
    memcpy(&magic, base, sizeof(magic));
    if (magic != version::kTrailerMagic) return std::nullopt;
    Trailer trailer{};
    memcpy(&trailer.protocolVersion, base + 4, sizeof(trailer.protocolVersion));
    trailer.maxPlayers = base[6];
    trailer.flags = base[7];
    if (trailer.protocolVersion >= version::kFirstHashedVersion) {
        if (length < version::kTrailerBytes) return std::nullopt;
        memcpy(&trailer.contentHash, base - version::kHashBytes, sizeof(trailer.contentHash));
    }
    return trailer;
}

enum class Verdict { Match, NoTrailer, VersionDiffers, MaxPlayersDiffers, FlagsDiffer, ContentDiffers };

// The first field in which a peer's trailer (none = no mod) differs from ours.
inline Verdict compare(const std::optional<Trailer>& theirs, const Settings& settings) {
    if (!theirs) return Verdict::NoTrailer;
    const Trailer mine = ours(settings);
    if (theirs->protocolVersion != mine.protocolVersion) return Verdict::VersionDiffers;
    if (theirs->maxPlayers != mine.maxPlayers) return Verdict::MaxPlayersDiffers;
    if (theirs->flags != mine.flags) return Verdict::FlagsDiffer;
    if (theirs->contentHash != mine.contentHash) return Verdict::ContentDiffers;
    return Verdict::Match;
}

inline Verdict check(const uint8_t* payload, size_t length, const Settings& settings) {
    return compare(read(payload, length), settings);
}

// The words shown on the refused player's screen. `host` is the host's trailer (none = no mod), `mine` ours.
inline std::string refusalText(Verdict verdict, const std::optional<Trailer>& host, const Trailer& mine) {
    char text[128];
    switch (verdict) {
        case Verdict::Match: return {};
        case Verdict::NoTrailer: return "The host is not running the co-op mod.";
        case Verdict::VersionDiffers:
            snprintf(text, sizeof(text), "Co-op mod versions differ (host %u, yours %u).", host ? host->protocolVersion : 0u,
                     static_cast<unsigned>(mine.protocolVersion));
            return text;
        case Verdict::MaxPlayersDiffers:
            snprintf(text, sizeof(text), "max_players differs (host %u, yours %u).", host ? host->maxPlayers : 0u,
                     static_cast<unsigned>(mine.maxPlayers));
            return text;
        case Verdict::FlagsDiffer: return "Network setting differs from the host (Steam or Direct).";
        case Verdict::ContentDiffers: return "Co-op files differ from the host (dinput8.dll or coop\\scripts).";
    }
    return {};
}

// One digest of every field the gate compares (FNV-1a 64 of version, max_players, flags and hash, little endian).
inline uint64_t digest(const Trailer& trailer) {
    uint8_t bytes[kDigestFieldBytes];
    memcpy(bytes, &trailer.protocolVersion, sizeof(trailer.protocolVersion));
    bytes[2] = trailer.maxPlayers;
    bytes[3] = trailer.flags;
    memcpy(bytes + 4, &trailer.contentHash, sizeof(trailer.contentHash));
    return fnv1a::hash64(bytes, sizeof(bytes));
}

// The browser tag `cfv<VVV>-<digest>`: the version in three digits and the low 32 bits of the digest in hex, so a browser
// filter lists only hosts the gate would admit.
inline std::string tagFor(const Trailer& trailer) {
    char tag[32];
    snprintf(tag, sizeof(tag), "%s%0*u%c%0*X", force_join_names::kTagPrefix, force_join_names::kVersionDigits,
             static_cast<unsigned>(trailer.protocolVersion), force_join_names::kTokenSeparator, kDigestHexDigits,
             static_cast<uint32_t>(digest(trailer)));
    return tag;
}

// Where the sender's message goes on: how long a message with the trailer may be (the game's send function refuses a
// message of 0x1FF bytes or more).
constexpr size_t kSendLengthLimit = 0x1FF;

inline bool fitsInSend(size_t length) { return length + version::kTrailerBytes < kSendLengthLimit; }

}  // namespace trailer_rules
