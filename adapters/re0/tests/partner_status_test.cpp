// Checks the partner status line text and the condition thresholds (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/partner_status.h"

namespace {

using partner_status::Condition;
using partner_status::Input;
using partner_status::condition;
using partner_status::text;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void checkText(const Input& input, const char* expected, const char* what) {
    const std::string actual = text(input);
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

void testLine() {
    Input input;
    input.name = "Ahmad";
    checkText(input, "Ahmad", "name before the first state");
    input.hasState = true;
    input.hp = 150;
    input.sameRoom = true;
    checkText(input, "Ahmad / Fine / same room", "together");
    input.sameRoom = false;
    input.room = 0x24;
    input.hp = 30;
    checkText(input, "Ahmad / Danger / room 0x24", "apart");
    input.inMenu = true;
    checkText(input, "Ahmad / Danger / room 0x24 / in menu", "apart in a menu");
    input.room = partner_status::kNoRoom;
    checkText(input, "Ahmad / Danger / in menu", "unknown room is left out");
    input.name = "AVeryLongSteamNameIndeed";
    checkText(input, "AVeryLongSteamNa / Danger / in menu", "long names are cut");
    input.hostLeft = true;
    checkText(input, "Host left / not saved", "host left replaces the line");
}

}  // namespace

int main() {
    testCondition();
    testLine();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
