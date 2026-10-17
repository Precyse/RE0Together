// The join_req password field and the refusal reasons that end a join (no game).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/join_deny_rules.h"

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
    return g_failures == 0 ? 0 : 1;
}
