#include "session_kick.h"

#include <mutex>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "session_kick_rules.h"
#include "session_kick_sites.h"
#include "site_installer.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;
using session_kick_rules::Entry;

constexpr size_t kOptionChars = sr2::kDialogOptionBytes / sizeof(wchar_t);
constexpr uint32_t kNoConfirmation = 0;  // a forced close arrives as action 1, which the picker ignores
constexpr uint32_t kNoLastFlag = 0;
const wchar_t kEmptyBody[] = L"";

using KickFunction = int(__cdecl*)(void* lua);

bool g_trace = false;
void* g_kickOriginal = nullptr;  // the trampoline of coop_kick_player: the game's confirm

// The picker that is open and the member it chose, one lock for both: the dialog callback runs on the game's thread frames later.
struct KickState {
    session_kick_rules::Picker picker;
    uintptr_t pickerSession;
    session_kick_rules::Pending pending;
};

std::mutex g_lock;
KickState g_state{};

uintptr_t currentSession() { return readDword(sr2::kSessionPointer); }

uint8_t slotOf(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : partner_rules::kNoSlot; }

bool isHost(uintptr_t session) { return session && readByte(session + sr2::kSessionHostFlagOffset) != 0; }

void clearPending() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_state.pending = {};
}

// The original coop_kick_player: the confirm for whoever the getters name now.
void openGameConfirm(void* lua) { reinterpret_cast<KickFunction>(g_kickOriginal)(lua); }

// The picker text of each entry, then the game's NO.
size_t fillOptions(const session_kick_rules::Picker& picker, wchar_t (&texts)[sr2::kDialogMaxOptions][kOptionChars],
                   const wchar_t* (&options)[sr2::kDialogMaxOptions]) {
    for (size_t i = 0; i < picker.count; ++i) {
        wchar_t name[session_kick_rules::kMemberNameChars] = {};
        readMemory(picker.entries[i].member + sr2::kMemberNameOffset, name, sizeof(name));
        session_kick_rules::optionText(name, session_kick_rules::kMemberNameChars, texts[i], kOptionChars);
        options[i] = texts[i];
    }
    options[picker.count] = reinterpret_cast<const wchar_t*>(readDword(sr2::kDialogNoTextPointer));
    return picker.count + 1;
}

// The dialog callback of the picker: cdecl(handle, result, action). A choice becomes the pending target and the game's confirm
// opens for it.
void __cdecl onPickerResult(uint32_t, int32_t result, int32_t action) {
    Entry chosen{};
    uintptr_t pickerSession = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        const size_t index = session_kick_rules::chosenIndex(g_state.picker, result, action);
        if (index == session_kick_rules::kNone) {
            if (g_trace && action == sr2::kDialogActionClose) {
                logger::write("trace: kick: picker closed, result %d action %d, nobody chosen", result, action);
            }
            return;
        }
        chosen = g_state.picker.entries[index];
        pickerSession = g_state.pickerSession;
    }
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    if (pickerSession != currentSession() || !session_kick_rules::stillPresent(chosen, snapshot.members, snapshot.memberCount)) {
        logger::write("kick: slot %u left before the choice, nobody is kicked", chosen.slot);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_state.pending = {pickerSession, chosen};
    }
    if (g_trace) logger::write("trace: kick: picker chose slot %u member 0x%08X", chosen.slot, static_cast<unsigned>(chosen.member));
    openGameConfirm(nullptr);
}

// Opens the picker for `picker`. True when a dialog was free.
bool openPicker(const session_kick_rules::Picker& picker, uintptr_t session) {
    wchar_t texts[sr2::kDialogMaxOptions][kOptionChars];
    const wchar_t* options[sr2::kDialogMaxOptions] = {};
    const size_t optionCount = fillOptions(picker, texts, options);
    const uint32_t title = guest_call::call(sr2::kLocalizeText, {.edi = 0, .esi = sr2::kKickPlayerTitleKey});
    const uint32_t record = guest_call::call(sr2::kDialogCreate, {.eax = static_cast<uint32_t>(picker.count)},
                                             {title, reinterpret_cast<uint32_t>(kEmptyBody), reinterpret_cast<uint32_t>(options),
                                              static_cast<uint32_t>(optionCount), sr2::kDialogPriorityAction, kNoConfirmation, kNoLastFlag});
    if (!record) return false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_state.picker = picker;
        g_state.pickerSession = session;
    }
    dwordField(record, sr2::kDialogCallbackOffset) = reinterpret_cast<uint32_t>(&onPickerResult);
    return true;
}

// KA1: coop_kick_player. The host with two or more remotes picks one first; every other case is the game's confirm.
int __cdecl kickPickerOrConfirm(void* lua) {
    clearPending();
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    const session_kick_rules::Picker picker = session_kick_rules::pickerEntries(snapshot.members, snapshot.memberCount);
    const size_t remotes = picker.count + picker.skipped;
    const uintptr_t session = currentSession();
    if (!session_kick_rules::showsPicker(isHost(session), remotes)) {
        if (g_trace && remotes == 1) logger::write("trace: kick: 1 remote member, the game's confirm names the default partner");
        return reinterpret_cast<KickFunction>(g_kickOriginal)(lua);
    }
    if (g_trace) {
        logger::write("trace: kick: picker for %zu remote members: slots %s", picker.count,
                      session_kick_rules::slotsText(picker.entries, picker.count).c_str());
        if (picker.skipped > 0) logger::write("trace: kick: %zu more remote members are not offered", picker.skipped);
    }
    if (!openPicker(picker, session)) logger::write("kick: no free dialog, picker not shown");
    return 0;
}

// Who the game's confirm builder, confirm callback and kick_coop_player get from their getter call: the chosen remote while it is
// still there, else the game's partner.
struct Answer {
    uintptr_t member;
    uint8_t slot;
    bool chosen;
};

Answer answerNow() {
    session_kick_rules::Pending pending;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        pending = g_state.pending;
    }
    if (pending.entry.member) {
        remote_players::Snapshot snapshot;
        remote_players::takeMembers(snapshot);
        const uintptr_t member = session_kick_rules::resolvePending(pending, currentSession(), snapshot.members, snapshot.memberCount);
        if (member) return {member, pending.entry.slot, true};
        if (g_trace) logger::write("trace: kick: chosen slot %u is gone, the default partner answers", pending.entry.slot);
    }
    const uintptr_t partner = partner_scope::partnerMember();
    return {partner, slotOf(partner), false};
}

void traceAnswer(const char* what, const Answer& answer) {
    if (!g_trace || !answer.member) return;
    logger::write("trace: kick: %s slot %u member 0x%08X (%s)", what, answer.slot, static_cast<unsigned>(answer.member),
                  answer.chosen ? "chosen" : "default partner");
}

// KA2: the member the confirm names.
uintptr_t __cdecl kickTargetNamed() {
    const Answer answer = answerNow();
    traceAnswer("confirm names", answer);
    return answer.member;
}

// KA3, KA4: the member that is removed; the choice is used up.
uintptr_t __cdecl kickTargetTaken() {
    const Answer answer = answerNow();
    clearPending();
    traceAnswer("kicking", answer);
    return answer.member;
}

// KA5: removes the member the game passed and then every other remote, each only while it is still a member (the removal frees it).
void __cdecl removeEveryRemote(uintptr_t session, uintptr_t passed) {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    Entry order[partner_rules::kMaxWalked];
    const size_t count = session_kick_rules::removalOrder(passed, snapshot.members, snapshot.memberCount, order, partner_rules::kMaxWalked);
    if (g_trace && count > 0) {
        logger::write("trace: kick: session end removes %zu remote members: slots %s", count, session_kick_rules::slotsText(order, count).c_str());
    }
    for (size_t i = 0; i < count; ++i) {
        if (remote_players::isRemoteMember(order[i].member)) guest_call::call(sr2::kSessionRemoveMember, {.eax = static_cast<uint32_t>(session)}, {static_cast<uint32_t>(order[i].member)});
    }
}

// Replaces `call 0x80F9B0` with eax = the session and the member pushed; the stdcall pops it.
void __declspec(naked) removeEveryRemoteStub() {
    __asm {
        pushad
        push dword ptr [esp + 0x24]
        push eax
        call removeEveryRemote
        add esp, 8
        popad
        ret 4
    }
}

}  // namespace

namespace session_kick {

partner_thunks::Binding bindingOf(Thunk thunk) {
    switch (thunk) {
        case Thunk::KickPickerOrConfirm: return {reinterpret_cast<void*>(&kickPickerOrConfirm), &g_kickOriginal};
        case Thunk::KickTargetNamed: return {reinterpret_cast<void*>(&kickTargetNamed), nullptr};
        case Thunk::KickTargetTaken: return {reinterpret_cast<void*>(&kickTargetTaken), nullptr};
        case Thunk::RemoveEveryRemoteMember: return {reinterpret_cast<void*>(&removeEveryRemoteStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void install(bool trace) {
    g_trace = trace;
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        if (!site_installer::sitesMatch(set.sites)) {
            logger::write("players 3 and 4: %s not patched", set.name);
            continue;
        }
        const site_installer::Result result = site_installer::install(set.sites, false, &bindingOf);
        if (result.installed) {
            logger::write("players 3 and 4: %s, %zu sites patched", set.name, result.patched);
        } else {
            logger::write("players 3 and 4: %s not patched", set.name);
        }
    }
}

}  // namespace session_kick
