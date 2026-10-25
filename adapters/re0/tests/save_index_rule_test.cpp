// Checks the save slot summary and label against the launcher's SaveIndex (tools/launcher_tests/SaveIndexTests.cs uses
// the same fields), and the staged-copy pick of save_redirect (no game). Exit 0 when every check passes.
#include <array>
#include <cstdio>
#include <cstring>

#include "../src/save_index_rule.h"
#include "../src/save_redirect_rule.h"

namespace {

using namespace save_index_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

std::array<uint8_t, kSummaryBytes> head(uint8_t area, uint8_t count, uint8_t wesker, uint32_t difficulty, float frames) {
    std::array<uint8_t, kSummaryBytes> bytes{};
    bytes[kAreaField] = area;
    bytes[kAreaField + 1] = count;
    bytes[kWeskerField] = wesker;
    std::memcpy(bytes.data() + kDifficultyField, &difficulty, sizeof(difficulty));
    std::memcpy(bytes.data() + kPlayFramesField, &frames, sizeof(frames));
    return bytes;
}

void testSummary() {
    // The user's slot 7: five saves, Hard, 137649 frames (38 min 14 s), area 4.
    const Summary summary = summarize(head(4, 5, 0, 2, 137649.0f));
    check(summary.used && !summary.cleared && !summary.wesker, "a used slot");
    check(label(7, summary) == "Slot 8 / Hard / 0:38:14 / Area 4", "the launcher's label");
    check(startMode(summary) == kStartContinue, "Continue starts it in mode 1");
    const Summary cleared = summarize(head(kClearedArea, 1, 1, 1, 60.0f * 3725));
    check(label(0, cleared) == "Slot 1 / Normal / 1:02:05 / Cleared / Wesker", "a cleared Wesker save");
    check(startMode(cleared) == kStartCleared, "a cleared save starts in mode 2");
    check(!summarize(head(0, 0, 0, 0, 0.0f)).used, "bytes 4..8 all zero: an empty slot");
    check(!summarize(std::array<uint8_t, 8>{1, 1, 1, 1, 1, 1, 1, 1}).used, "too few bytes: unused");
    check(label(2, summarize(head(4, 1, 0, 9, -5.0f))) == "Slot 3 / 0:00:00 / Area 4", "an unknown difficulty and a negative time");
}

void testStagedPick() {
    using save_redirect_rule::Source;
    check(save_redirect_rule::source(false, false) == Source::Steam, "no session file: Steam");
    check(save_redirect_rule::source(true, false) == Source::Session, "the session copy from before the launch");
    // The staging gap this fixes: a host save sent while the game ran must be what the game reads next.
    check(save_redirect_rule::source(true, true) == Source::Staged, "a staged copy is newer");
    check(save_redirect_rule::source(false, true) == Source::Steam, "a staged file alone does not make a session");
}

}  // namespace

int main() {
    testSummary();
    testStagedPick();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
