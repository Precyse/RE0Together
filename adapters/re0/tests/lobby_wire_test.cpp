// Checks the in-game lobby wire: the launcher's LOBBY_STATE text, OPTION_SET, LOBBY_PRESENCE and SESSION_LOAD (no game).
// Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/lobby_wire.h"

namespace {

using namespace lobby_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// What the launcher's LobbyStateText builds for a two-player RE0 lobby (tools/launcher_tests/LobbyStateTextTests.cs).
constexpr const char kText[] =
    "member\t0\t1\t1\trebecca\tRebecca\tWestern\tPrecyse\n"
    "member\t255\t0\t0\tbilly\tBilly\tSaved\tAmy\n"
    "option\tpartner_mode\tstate\tState\n"
    "option\tworld_pause\tnever\tNever\n"
    "option\tcharacter\trebecca\tRebecca\n"
    "value\tworld_pause\tnever\tNever\n"
    "value\tworld_pause\tmenus\tMenus pause\n"
    "value\tcharacter\trebecca\tRebecca\n"
    "value\tcharacter\tbilly\tBilly\n"
    "outfit\trebecca\t\tSaved\n"
    "outfit\trebecca\t7d\tWestern\n"
    "outfit\tbilly\t\tSaved\n"
    "future\tsomething\n"
    "member\tx\t1\t1\t\t\t\tbad slot\n"
    "own\toutfit_rebecca\t7d\n"
    "own\toutfit_billy\t";

void testLobbyState() {
    const LobbyState state = parseLobbyState(kText);
    check(state.members.size() == 2, "two members; a line with a bad slot is skipped");
    check(state.members[0].host && state.members[0].local && state.members[0].slot == 0 && state.members[0].name == "Precyse" &&
              state.members[0].outfitLabel == "Western",
          "the host's member line");
    check(!state.members[1].host && state.members[1].slot == kNoSlot && state.members[1].character == "billy", "a member without a slot");
    check(state.option("world_pause") && state.option("world_pause")->label == "Never", "a host option's current value and label");
    check(!state.option("load_slot"), "no option that was not sent");
    const std::vector<Value> pauses = state.valuesOf("world_pause");
    check(pauses.size() == 2 && pauses[1].value == "menus" && pauses[1].label == "Menus pause", "an option's values in order");
    const std::vector<Value> outfits = state.outfitsOf("rebecca");
    check(outfits.size() == 2 && outfits[0].value.empty() && outfits[1].label == "Western", "a character's outfits, the saved one first");
    check(state.ownValue("outfit_rebecca") && state.ownValue("outfit_rebecca")->value == "7d", "an own value");
    check(state.ownValue("outfit_billy") && state.ownValue("outfit_billy")->value.empty(), "an empty own value (the saved outfit)");
    check(parseLobbyState("").members.empty(), "empty text, empty lobby");
}

void testOptionSet() {
    check(encodeOptionSet("outfit_billy", "95") == "outfit_billy=95", "key=value");
    check(encodeOptionSet("load_slot", "") == "load_slot=", "an empty value");
    check(!encodeOptionSet("Outfit", "95"), "an upper-case key is refused");
    check(!encodeOptionSet("", "95"), "an empty key is refused");
    check(!encodeOptionSet("load_slot", "1=2"), "'=' in a value is refused");
    check(!encodeOptionSet("load_slot", "a;b"), "';' in a value is refused");
    check(!encodeOptionSet(std::string(33, 'k'), "1"), "a key past 32 characters is refused");
    check(!encodeOptionSet("k", std::string(33, 'v')), "a value past 32 characters is refused");
}

template <class T>
std::vector<uint8_t> bytes(const T& value) {
    std::vector<uint8_t> out(sizeof(value));
    std::memcpy(out.data(), &value, sizeof(value));
    return out;
}

void testPresence() {
    const Presence host{static_cast<uint8_t>(Place::Page), kHasPick, 3, 0, 0x1234abcd};
    const auto decoded = decodePresence(bytes(host));
    check(decoded && decoded->pickSlot == 3 && decoded->pickHash == 0x1234abcd, "a host's presence with its pick");
    const Presence guest{static_cast<uint8_t>(Place::Title), kSaveChecked | kSaveMatches, kNoSlot, 0, 0};
    check(decodePresence(bytes(guest)).has_value(), "a guest's presence");
    check(!decodePresence(bytes(Presence{4, 0, kNoSlot, 0, 0})), "an unknown place is refused");
    check(!decodePresence(bytes(Presence{0, kHasPick, 20, 0, 0})), "a pick outside the player slots is refused");
    check(!decodePresence(bytes(Presence{0, 0, 3, 0, 0})), "a slot without a pick is refused");
    check(!decodePresence(bytes(Presence{0, 8, kNoSlot, 0, 0})), "an unknown flag is refused");
    check(!decodePresence(std::vector<uint8_t>(7)), "a short presence is refused");
}

void testSessionLoad() {
    check(decodeSessionLoad(bytes(SessionLoad{19, 7, 1})).has_value(), "a start of the co-op slot");
    check(!decodeSessionLoad(bytes(SessionLoad{20, 7, 1})), "the system data slot is no start");
    check(!decodeSessionLoad(bytes(SessionLoad{-1, 7, 1})), "a negative slot is refused");
    check(!decodeSessionLoad(bytes(SessionLoad{2, 7, 0})), "serial 0 is refused");
    check(!decodeSessionLoad(std::vector<uint8_t>(16)), "a wrong size is refused");
    check(kMsgSessionLoad < 0x0140 && kMsgLobbyPresence < 0x0140, "outside the state-mode block: both run in input mode too");
}

}  // namespace

int main() {
    testLobbyState();
    testOptionSet();
    testPresence();
    testSessionLoad();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
