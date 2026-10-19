// The kick and session admin rules of milestone 16 wave 1 (no game): the picker entries, the choice, the pending target, the
// option text, the Disconnect targets and the removal order.
#include <cstdio>
#include <string>

#include "../src/session_kick_rules.h"

namespace {

using namespace session_kick_rules;

constexpr uint8_t kNoSlot = partner_rules::kNoSlot;
constexpr uintptr_t kSession = 0x30000000;
constexpr uintptr_t kOtherSession = 0x30001000;
constexpr uintptr_t kLocalMember = 0x1000;
constexpr uintptr_t kMemberA = 0x2000;
constexpr uintptr_t kMemberB = 0x3000;
constexpr uintptr_t kMemberC = 0x4000;
constexpr uintptr_t kMemberD = 0x5000;
constexpr int32_t kClose = sr2::kDialogActionClose;
constexpr int32_t kForcedClose = 1;
constexpr int32_t kNavigate = 3;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

MemberInfo remote(uintptr_t address, uint8_t slot) { return {address, slot, 0, false}; }
MemberInfo local(uint8_t slot) { return {kLocalMember, slot, 0, true}; }

std::string slots(const Picker& picker) { return slotsText(picker.entries, picker.count); }

void pickerEntriesTests() {
    const MemberInfo withLocal[] = {local(0), remote(kMemberA, 1), remote(kMemberB, 2)};
    const Picker normal = pickerEntries(withLocal, 3);
    check(normal.count == 2 && slots(normal) == "1 2" && normal.skipped == 0 && normal.entries[0].member == kMemberA,
          "pickerEntries: the local member is excluded");

    const MemberInfo withNoSlot[] = {local(0), remote(kMemberA, kNoSlot), remote(kMemberB, 2)};
    const Picker noSlot = pickerEntries(withNoSlot, 3);
    check(noSlot.count == 1 && noSlot.entries[0].slot == 2, "pickerEntries: a member with slot 0xFF is excluded");

    const MemberInfo unsorted[] = {remote(kMemberC, 3), local(0), remote(kMemberA, 1), remote(kMemberB, 2)};
    const Picker sorted = pickerEntries(unsorted, 4);
    check(slots(sorted) == "1 2 3" && sorted.entries[0].member == kMemberA && sorted.entries[2].member == kMemberC,
          "pickerEntries: unsorted input comes out in ascending slot, with its members");

    const MemberInfo four[] = {remote(kMemberD, 4), remote(kMemberC, 3), remote(kMemberA, 1), remote(kMemberB, 2), local(0)};
    const Picker capped = pickerEntries(four, 5);
    check(capped.count == kMaxPickerNames && capped.skipped == 1 && slots(capped) == "1 2 3",
          "pickerEntries: 4 remotes give 3 entries and skipped 1, the lowest slots");

    const MemberInfo none[] = {local(0)};
    const Picker empty = pickerEntries(none, 1);
    check(empty.count == 0 && empty.skipped == 0, "pickerEntries: no remote gives no entries");
    check(pickerEntries(nullptr, 0).count == 0, "pickerEntries: no members at all gives no entries");
}

void showsPickerTests() {
    check(!showsPicker(true, 1), "showsPicker: the host with one remote runs the game's confirm");
    check(showsPicker(true, 2), "showsPicker: the host with two remotes picks");
    check(showsPicker(true, 3), "showsPicker: the host with three remotes picks");
    check(!showsPicker(false, 3), "showsPicker: a guest never picks");
    check(!showsPicker(true, 0), "showsPicker: no remote, no picker");
}

void chosenIndexTests() {
    const MemberInfo members[] = {remote(kMemberA, 1), remote(kMemberB, 2)};
    const Picker picker = pickerEntries(members, 2);
    check(chosenIndex(picker, 0, kClose) == 0 && chosenIndex(picker, 1, kClose) == 1, "chosenIndex: a close with a result in range chooses it");
    check(chosenIndex(picker, 2, kClose) == kNone, "chosenIndex: NO (the result after the last entry) chooses nobody");
    check(chosenIndex(picker, -1, kForcedClose) == kNone, "chosenIndex: a forced close (action 1, result -1) chooses nobody");
    check(chosenIndex(picker, 0, kForcedClose) == kNone, "chosenIndex: action 1 chooses nobody whatever the result");
    check(chosenIndex(picker, 0, kNavigate) == kNone, "chosenIndex: a move between options (action 3) chooses nobody");
    check(chosenIndex(picker, -1, kClose) == kNone && chosenIndex(picker, -2, kClose) == kNone, "chosenIndex: a negative result chooses nobody");
}

void presenceTests() {
    const MemberInfo members[] = {local(0), remote(kMemberA, 1), remote(kMemberB, 2)};
    check(stillPresent({kMemberA, 1}, members, 3), "stillPresent: the same address and slot is present");
    check(!stillPresent({kMemberA, 2}, members, 3), "stillPresent: the same address in another slot (a reused member object) is not");
    check(!stillPresent({kMemberC, 3}, members, 3), "stillPresent: a member that left is not");
    check(!stillPresent({kLocalMember, 0}, members, 3), "stillPresent: the local member never is");

    const Pending pending{kSession, {kMemberB, 2}};
    check(resolvePending(pending, kSession, members, 3) == kMemberB, "resolvePending: a present member of the same session resolves");
    check(resolvePending(Pending{}, kSession, members, 3) == 0, "resolvePending: nothing pending gives 0");
    check(resolvePending(pending, kOtherSession, members, 3) == 0, "resolvePending: another session gives 0");
    check(resolvePending({kSession, {kMemberC, 3}}, kSession, members, 3) == 0, "resolvePending: a member that left gives 0");
    check(resolvePending({kSession, {kMemberB, 1}}, kSession, members, 3) == 0, "resolvePending: a reused member object gives 0");
}

std::wstring text(const wchar_t* name, size_t nameCap, size_t outCap, size_t* length = nullptr) {
    wchar_t out[0x100] = {};
    const size_t written = optionText(name, nameCap, out, outCap);
    if (length) *length = written;
    return out;
}

void optionTextTests() {
    size_t length = 0;
    const std::wstring normal = text(L"Mia", 0x40, 0x80, &length);
    check(normal == L"\x01Mia\x01" && length == 5, "optionText: the name between two literal marks");

    wchar_t full[0x40];
    for (wchar_t& c : full) c = L'x';
    const std::wstring cut = text(full, 0x40, 0x80, &length);
    check(length == kMemberNameChars - 1 + 2 && cut.size() == length && cut.front() == L'\x01' && cut.back() == L'\x01',
          "optionText: a name without a NUL is cut at 0x3F characters");

    const std::wstring empty = text(L"", 0x40, 0x80, &length);
    check(empty == L"\x01\x01" && length == 2, "optionText: an empty name is two marks");

    const std::wstring small = text(L"Mia", 0x40, 5, &length);
    check(small == L"\x01Mi\x01" && length == 4, "optionText: a small output cuts the name and keeps the closing mark and the NUL");
    wchar_t exact[3] = {L'?', L'?', L'?'};
    check(optionText(L"Mia", 0x40, exact, 3) == 2 && exact[0] == L'\x01' && exact[1] == L'\x01' && exact[2] == 0,
          "optionText: room for the marks and the NUL only gives the marks");
    wchar_t tiny[2] = {L'?', L'?'};
    check(optionText(L"Mia", 0x40, tiny, 2) == 0 && tiny[0] == 0 && tiny[1] == L'?', "optionText: no room for the marks gives an empty string");
    check(optionText(L"Mia", 0x40, nullptr, 0) == 0, "optionText: no output at all writes nothing");

    const std::wstring capped = text(L"Mia", 2, 0x80, &length);
    check(capped == L"\x01Mi\x01" && length == 4, "optionText: a name cap shorter than the name cuts it");
}

void disconnectTargetsTests() {
    const MemberInfo two[] = {local(0), remote(kMemberA, 1), remote(kMemberB, 2)};
    const Targets pausingThird = disconnectTargets(0x04, two, 3);
    check(pausingThird.pausing && pausingThird.count == 1 && pausingThird.entries[0].member == kMemberB, "disconnectTargets: mask 0x04 drops slot 2 only");

    const Targets both = disconnectTargets(0x06, two, 3);
    check(both.pausing && both.count == 2 && both.entries[0].slot == 1 && both.entries[1].slot == 2, "disconnectTargets: mask 0x06 drops slots 1 and 2 in order");

    const Targets hostBit = disconnectTargets(0x01, two, 3);
    check(!hostBit.pausing && hostBit.count == 2 && hostBit.entries[0].slot == 1,
          "disconnectTargets: the shipped byte 1 is the host's own bit, so every remote goes and nobody is pausing");

    const Targets nothingPaused = disconnectTargets(0x00, two, 3);
    check(!nothingPaused.pausing && nothingPaused.count == 2, "disconnectTargets: no pause bit drops every remote");

    const MemberInfo alone[] = {local(0)};
    const Targets noRemote = disconnectTargets(0x04, alone, 1);
    check(noRemote.count == 0 && !noRemote.pausing, "disconnectTargets: no remote member gives no target");

    const MemberInfo one[] = {local(0), remote(kMemberA, 1)};
    const Targets single = disconnectTargets(0x00, one, 2);
    check(single.count == 1 && single.entries[0].member == kMemberA, "disconnectTargets: one remote with mask 0 is that remote");

    const MemberInfo unsorted[] = {remote(kMemberB, 2), local(0), remote(kMemberA, 1)};
    check(disconnectTargets(0x06, unsorted, 3).entries[0].slot == 1, "disconnectTargets: unsorted members come out in ascending slot");
}

void removalOrderTests() {
    const MemberInfo members[] = {local(0), remote(kMemberA, 1), remote(kMemberB, 2), remote(kMemberC, 3)};
    Entry out[partner_rules::kMaxWalked];

    size_t count = removalOrder(kMemberB, members, 4, out, partner_rules::kMaxWalked);
    check(count == 3 && out[0].member == kMemberB && out[1].member == kMemberA && out[2].member == kMemberC,
          "removalOrder: the passed member in the middle slot goes first, then the others ascending");

    count = removalOrder(0, members, 4, out, partner_rules::kMaxWalked);
    check(count == 3 && out[0].slot == 1 && out[1].slot == 2 && out[2].slot == 3, "removalOrder: a passed 0 gives the remotes ascending");

    count = removalOrder(kLocalMember, members, 4, out, partner_rules::kMaxWalked);
    check(count == 3 && out[0].member == kMemberA, "removalOrder: the local member is never removed, the remotes follow ascending");

    const MemberInfo single[] = {local(0), remote(kMemberA, 1)};
    count = removalOrder(kMemberA, single, 2, out, partner_rules::kMaxWalked);
    check(count == 1 && out[0].member == kMemberA && out[0].slot == 1, "removalOrder: one remote is the single call the game made");

    const MemberInfo unassigned[] = {local(0), remote(kMemberA, kNoSlot)};
    count = removalOrder(kMemberA, unassigned, 2, out, partner_rules::kMaxWalked);
    check(count == 1 && out[0].member == kMemberA, "removalOrder: the passed member goes even without a slot, as the game's single call does");

    count = removalOrder(kMemberB, members, 4, out, 2);
    check(count == 2 && out[0].member == kMemberB && out[1].member == kMemberA, "removalOrder: never writes past the capacity");

    count = removalOrder(kMemberD, members, 4, out, partner_rules::kMaxWalked);
    check(count == 3 && out[0].member == kMemberA, "removalOrder: a passed member that is not in the session is not removed");
}

}  // namespace

int main() {
    pickerEntriesTests();
    showsPickerTests();
    chosenIndexTests();
    presenceTests();
    optionTextTests();
    disconnectTargetsTests();
    removalOrderTests();
    return g_failures == 0 ? 0 : 1;
}
