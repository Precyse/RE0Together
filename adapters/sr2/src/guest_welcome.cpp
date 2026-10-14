#include "guest_welcome.h"

#include <mutex>
#include <vector>

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

// The look sync to `member`, when our own member is in the game.
void syncLook(uintptr_t session, uintptr_t member) {
    const uintptr_t own = readDword(session + sr2::kSessionLocalMemberOffset);
    const uint32_t human = readDword(sr2::kLocalPlayer);
    if (!human || !guest_welcome_rules::looksSyncable(readDword(own + sr2::kMemberStateOffset))) return;
    guest_call::call(sr2::kLookSyncA, {.ecx = static_cast<uint32_t>(member), .eax = human});
    guest_call::call(sr2::kLookSyncB, {.ecx = static_cast<uint32_t>(member), .eax = human});
}

// G1: the state of `member` just changed on a guest.
void __cdecl welcomeMember(uintptr_t member) {
    const uintptr_t session = readDword(member + sr2::kMemberSessionOffset);
    const guest_welcome_rules::Key key{session, member, slotOf(member)};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_welcomed.keepOnly(session, presentMembers(session));
        if (!guest_welcome_rules::due(readDword(member + sr2::kMemberStateOffset))) {
            g_welcomed.forget(key);
            return;
        }
        if (!g_welcomed.add(key)) return;
    }
    guest_call::call(sr2::kQueueOwnedObjects, {.edx = static_cast<uint32_t>(member)});
    syncLook(session, member);
    if (g_trace) {
        logger::write("trace: welcomed member 0x%X slot %u, queued %u objects", static_cast<unsigned>(member), key.slot,
                      readDword(member + sr2::kMemberKnownCountOffset));
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
