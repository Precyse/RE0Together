// narrow and lowered: the text helpers behind the adapter name and script file name rules (no game).
#include "../src/ascii_text.h"
#include "check.h"

int main() {
    check(ascii_text::narrow(L"Ethernet 2") == "Ethernet 2", "ASCII text is kept");
    check(ascii_text::narrow(L"Caf\u00E9 \u4E2D") == "Caf? ?", "anything outside ASCII becomes '?'");
    check(ascii_text::narrow(L"") == "" && ascii_text::narrow(nullptr) == "", "empty and null text are empty");
    check(ascii_text::lowered("UG_Lib.LUA") == "ug_lib.lua", "ASCII letters are lowered");
    check(ascii_text::lowered("") == "" && ascii_text::lowered("a-1_B") == "a-1_b", "empty text and non letters are kept");
    return g_failures == 0 ? 0 : 1;
}
