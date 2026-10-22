// The pure rules of the waypoint recipient scope of milestone 16 wave 2 (no game): the control text, the name search, the scope and
// the layout of the Lua stack.
#include <cstdio>
#include <string>
#include <string_view>

#include "../src/waypoint_scope_rules.h"
#include "check.h"

namespace {

namespace rules = waypoint_scope_rules;

void testParseControl() {
    const rules::Control close = rules::parseControl("coop_send_to");
    check(close.isControl && !close.open && close.name.empty(), "coop_send_to closes");
    const rules::Control open = rules::parseControl("coop_send_to #PLAYER3#");
    check(open.isControl && open.open && open.name == "#PLAYER3#", "coop_send_to #PLAYER3# opens for #PLAYER3#");
    check(!rules::parseControl("coop_send_tox").isControl, "a longer word is not the control");
    check(!rules::parseControl("coop_send_to ").isControl, "an empty name is not a control");
    check(!rules::parseControl("coop_send_to#PLAYER3#").isControl, "the name needs its space");
    check(!rules::parseControl("stopped failure timer for #PLAYER3#").isControl, "other debug text is no control");
    check(!rules::parseControl("").isControl, "empty text is no control");
    check(!rules::parseControl("coop_send").isControl, "a prefix of the control is no control");
    const std::string longest(rules::kMaxNameBytes, 'a');
    const std::string tooLong(rules::kMaxNameBytes + 1, 'a');
    check(rules::parseControl("coop_send_to " + longest).open, "a name of 32 bytes opens");
    check(!rules::parseControl("coop_send_to " + tooLong).isControl, "a name of 33 bytes is no control");
    check(!rules::parseControl(std::string("coop_send_to #PLA\x01YER#")).isControl, "a name with a byte that is not printable is no control");
    check(!rules::parseControl(std::string("coop_send_to #PLA\x7F") + "YER#").isControl, "DEL is not printable");
    check(rules::kMaxControlBytes == rules::kSendToControl.size() + 1 + rules::kMaxNameBytes, "the longest control is the word, a space and 32 bytes");
}

void testFindName() {
    const std::string_view names[] = {"#PLAYER2#", "#PLAYER3#", "#PLAYER4#"};
    check(rules::findName(names, 3, "#PLAYER3#") == 1, "an exact name is found");
    check(rules::findName(names, 3, "#PLAYER5#") == 3, "a name that is not there gives the count");
    check(rules::findName(names, 3, "#player3#") == 3, "the search is case sensitive");
    check(rules::findName(names, 3, "#PLAYER") == 3, "a prefix is not a match");
    check(rules::findName(names, 0, "#PLAYER2#") == 0, "no names: the count, 0");
}

void testScope() {
    rules::Scope scope;
    check(!scope.active() && scope.name().empty(), "a new scope is closed");
    check(!scope.open("#PLAYER3#") && scope.active() && scope.name() == "#PLAYER3#", "opening a closed scope replaces nothing");
    check(scope.open("#PLAYER4#") && scope.name() == "#PLAYER4#", "opening an open scope replaces it");
    scope.close();
    check(!scope.active() && scope.name().empty(), "close empties the scope");
    const std::string long40(40, 'b');
    scope.open(long40);
    check(scope.name().size() == rules::kMaxNameBytes, "a name is cut at 32 bytes");
    check(scope.open("x") && scope.name() == "x", "a shorter name over a long one leaves nothing of the long one");
}

void testLuaStack() {
    check(rules::isLuaString(sr2::kLuaTypeString) && !rules::isLuaString(3), "type 4 is a string");
    check(rules::hasArgument(0x1000, 0x1010) && rules::hasArgument(0x1000, 0x1020), "an object between base and top is an argument");
    check(!rules::hasArgument(0x1000, 0x1000) && !rules::hasArgument(0x1000, 0x100F), "an empty stack has no argument");
}

}  // namespace

int main() {
    testParseControl();
    testFindName();
    testScope();
    testLuaStack();
    return g_failures == 0 ? 0 : 1;
}
