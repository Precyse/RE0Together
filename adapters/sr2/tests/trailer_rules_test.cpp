// The version gate trailer (no game): its layout (the content hash in front of the milestone 8 trailer), reading it back
// from the end of a payload, an old 8 byte peer, the verdicts in order, the refusal texts and the browser tag.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/trailer_rules.h"

namespace {

using trailer_rules::Settings;
using trailer_rules::Trailer;
using trailer_rules::Verdict;

constexpr uint8_t kFourPlayers = 4;
constexpr size_t kPayloadBytes = 115;  // a join_req of the logs
constexpr uint64_t kHash = 0x1122334455667788ull;
constexpr uint16_t kOldVersion = 2;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// A payload of `size` filler bytes ending in `trailer`.
std::vector<uint8_t> payloadWith(const std::vector<uint8_t>& trailer, size_t size = kPayloadBytes) {
    std::vector<uint8_t> payload(size, 0x41);
    std::copy(trailer.begin(), trailer.end(), payload.end() - trailer.size());
    return payload;
}

std::vector<uint8_t> bytesOf(const Settings& settings) {
    const auto trailer = trailer_rules::make(settings);
    return {trailer.begin(), trailer.end()};
}

// What a milestone 8 peer (protocol version 2) sends: the last 8 bytes alone.
std::vector<uint8_t> oldPeerTrailer(uint8_t maxPlayers, uint8_t flags) {
    return {'C', 'F', 'V', 'G', static_cast<uint8_t>(kOldVersion), 0, maxPlayers, flags};
}

Verdict verdictOf(const std::vector<uint8_t>& payload, const Settings& mine) {
    return trailer_rules::check(payload.data(), payload.size(), mine);
}

}  // namespace

int main() {
    const Settings settings{kFourPlayers, false, false, kHash};
    const auto bytes = trailer_rules::make(settings);
    check(version::kProtocolVersion == 3 && version::kTrailerBytes == 16 && bytes.size() == 16, "version 3: the trailer is 16 bytes");
    check(bytes[0] == 0x88 && bytes[1] == 0x77 && bytes[7] == 0x11, "it starts with the content hash, little endian");
    check(bytes[8] == 'C' && bytes[9] == 'F' && bytes[10] == 'V' && bytes[11] == 'G', "then CFVG");
    check(bytes[12] == (version::kProtocolVersion & 0xFF) && bytes[13] == (version::kProtocolVersion >> 8),
          "then the protocol version, little endian");
    check(bytes[14] == kFourPlayers && bytes[15] == 0, "then max_players and the flags");
    check(trailer_rules::make({kFourPlayers, true, false, kHash})[15] == trailer_rules::kFlagPartnerExperimental,
          "partner_experimental is bit 0 of the flags");
    check(trailer_rules::make({kFourPlayers, false, true, kHash})[15] == trailer_rules::kFlagSteamTransport &&
              trailer_rules::make({kFourPlayers, true, true, kHash})[15] ==
                  (trailer_rules::kFlagPartnerExperimental | trailer_rules::kFlagSteamTransport),
          "the steam transport is bit 1 of the flags, beside bit 0");

    const auto payload = payloadWith(bytesOf(settings));
    check(verdictOf(payload, settings) == Verdict::Match, "a payload that ends in our trailer matches");
    const auto read = trailer_rules::read(payload.data(), payload.size());
    check(read && *read == trailer_rules::ours(settings) && read->contentHash == kHash,
          "the trailer, hash included, is read back from the end of the payload");

    const std::vector<uint8_t> plain(kPayloadBytes, 0x41);
    check(verdictOf(plain, settings) == Verdict::NoTrailer, "an unmodded payload has none");
    check(trailer_rules::check(plain.data(), 0, settings) == Verdict::NoTrailer &&
              trailer_rules::check(plain.data(), version::kBaseTrailerBytes - 1, settings) == Verdict::NoTrailer,
          "a payload shorter than the milestone 8 trailer has none");
    check(trailer_rules::check(payload.data(), payload.size() - 1, settings) == Verdict::NoTrailer,
          "the trailer must be at the very end");

    const auto oldPayload = payloadWith(oldPeerTrailer(kFourPlayers, 0));
    const auto oldRead = trailer_rules::read(oldPayload.data(), oldPayload.size());
    check(oldRead && oldRead->protocolVersion == kOldVersion && oldRead->contentHash == 0,
          "an old 8 byte peer is read as its version, without a hash");
    check(verdictOf(oldPayload, settings) == Verdict::VersionDiffers, "an old 8 byte peer sees (and is) a version mismatch");
    const auto oldExact = payloadWith(oldPeerTrailer(kFourPlayers, 0), version::kBaseTrailerBytes);
    check(verdictOf(oldExact, settings) == Verdict::VersionDiffers, "also when its payload is only the trailer");
    const auto ours16 = bytesOf(settings);
    const std::vector<uint8_t> tail(ours16.end() - version::kBaseTrailerBytes, ours16.end());
    check(tail == std::vector<uint8_t>{'C', 'F', 'V', 'G', version::kProtocolVersion, 0, kFourPlayers, 0},
          "the last 8 bytes of ours are the milestone 8 trailer with version 3, which is what an old peer reads");

    std::vector<uint8_t> other = payload;
    other[other.size() - 4] = static_cast<uint8_t>(version::kProtocolVersion + 1);
    check(verdictOf(other, settings) == Verdict::VersionDiffers, "another version is refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers + 1, false, false, kHash}) == Verdict::MaxPlayersDiffers,
          "another max_players is refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers, true, false, kHash}) == Verdict::FlagsDiffer,
          "other patched sites (flags) are refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers, false, true, kHash}) == Verdict::FlagsDiffer,
          "a native peer and a steam peer refuse each other");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers, false, false, kHash + 1}) == Verdict::ContentDiffers,
          "another content hash is refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers + 1, true, true, kHash + 1}) == Verdict::MaxPlayersDiffers,
          "the verdict is the first field that differs: version, max_players, flags, then content");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers, true, false, kHash + 1}) == Verdict::FlagsDiffer,
          "flags come before content");

    const auto exact = payloadWith(bytesOf(settings), version::kTrailerBytes);
    check(verdictOf(exact, settings) == Verdict::Match, "a payload that is only the trailer matches");
    const auto shortOfHash = payloadWith(bytesOf(settings), version::kTrailerBytes);
    check(!trailer_rules::read(shortOfHash.data() + 1, shortOfHash.size() - 1), "a version 3 trailer without room for the hash is none");

    check(trailer_rules::fitsInSend(kPayloadBytes + 3) && trailer_rules::fitsInSend(0x1FF - version::kTrailerBytes - 1) &&
              !trailer_rules::fitsInSend(0x1FF - version::kTrailerBytes),
          "a message may grow by 16 bytes while the send's limit of 0x1FF holds");

    const Trailer mine = trailer_rules::ours(settings);
    const std::optional<Trailer> host = Trailer{kOldVersion, kFourPlayers + 1, 0, kHash};
    check(trailer_rules::refusalText(Verdict::Match, host, mine).empty(), "a match has no refusal text");
    check(trailer_rules::refusalText(Verdict::NoTrailer, std::nullopt, mine) == "The host is not running the co-op mod.",
          "no trailer: the host has no mod");
    check(trailer_rules::refusalText(Verdict::VersionDiffers, host, mine) == "Co-op mod versions differ (host 2, yours 3).",
          "version: both numbers");
    check(trailer_rules::refusalText(Verdict::MaxPlayersDiffers, host, mine) == "max_players differs (host 5, yours 4).",
          "max_players: both numbers");
    check(trailer_rules::refusalText(Verdict::FlagsDiffer, host, mine) == "partner_experimental differs from the host.",
          "flags: partner_experimental");
    check(trailer_rules::refusalText(Verdict::ContentDiffers, host, mine) ==
              "Co-op files differ from the host (dinput8.dll or coop\\scripts).",
          "content: the files");
    check(trailer_rules::refusalText(Verdict::VersionDiffers, std::nullopt, mine).size() < 0x80, "a text fits the popup buffer");

    const std::string tag = trailer_rules::tagFor(mine);
    check(tag.size() == 15 && tag.compare(0, 7, "cfv003-") == 0 && force_join_names::isTag(tag),
          "the tag is cfv, the version in three digits, a dash and 8 hex digits");
    check(trailer_rules::tagFor(mine) == tag && trailer_rules::tagFor(trailer_rules::ours(settings)) == tag, "the tag is stable");
    const Settings others[] = {{kFourPlayers + 1, false, false, kHash}, {kFourPlayers, true, false, kHash},
                               {kFourPlayers, false, true, kHash}, {kFourPlayers, false, false, kHash + 1}};
    bool allDiffer = true;
    for (const Settings& variant : others) allDiffer = allDiffer && trailer_rules::tagFor(trailer_rules::ours(variant)) != tag;
    check(allDiffer, "every field the gate compares changes the tag");
    check(trailer_rules::digest(mine) != 0 && static_cast<uint32_t>(trailer_rules::digest(mine)) ==
              std::stoul(tag.substr(7), nullptr, 16),
          "the tag's digest is the low 32 bits of the FNV-1a 64 digest");
    return g_failures == 0 ? 0 : 1;
}
