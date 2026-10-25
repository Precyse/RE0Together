// Checks the room name table: known rooms in every stage, ids without a room, unique ids (no game). Exit 0 when every
// check passes.
#include <cstdio>
#include <cstring>
#include <iterator>

#include "../src/room_names.h"

namespace {

int g_failures = 0;

void checkName(uint16_t scene, const char* expected) {
    const char* actual = room_names::nameOf(scene);
    if (actual && std::strcmp(actual, expected) == 0) return;
    std::printf("FAIL: scene 0x%02x: got \"%s\" expected \"%s\"\n", scene, actual ? actual : "(none)", expected);
    ++g_failures;
}

void checkUnnamed(uint16_t scene) {
    if (!room_names::nameOf(scene)) return;
    std::printf("FAIL: scene 0x%02x has a name, expected none\n", scene);
    ++g_failures;
}

void testKnownRooms() {
    checkName(0x04, "Cabin");             // tr04, the train's save room
    checkName(0x08, "Dining Car");        // tr08
    checkName(0x24, "Hall");              // sh00
    checkName(0x32, "Library");           // sh0e, the lift room
    checkName(0x61, "Confinement Room");  // dm0b
    checkName(0x6f, "Ropeway");           // ch0c
    checkName(0x88, "Panel Room");        // bt04
    checkName(0xae, "B9 Passage B");      // pl21, the last scene id
}

void testUnnamed() {
    checkUnnamed(0x10);    // tr10, no confirmed room
    checkUnnamed(0x2e);    // sh0a
    checkUnnamed(0xaf);    // past the last scene id
    checkUnnamed(0xffff);  // no room
}

void testUniqueIds() {
    for (size_t i = 0; i < std::size(room_names::kRooms); ++i) {
        for (size_t j = i + 1; j < std::size(room_names::kRooms); ++j) {
            if (room_names::kRooms[i].scene != room_names::kRooms[j].scene) continue;
            std::printf("FAIL: scene 0x%02x listed twice\n", room_names::kRooms[i].scene);
            ++g_failures;
        }
    }
}

static_assert(room_names::nameOf(0x24) != nullptr, "the lookup is usable at compile time");

}  // namespace

int main() {
    testKnownRooms();
    testUnnamed();
    testUniqueIds();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
