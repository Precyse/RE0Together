// The join_req password field and the refusal reasons that end a join (no game).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/join_deny_rules.h"
#include "../src/version.h"

namespace {

using join_deny_rules::passwordMatches;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// A payload field: `text` and its end mark, then filler.
std::vector<uint8_t> field(const std::string& text, size_t size = join_deny_rules::kMaxPasswordBytes) {
    std::vector<uint8_t> bytes(size, 0x41);
    memcpy(bytes.data(), text.c_str(), text.size() + 1);
    return bytes;
}

}  // namespace

int main() {
    const std::string secret = "123456789";
    check(passwordMatches(field(secret).data(), join_deny_rules::kMaxPasswordBytes, secret), "the secret and its end mark match");
    check(!passwordMatches(field("12345678").data(), join_deny_rules::kMaxPasswordBytes, secret), "a shorter password does not");
    check(!passwordMatches(field("1234567890").data(), join_deny_rules::kMaxPasswordBytes, secret), "a longer one does not");
    check(!passwordMatches(field("123456780").data(), join_deny_rules::kMaxPasswordBytes, secret), "a different one does not");
    check(!passwordMatches(field("").data(), join_deny_rules::kMaxPasswordBytes, secret), "an empty password does not");
    check(passwordMatches(field("").data(), join_deny_rules::kMaxPasswordBytes, ""), "an empty secret matches an empty field");
    const std::vector<uint8_t> unterminated(join_deny_rules::kMaxPasswordBytes, 0x31);
    check(!passwordMatches(unterminated.data(), unterminated.size(), std::string(join_deny_rules::kMaxPasswordBytes, '1')),
          "a field without an end mark inside its 64 bytes never matches");
    check(!passwordMatches(field(secret).data(), secret.size(), secret), "a field cut before its end mark does not match");

    check(join_deny_rules::finalDetail(join_deny_rules::kReasonWrongVersion) == std::string("version"), "reason 1 ends the join: version");
    check(join_deny_rules::finalDetail(join_deny_rules::kReasonBadPassword) == std::string("password"), "reason 7 ends the join: password");
    check(join_deny_rules::finalDetail(join_deny_rules::kReasonNone) == nullptr && join_deny_rules::finalDetail(3) == nullptr,
          "no refusal and the other reasons leave the retries to the idle count");

    const std::vector<uint8_t> trailer(version::kTrailerBytes, 0xEE);
    const auto message = join_deny_rules::denyMessage(join_deny_rules::kReasonWrongVersion, trailer.data(), trailer.size());
    check(message.length == 3 + 4 + 1 + trailer.size() && message.bytes[0] == join_deny_rules::kDenyMessageId,
          "join_deny: id 4, the 3 byte header, the game version, the reason and the trailer");
    check(message.bytes[1] == message.length - join_deny_rules::kHeaderBytes && message.bytes[2] == 0,
          "the u16 payload length counts everything after the header, the trailer included");
    check(message.bytes[3] == 0xC9 && message.bytes[4] == 0 && message.bytes[5] == 0 && message.bytes[6] == 0 &&
              message.bytes[join_deny_rules::kReasonOffset + join_deny_rules::kHeaderBytes] == join_deny_rules::kReasonWrongVersion,
          "the u32 0xC9 and then the reason, where the game's handler reads it (payload + 4)");
    check(std::equal(trailer.begin(), trailer.end(), message.bytes.begin() + message.length - trailer.size()),
          "the message ends in the trailer, so the guest reads it from the end of the payload");
    const auto password = join_deny_rules::denyMessage(join_deny_rules::kReasonBadPassword, trailer.data(), trailer.size());
    check(password.length == message.length && password.bytes[join_deny_rules::kHeaderBytes + join_deny_rules::kReasonOffset] == join_deny_rules::kReasonBadPassword, "reason 7 is the same message");
    check(join_deny_rules::kMaxDenyBytes >= 3 + 4 + 1 + version::kTrailerBytes, "the buffer holds the largest deny");
    return g_failures == 0 ? 0 : 1;
}
