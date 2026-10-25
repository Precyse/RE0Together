// Checks the partner tag content and the condition thresholds (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/partner_status.h"

namespace {

using partner_status::Condition;
using partner_status::Input;
using partner_status::condition;
using partner_status::tag;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void checkDetail(const Input& input, const char* expected, const char* what) {
    const std::string actual = tag(input).detail;
    if (actual == expected) return;
    std::printf("FAIL: %s: got \"%s\" expected \"%s\"\n", what, actual.c_str(), expected);
    ++g_failures;
}

void testCondition() {
    check(condition(150, 150) == Condition::Fine, "full health is fine");
    check(condition(77, 150) == Condition::Fine, "just over half is fine");
    check(condition(75, 150) == Condition::Caution, "half is caution");
    check(condition(39, 150) == Condition::Caution, "just over a quarter is caution");
    check(condition(37, 150) == Condition::Danger, "under a quarter is danger");
    check(condition(0, 150) == Condition::Danger, "no health is danger");
    check(condition(10, 0) == Condition::Danger, "no known maximum is danger");
}

void testTag() {
    Input input;
    input.name = "Ahmad";
    check(tag(input).name == "Ahmad" && !tag(input).hasCondition, "the name shows before the first state");
    input.hasState = true;
    input.hp = 150;
    input.sameRoom = true;
    check(tag(input).hasCondition && tag(input).condition == Condition::Fine, "together and healthy");
    checkDetail(input, "", "together has no detail line");
    input.sameRoom = false;
    input.room = 0x24;
    input.hp = 30;
    check(tag(input).condition == Condition::Danger, "low health is danger");
    checkDetail(input, "Room 0x24", "apart names the room");
    input.inMenu = true;
    checkDetail(input, "Room 0x24 / In menu", "apart in a menu");
    input.room = partner_status::kNoRoom;
    checkDetail(input, "In menu", "an unknown room is left out");
    input.inMenu = false;
    checkDetail(input, "", "an unknown room alone says nothing");
    input.name = "AVeryLongSteamNameIndeed";
    check(tag(input).name == "AVeryLongSteamNa", "long names are cut");
    check(tag(Input{}).name.empty(), "no partner, no tag");
}

}  // namespace

int main() {
    testCondition();
    testTag();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
