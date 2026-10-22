// The player name rules of milestone 12 (no game): the adapter's boundary check of the launcher's name, UTF-8 to UTF-16 and
// the cut to the game's buffer.
#include <cstdio>
#include <string>

#include "../src/player_name_rules.h"
#include "check.h"

int main() {
    using namespace player_name_rules;

    const auto ascii = parse("Player One");
    check(ascii && *ascii == u"Player One", "ASCII is kept");
    const auto latin = parse("T\xC3\xA9st \xC3\xB1\xC3\xBF");
    check(latin && *latin == u"Tést ñÿ", "Latin-1 letters decode from two UTF-8 bytes to one UTF-16 unit");
    check(!parse("\xE6\x97\xA5\xE6\x9C\xAC"), "CJK is rejected (the launcher sends ? in its place)");
    check(!parse("hi \xF0\x9F\x98\x80"), "an emoji is rejected");
    check(parse("who?") && *parse("who?") == u"who?", "the launcher's ? replacement is an ordinary character");
    check(!parse(""), "an empty name is none");
    check(!parse("tab\there"), "a control character is rejected");
    check(!parse("bell\x07"), "BEL is rejected");
    check(!parse("a\xC2\xA0" "b"), "U+00A0 (no-break space) is outside the set");
    check(!parse("a\xC2\x9F" "b"), "a C1 control is outside the set");
    check(!parse("a\xC0\xA1"), "an overlong form is rejected");
    check(!parse("a\xC3"), "a cut two byte sequence is rejected");
    check(!parse("a\xC3" "b"), "a lead byte without its continuation is rejected");

    check(parse(std::string(kMaxChars, 'x')).has_value(), "15 characters pass");
    check(!parse(std::string(kMaxChars + 1, 'x')), "16 characters are rejected");
    check(parse("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") &&
              !parse("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"),
          "the 15 character limit counts characters, not bytes");

    const std::u16string name = u"Player One";
    check(fit(name, kBufferChars) == name, "a name that fits the buffer is whole");
    check(fit(name, 5) == u"Play", "cap 5 holds 4 characters and the terminator");
    check(fit(name, 0x100) == name, "a cap above the game's 0x40 is limited to the buffer");
    check(fit(std::u16string(kBufferChars + 10, u'x'), 0x100).size() == kBufferChars - 1, "never more than 0x3F characters");
    check(fit(name, 1).empty() && fit(name, 0).empty(), "a cap of 1 or 0 holds no characters");
    return g_failures == 0 ? 0 : 1;
}
