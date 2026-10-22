// The member leave path (no game): the destroy message, who gets a reissue, who renames and drops, the creation queue rules,
// and the three sites as data.
#include <algorithm>
#include <cstdio>

#include "../src/creation_queue_rules.h"
#include "../src/leave_rules.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "check.h"
#include "juiced_targets.h"
#include "site_lookup.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kDropThunk = 0x1000A000;
constexpr uintptr_t kRenameThunk = 0x1000A100;
constexpr uintptr_t kNearbyMargin = 0x900;  // no milestone 4 site closer to the leave function
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uint8_t kHost = 0;
constexpr uint8_t kGuestOne = 1;
constexpr uint8_t kGuestTwo = 2;

std::vector<uint8_t> jumpTo(uintptr_t address, uintptr_t thunk, size_t size) {
    const uint32_t to = static_cast<uint32_t>(thunk - (address + 5));
    std::vector<uint8_t> bytes = {0xE9, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8), static_cast<uint8_t>(to >> 16),
                                  static_cast<uint8_t>(to >> 24)};
    bytes.resize(size, 0x90);
    return bytes;
}

}  // namespace

int main() {
    using namespace leave_rules;

    const Message narrow = destroyMessage(0xAABBCCDD, 5, 0x1234, false);
    check(narrow.length == 10 && narrow.bytes[0] == 0x0F && narrow.bytes[3] == 0xDD && narrow.bytes[6] == 0xAA &&
              narrow.bytes[7] == 5 && narrow.bytes[8] == 0x34 && narrow.bytes[9] == 0x12,
          "a destroy message is rop_destroy 0x0F, not rop_new_owner 0x2A: the session value, the class, the id");
    const Message sealedNarrow = sealed(narrow);
    check(sealedNarrow.bytes[1] == 7 && sealedNarrow.bytes[2] == 0, "its payload length counts the bytes after the 3 byte header");
    const Message wide = destroyMessage(0xAABBCCDD, 5, 0x1234, true);
    check(wide.length == 11 && wide.bytes[3] == 0 && wide.bytes[4] == 0xDD && wide.bytes[10] == 0x12 && sealed(wide).bytes[1] == 8,
          "with the 4 byte header everything is one byte later and the length is still counted from byte 3");
    Message withPayload = narrow;
    withPayload.length += 40;
    check(sealed(withPayload).bytes[1] == 47, "the class payload the game appends is part of the length");

    check(guestLeave(0, false, false) == GuestLeave::CutsceneCleanup && guestLeave(1, true, false) == GuestLeave::CutsceneCleanup &&
              guestLeave(6, false, true) == GuestLeave::CutsceneCleanup,
          "an idle session, the host or this machine leaving: the shipped cutscene cleanup");
    check(guestLeave(1, false, false) == GuestLeave::ObjectPass && guestLeave(6, false, false) == GuestLeave::ObjectPass &&
              guestLeave(8, false, false) == GuestLeave::ObjectPass,
          "another guest leaving in the states 1, 6 and 8: the host's pass over the objects, and no cutscene end");
    check(guestLeave(3, false, false) == GuestLeave::Nothing, "another guest leaving in any other state: nothing");

    check(usableMember(false, 1, 8, kGuestOne, kGuestTwo), "a remote member that can receive and is connected gets the reissue");
    check(!usableMember(true, 1, 13, kHost, kGuestTwo) && !usableMember(false, 0, 13, kGuestOne, kGuestTwo) &&
              !usableMember(false, 1, 7, kGuestOne, kGuestTwo) && !usableMember(false, 1, 13, kGuestTwo, kGuestTwo),
          "not the local member, one that cannot receive, one still connecting, or the leaver");

    check(keepsRename(kGuestOne, kGuestOne, kGuestTwo, false) && keepsRename(kHost, kHost, kGuestTwo, true),
          "a machine renames the records it owns");
    check(keepsRename(kGuestTwo, kHost, kGuestTwo, true), "the host also renames the leaver's records nobody took over");
    check(!keepsRename(kGuestTwo, kGuestOne, kGuestTwo, false) && !keepsRename(kGuestOne, kHost, kGuestTwo, true),
          "a guest does not rename the leaver's records, nor anyone does another owner's");

    check(guestDrops(false, kGuestTwo, kHost), "a guest drops the records of a guest that left");
    check(!guestDrops(true, kGuestTwo, kHost) && !guestDrops(false, kHost, kHost),
          "the host keeps the game's handling, and a guest whose host left too (the session is lost)");

    ReissueSet set;
    set.add(0x1000, 0x2001);
    set.add(0x2000, 0x2002);
    set.add(0x1000, 0x2003);
    check(set.items().size() == 2 && set.items()[0].oldId == 0x2001, "a record is reissued once, with the first id noted");

    using namespace creation_queue_rules;
    check(kCapacity == 0x100 && hasRoom(0xFF) && !hasRoom(0x100), "a member's queue holds 0x100 handles");
    check(maskAfterQueue(0xFFFF, 2) == 0xFFFB && maskAfterQueue(0x0004, 2) == 0 && maskAfterQueue(0x0001, 2) == 0x0001,
          "queuing a creation clears the member's bit of the created mask only");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kThunk, kThunk, kDropThunk, kRenameThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const CodePatch* drop = patchAt(patches, 0x807413);
    check(drop && drop->expected == std::vector<uint8_t>{0x38, 0x0E, 0x0F, 0x85, 0x91, 0x00, 0x00, 0x00} &&
              drop->replacement == jumpTo(0x807413, kDropThunk, 8),
          "LV1: 38 0E 0F 85 91 00 00 00 at 0x807413 is checked and becomes jmp rel32 and three nops");
    const CodePatch* rename = patchAt(patches, 0x8074E5);
    check(rename &&
              rename->expected == std::vector<uint8_t>{0x66, 0x8B, 0x4E, 0x02, 0x66, 0xC1, 0xE9, 0x0C, 0x66, 0x3B, 0xCB, 0x75, 0x31} &&
              rename->replacement == jumpTo(0x8074E5, kRenameThunk, 13),
          "LV2: the 13 bytes at 0x8074E5 are checked and become jmp rel32 and eight nops");
    const CodePatch* entry = patchAt(patches, 0x8072E0);
    check(entry && entry->expected == std::vector<uint8_t>{0x83, 0xEC, 0x10, 0xA1, 0x14, 0x8C, 0x52, 0x02} &&
              entry->replacement.empty(),
          "LV3: the entry of the leave function is checked, the hook writes it");
    check(player_cap_patches::forPlayers(2, thunks).empty(), "max_players 2 patches nothing, the game leaves as shipped");

    bool clear = true;
    for (const partner_sites::Site& site : partner_sites::kSites) {
        for (const uintptr_t address : {0x8072E0u, 0x807413u, 0x8074E5u}) {
            const uintptr_t distance = site.address > address ? site.address - address : address - site.address;
            clear = clear && distance >= kNearbyMargin;
        }
    }
    check(clear, "no milestone 4 site within 0x900 bytes of the leave function's edits");
    bool clearOfJuiced = true;
    for (const CodePatch* patch : {drop, rename, entry}) {
        const uintptr_t low = patch->address - kJuicedMargin;
        const uintptr_t high = patch->address + patch->expected.size() + kJuicedMargin;
        clearOfJuiced = clearOfJuiced && std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                                                      [&](uint32_t target) { return target >= low && target < high; });
    }
    check(clearOfJuiced, "no edit within 16 bytes of an address Juiced touches");
    return g_failures == 0 ? 0 : 1;
}
