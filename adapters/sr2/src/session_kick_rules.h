#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "partner_rules.h"
#include "pause_mask_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"

// The pure rules of the kick menu, the pause overlay's Disconnect and the host's session end (milestone 16 wave 1, unit
// tested, no game access). Inputs are the session members as remote_players::takeMembers reads them. A "remote" is a member
// that is not the local one and has a slot (not partner_rules::kNoSlot).
namespace session_kick_rules {

constexpr size_t kNone = partner_rules::kNone;
constexpr size_t kMaxPickerNames = sr2::kDialogMaxOptions - 1;  // the fourth option is NO
constexpr size_t kMemberNameChars = 0x40;                       // the name field of a member, with its NUL

using remote_players::MemberInfo;

// A remote member as the picker and the removal keep it: the slot tells whether a reused member object is still the same player.
struct Entry {
    uintptr_t member;
    uint8_t slot;
};

struct Picker {
    Entry entries[kMaxPickerNames];
    size_t count;
    size_t skipped;  // remotes that did not fit
};

// The member the player chose, and the session it belonged to. member 0 = nothing chosen.
struct Pending {
    uintptr_t session;
    Entry entry;
};

struct Targets {
    Entry entries[partner_rules::kMaxWalked];
    size_t count;
    bool pausing;  // the entries are the members that hold the pause, not every remote
};

// The remote members in ascending slot into `out` (room for partner_rules::kMaxWalked), how many.
inline size_t remotesBySlot(const MemberInfo* members, size_t count, Entry* out) {
    size_t found = 0;
    for (size_t i = 0; i < count && found < partner_rules::kMaxWalked; ++i) {
        if (members[i].local || members[i].slot == partner_rules::kNoSlot) continue;
        size_t at = found++;
        while (at > 0 && out[at - 1].slot > members[i].slot) {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = {members[i].address, members[i].slot};
    }
    return found;
}

// The names the picker offers: the remotes in ascending slot, at most kMaxPickerNames.
inline Picker pickerEntries(const MemberInfo* members, size_t count) {
    Entry remotes[partner_rules::kMaxWalked];
    const size_t found = remotesBySlot(members, count, remotes);
    Picker picker{};
    for (; picker.count < found && picker.count < kMaxPickerNames; ++picker.count) picker.entries[picker.count] = remotes[picker.count];
    picker.skipped = found - picker.count;
    return picker;
}

// Only the host with two or more remotes has a choice to make; with one remote the game's own confirm names it.
inline bool showsPicker(bool host, size_t remoteCount) { return host && remoteCount >= 2; }

// The picker entry an answer chooses: only a close (action 2) with a result inside the entries. NO is the result after the last
// entry; a forced close arrives as action 1 and a move between options as action 3.
inline size_t chosenIndex(const Picker& picker, int32_t result, int32_t action) {
    if (action != sr2::kDialogActionClose || result < 0 || static_cast<size_t>(result) >= picker.count) return kNone;
    return static_cast<size_t>(result);
}

// True when a remote member still has this address and this slot.
inline bool stillPresent(const Entry& entry, const MemberInfo* members, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!members[i].local && members[i].address == entry.member && members[i].slot == entry.slot) return true;
    }
    return false;
}

// The chosen member while it is still there in the same session, else 0.
inline uintptr_t resolvePending(const Pending& pending, uintptr_t session, const MemberInfo* members, size_t count) {
    if (!pending.entry.member || pending.session != session || !stillPresent(pending.entry, members, count)) return 0;
    return pending.entry.member;
}

// The picker text of a member name: the literal mark, the name up to its first NUL (at most kMemberNameChars - 1 characters of the
// `nameCap` it holds), the mark, a NUL. Cut short when `outCap` is small; empty when not even the marks fit. Returns the length
// without the NUL.
inline size_t optionText(const wchar_t* name, size_t nameCap, wchar_t* out, size_t outCap) {
    constexpr size_t kMarksAndNul = 3;
    if (outCap < kMarksAndNul) {
        if (outCap > 0) out[0] = 0;
        return 0;
    }
    const size_t limit = nameCap < kMemberNameChars - 1 ? nameCap : kMemberNameChars - 1;
    size_t length = 0;
    while (length < limit && length < outCap - kMarksAndNul && name[length] != 0) ++length;
    out[0] = sr2::kDialogLiteralMark;
    for (size_t i = 0; i < length; ++i) out[1 + i] = name[i];
    out[1 + length] = sr2::kDialogLiteralMark;
    out[2 + length] = 0;
    return length + 2;
}

// The remotes a Disconnect drops: those whose bit is in the pause byte `mask`, else every remote.
inline Targets disconnectTargets(uint8_t mask, const MemberInfo* members, size_t count) {
    Entry remotes[partner_rules::kMaxWalked];
    const size_t found = remotesBySlot(members, count, remotes);
    Targets targets{};
    for (size_t i = 0; i < found; ++i) {
        if ((mask & pause_mask_rules::bitOf(remotes[i].slot)) != 0) targets.entries[targets.count++] = remotes[i];
    }
    targets.pausing = targets.count > 0;
    if (!targets.pausing) {
        for (; targets.count < found; ++targets.count) targets.entries[targets.count] = remotes[targets.count];
    }
    return targets;
}

// The order a session end removes members in: the one the game passed first (a member of the session that is not the local one),
// then the other remotes in ascending slot. Into `out` (room for `cap`), how many.
inline size_t removalOrder(uintptr_t passed, const MemberInfo* members, size_t count, Entry* out, size_t cap) {
    size_t written = 0;
    for (size_t i = 0; i < count && written < cap; ++i) {
        if (!members[i].local && members[i].address == passed) out[written++] = {passed, members[i].slot};
    }
    Entry remotes[partner_rules::kMaxWalked];
    const size_t found = remotesBySlot(members, count, remotes);
    for (size_t i = 0; i < found && written < cap; ++i) {
        if (remotes[i].member != passed) out[written++] = remotes[i];
    }
    return written;
}

// "1 2" for the slots of the entries, for the trace lines.
inline std::string slotsText(const Entry* entries, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) text += ' ';
        text += std::to_string(entries[i].slot);
    }
    return text;
}

}  // namespace session_kick_rules
