#include "guest_welcome.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "creation_queue.h"
#include "guest_call.h"
#include "guest_welcome_rules.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

std::mutex g_mutex;
guest_welcome_rules::WelcomedSet g_welcomed;
bool g_trace = false;
const uintptr_t g_callbackDone = sr2::kMemberCallbackEpilogue;
const uintptr_t g_resetFirst = sr2::kInGameResetFirst;
const uintptr_t g_resetSecond = sr2::kInGameResetSecond;

uint8_t slotOf(uintptr_t member) {
    uint8_t slot = 0;
    readMemory(member + sr2::kMemberSlotOffset, &slot, sizeof(slot));
    return slot;
}

// The members of the session as welcome keys.
std::vector<guest_welcome_rules::Key> presentMembers(uintptr_t session) {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    std::vector<guest_welcome_rules::Key> keys;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        keys.push_back({session, snapshot.members[i].address, snapshot.members[i].slot});
    }
    return keys;
}

// The look sync to `member`: our local player's look, as the game's J4 / J5 send it to a member.
void syncLook(uintptr_t member, uint32_t human) {
    guest_call::call(sr2::kLookSyncA, {.ecx = static_cast<uint32_t>(member), .eax = human});
    guest_call::call(sr2::kLookSyncB, {.ecx = static_cast<uint32_t>(member), .eax = human});
}

// G1: the state of `member` just changed on a guest. A member that is ready gets our objects once; our look goes to every
// welcomed member that is owed it as soon as our own member is in the game, which may be this very callback (the joiner
// arrived while we were still joining, or our own member just reached 0xD).
void __cdecl welcomeMember(uintptr_t member) {
    const uintptr_t session = readDword(member + sr2::kMemberSessionOffset);
    const uintptr_t own = readDword(session + sr2::kSessionLocalMemberOffset);
    const uint32_t human = readDword(sr2::kLocalPlayer);
    const bool ownInGame = human && own && guest_welcome_rules::looksSyncable(readDword(own + sr2::kMemberStateOffset));
    const guest_welcome_rules::Key key{session, member, slotOf(member)};
    bool queueObjects = false;
    std::vector<guest_welcome_rules::Key> owed;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_welcomed.keepOnly(session, presentMembers(session));
        if (!guest_welcome_rules::due(readDword(member + sr2::kMemberStateOffset))) {
            g_welcomed.forget(key);
            return;
        }
        queueObjects = g_welcomed.add(key);
        owed = g_welcomed.takeOwedLooks(own, ownInGame);
    }
    if (queueObjects) guest_call::call(sr2::kQueueOwnedObjects, {.edx = static_cast<uint32_t>(member)});
    for (const guest_welcome_rules::Key& target : owed) syncLook(target.member, human);
    if (!g_trace) return;
    const bool lookSent = std::find(owed.begin(), owed.end(), key) != owed.end();
    if (queueObjects) {
        logger::write("trace: welcomed member 0x%X slot %u, queued %u objects, look %s", static_cast<unsigned>(member), key.slot,
                      creation_queue::queued(member), lookSent ? "sent" : "owed");
    }
    for (const guest_welcome_rules::Key& target : owed) {
        if (target == key && queueObjects) continue;
        logger::write("trace: look sent to welcomed member 0x%X slot %u, which was owed it", static_cast<unsigned>(target.member), target.slot);
    }
}

// Replaces `cmp [edi+0x3C], 0xD; jne done` of the guest branch: the welcome, then the original reset on state 0xD.
void __declspec(naked) guestWelcomeStub() {
    __asm {
        push edi
        call welcomeMember
        add esp, 4
        cmp dword ptr [edi + 0x3C], 0xD
        jne done
        or eax, -1
        mov ecx, dword ptr [g_resetFirst]
        mov dword ptr [ecx], eax
        mov ecx, dword ptr [g_resetSecond]
        mov dword ptr [ecx], eax
    done:
        jmp dword ptr [g_callbackDone]
    }
}

}  // namespace

namespace guest_welcome {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t thunk() { return reinterpret_cast<uintptr_t>(&guestWelcomeStub); }

}  // namespace guest_welcome
