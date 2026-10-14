// The milestone 8 trailer (no game): its layout, reading it back from the end of a payload, and the verdicts.
#include <cstdio>
#include <vector>

#include "../src/trailer_rules.h"

namespace {

using trailer_rules::Settings;
using trailer_rules::Verdict;

constexpr uint8_t kFourPlayers = 4;
constexpr size_t kPayloadBytes = 115;  // a join_req of the logs

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// A payload of `size` filler bytes ending in `trailer`.
std::vector<uint8_t> payloadWith(const std::array<uint8_t, version::kTrailerBytes>& trailer, size_t size = kPayloadBytes) {
    std::vector<uint8_t> payload(size, 0x41);
    std::copy(trailer.begin(), trailer.end(), payload.end() - trailer.size());
    return payload;
}

}  // namespace

int main() {
    const Settings settings{kFourPlayers, false};
    const auto bytes = trailer_rules::make(settings);
    check(version::kTrailerBytes == 8 && bytes.size() == 8, "the trailer is 8 bytes");
    check(bytes[0] == 'C' && bytes[1] == 'F' && bytes[2] == 'V' && bytes[3] == 'G', "it starts with CFVG");
    check(bytes[4] == (version::kProtocolVersion & 0xFF) && bytes[5] == (version::kProtocolVersion >> 8),
          "then the protocol version, little endian");
    check(bytes[6] == kFourPlayers && bytes[7] == 0, "then max_players and the flags");
    check(trailer_rules::make({kFourPlayers, true})[7] == trailer_rules::kFlagPartnerExperimental,
          "partner_experimental is bit 0 of the flags");

    const auto payload = payloadWith(bytes);
    check(trailer_rules::check(payload.data(), payload.size(), settings) == Verdict::Match,
          "a payload that ends in our trailer matches");
    const auto read = trailer_rules::read(payload.data(), payload.size());
    check(read && *read == trailer_rules::ours(settings), "the trailer is read back from the end of the payload");

    const std::vector<uint8_t> plain(kPayloadBytes, 0x41);
    check(trailer_rules::check(plain.data(), plain.size(), settings) == Verdict::NoTrailer, "an unmodded payload has none");
    check(trailer_rules::check(plain.data(), 0, settings) == Verdict::NoTrailer &&
              trailer_rules::check(plain.data(), version::kTrailerBytes - 1, settings) == Verdict::NoTrailer,
          "a payload shorter than the trailer has none");
    check(trailer_rules::check(payload.data(), payload.size() - 1, settings) == Verdict::NoTrailer,
          "the trailer must be at the very end");

    std::vector<uint8_t> older = payload;
    older[older.size() - 4] = static_cast<uint8_t>(version::kProtocolVersion + 1);
    check(trailer_rules::check(older.data(), older.size(), settings) == Verdict::VersionDiffers, "another version is refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers + 1, false}) == Verdict::MaxPlayersDiffers,
          "another max_players is refused");
    check(trailer_rules::check(payload.data(), payload.size(), {kFourPlayers, true}) == Verdict::FlagsDiffer,
          "other patched sites (flags) are refused");

    const auto exact = payloadWith(bytes, version::kTrailerBytes);
    check(trailer_rules::check(exact.data(), exact.size(), settings) == Verdict::Match, "a payload that is only the trailer matches");

    check(trailer_rules::fitsInSend(kPayloadBytes + 3) && trailer_rules::fitsInSend(0x1FF - version::kTrailerBytes - 1) &&
              !trailer_rules::fitsInSend(0x1FF - version::kTrailerBytes),
          "a message may grow by 8 bytes while the send's limit of 0x1FF holds");
    return g_failures == 0 ? 0 : 1;
}
