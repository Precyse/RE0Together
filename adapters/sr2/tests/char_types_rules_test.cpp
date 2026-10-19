// The pure rules of one character type per remote (M16 W1, no game): the type number of each pool index, the extra slots, the slot sizes,
// the type names, the stream categories, the pool index of a human, the registry lookup and the trace text.
#include <array>
#include <cstdio>
#include <set>

#include "../src/char_types_rules.h"

namespace {

using namespace char_types_rules;

constexpr uint32_t kPoolIndices = 12;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void theTypeNumbers() {
    check(remoteTypeNumber(2, 3, 4) == 2 && remoteTypeNumber(1, 3, 4) == 3 && remoteTypeNumber(0, 3, 4) == 4,
          "4 players, local index 3: index 2 keeps mp2, index 1 gets mp3, index 0 gets mp4");
    check(remoteTypeNumber(3, 3, 4) == kNotWritten, "the local index is not written");
    bool shipped = true;
    for (uint32_t index = 4; index < kPoolIndices; ++index) shipped = shipped && remoteTypeNumber(index, 3, 4) == 2;
    check(shipped, "4 players: the indices 4 to 11 are never allocated and stay mp2");
    check(remoteTypeNumber(1, 2, 3) == 2 && remoteTypeNumber(0, 2, 3) == 3 && remoteTypeNumber(2, 2, 3) == kNotWritten,
          "3 players, local index 2: index 1 gets mp2, index 0 gets mp3");
    check(remoteTypeNumber(3, 1, 4) == 2 && remoteTypeNumber(2, 1, 4) == 3 && remoteTypeNumber(0, 1, 4) == 4,
          "4 players, local index 1: the local index is skipped in the count (3 -> 2, 2 -> 3, 0 -> 4)");
    check(remoteTypeNumber(0, 1, 2) == 2, "2 players, local index 1: the shipped mp2");
    check(remoteTypeNumber(6, 7, 8) == 2 && remoteTypeNumber(3, 7, 8) == 5 && remoteTypeNumber(0, 7, 8) == 8,
          "8 players, local index 7: index 6 down to 0 gets mp2 up to mp8");
    check(remoteTypeNumber(3, kNoLocal, 4) == 2 && remoteTypeNumber(2, kNoLocal, 4) == 3 && remoteTypeNumber(1, kNoLocal, 4) == 4 &&
              remoteTypeNumber(0, kNoLocal, 4) == 2,
          "no local player: 3 -> 2, 2 -> 3, 1 -> 4, and index 0 would be 5, above 4, so it keeps mp2");
    check(remoteTypeNumber(0, kNoLocal, 20) == 2, "a number above the last type keeps mp2 too");
}

void theTypesAreEachUsedOnce() {
    bool eachOnce = true;
    for (uint32_t maxPlayers = 2; maxPlayers <= kLastTypeNumber && eachOnce; ++maxPlayers) {
        for (uint32_t local = 0; local < maxPlayers && eachOnce; ++local) {
            std::set<uint32_t> numbers;
            for (uint32_t index = 0; index < maxPlayers; ++index) {
                if (index != local) numbers.insert(remoteTypeNumber(index, local, maxPlayers));
            }
            eachOnce = numbers.size() == maxPlayers - 1 && *numbers.begin() == 2 && *numbers.rbegin() == maxPlayers;
        }
    }
    check(eachOnce, "for every player count and local index the remotes get exactly mp2 to mp<max_players>, each once");
}

void theSlotsAndSizes() {
    check(extraCharacterSlots(8) == 6, "8 players: 6 extra slots, the shipped MP8 count");
    check(extraCharacterSlots(2) == 0 && extraCharacterSlots(1) == 0 && extraCharacterSlots(0) == 0, "2 players or fewer: no extra slots");
    check(extraCharacterSlots(3) == 1 && extraCharacterSlots(4) == 2, "3 and 4 players: 1 and 2 extra slots");
    check(extraCharacterSlots(12) == 10 && extraCharacterSlots(40) == 10, "the 10 slots of category 3 are the most");
    check(slotBytes(0x1000) == 0x2000 && slotBytes(0x1234) == 0x2300 && slotBytes(0xF01) == 0x2000 && slotBytes(0xF00) == 0x1F00,
          "a slot is the node size plus 0x1000, rounded up to 0x100");
    check(slotBytes(0x100) == 0x1100, "a size already on the step only gets the 0x1000");
}

void theNames() {
    check(typeName(2) == "styletest_pc_mp2" && typeName(3) == "styletest_pc_mp3" && typeName(12) == "styletest_pc_mp12",
          "the names styletest_pc_mp2, mp3 and mp12");
    check(typeName(1).empty() && typeName(0).empty() && typeName(13).empty(), "out of range 2..12 gives nothing");
}

void theCategories() {
    check(categoryOf(0x03000012) == 3 && categoryOf(0x02FFFFFF) == 2 && categoryOf(0) == 0, "the category is the top byte of the id");
    check(kPlayerCategory == 2 && kExtraPlayerCategory == 3, "co-op's characters are in category 2, the extra ones in 3");
    check(categoryBase(2) == 0x2F9B490 && categoryBase(3) == 0x2F9B564, "category 3 starts at 0x2F9B564, 0xD4 after category 2");
}

void thePoolIndex() {
    constexpr uintptr_t kPool = 0x2CD0590;
    constexpr uintptr_t kStride = 0x18F0;
    check(poolIndexOf(kPool) == 0 && poolIndexOf(kPool + 3 * kStride) == 3 && poolIndexOf(kPool + 11 * kStride) == 11,
          "the humans of the pool are indexed 0 to 11");
    check(poolIndexOf(0) == kNoLocal && poolIndexOf(kPool - 1) == kNoLocal && poolIndexOf(kPool + 12 * kStride) == kNoLocal,
          "no human, or a pointer outside the 12 slots, is no local index");
}

void theRegistry() {
    const uint32_t ids[] = {0x02000001, kNoRecordId, 0x03000002, 0x03000002, 0x03000003};
    check(recordFor(0x03000002, ids, 5) == 2, "the first slot with the id");
    check(recordFor(0x03000003, ids, 5) == 4 && recordFor(0x02000001, ids, 5) == 0, "the last and the first slot");
    check(recordFor(0x03000009, ids, 5) == kNoRecord, "an id that is not there is -1");
    check(recordFor(kNoRecordId, ids, 5) == kNoRecord, "an empty slot is never a match");
    check(recordFor(0x03000002, ids, 0) == kNoRecord, "an empty registry");
}

void theText() {
    check(assignmentText(1, 3, 0x1234, 0x5678) == "index 1 styletest_pc_mp3 type 0x1234 record 0x5678", "the char table trace entry");
    check(streamedText(4, 0x03000012, 1, 0x5678) == "styletest_pc_mp4 id 0x03000012 loaded 1 record 0x5678", "the level load trace entry");
}

}  // namespace

int main() {
    theTypeNumbers();
    theTypesAreEachUsedOnce();
    theSlotsAndSizes();
    theNames();
    theCategories();
    thePoolIndex();
    theRegistry();
    theText();
    return g_failures == 0 ? 0 : 1;
}
