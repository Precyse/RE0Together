#include "anim_stream.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <vector>

#include "activity_state.h"
#include "anim_stream_requests.h"
#include "anim_stream_rules.h"
#include "anim_stream_sites.h"
#include "anim_stream_state.h"
#include "anim_stream_triggers.h"
#include "anim_trace.h"
#include "code_patch.h"
#include "game_message.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "scoped_value.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using anim_stream_rules::kNoSlot;
using anim_stream_sites::NeedTableSite;
using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;
constexpr size_t kBufferArg = 0;
constexpr size_t kLengthArg = 1;

void* g_gateOriginal = nullptr;
void* g_needInitOriginal = nullptr;
bool g_trace = false;
uint32_t g_maxPlayers = 0;
std::array<bool, std::size(anim_stream_sites::kPhases)> g_phaseMatches{};
std::atomic<bool> g_moveAttempted{false};

// The member whose streaming_sync message the game's handler is running for, and the slot whose bit its need rows take.
thread_local uintptr_t t_handlerSender = 0;
thread_local std::optional<uint8_t> t_needOwner;

std::optional<uint8_t> ownerOf(uint8_t slot) {
    return slot == kNoSlot ? std::nullopt : std::optional<uint8_t>(slot);
}

// ---- AS1: the handler of streaming_sync ----

// AS1: cdecl(payload, context). The game's handler runs only for the messages anim_stream_rules::gate lets through, with the sender
// as the owner of the need bits it sets or clears and as the member an answer is sent to.
void __cdecl streamingSyncGate(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const uint8_t sub = readByte(payload);
    const uint16_t group = readWord(payload + sr2::kStreamingGroupOffset);
    const uintptr_t sender = activity_state::senderOf(context);
    const bool groupPending = sub == sr2::kStreamingSubAnswer && anim_stream_rules::pendingFor(anim_stream_state::pendingRequests(), group).found;
    const bool senderIsHost = sender != 0 && sender == remote_players::hostMember();
    const anim_stream_rules::Verdict verdict = anim_stream_rules::gate(sub, activity_state::isHost(), senderIsHost, groupPending);
    const uint8_t slot = anim_stream_state::slotOfMember(sender);
    if (verdict != anim_stream_rules::Verdict::Run) {
        if (g_trace) {
            logger::write("trace: anim sets: streaming_sync sub %u group 0x%04X%s from slot %u dropped: %s", sub, group,
                          anim_trace_rules::syncIdText(sub, readDword(payload + sr2::kStreamingIdOffset)).c_str(), slot,
                          anim_stream_rules::dropText(verdict));
        }
        return;
    }
    const ScopedValue<uintptr_t> handlerSender(t_handlerSender, sender);
    const ScopedValue<std::optional<uint8_t>> needOwner(t_needOwner, ownerOf(slot));
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_gateOriginal), frame->regs,
                                       {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
}

// ---- AS2 to AS5: the sends of streaming_sync ----

// The member a message of this sub goes to, 0 for everyone.
uintptr_t recipientMember(anim_stream_rules::Recipient recipient) {
    switch (recipient) {
        case anim_stream_rules::Recipient::Host: return remote_players::hostMember();
        case anim_stream_rules::Recipient::HandlerSender: return t_handlerSender;
        default: return 0;
    }
}

const char* missingRecipientText(anim_stream_rules::Recipient recipient) {
    switch (recipient) {
        case anim_stream_rules::Recipient::Host: return ", no host";
        case anim_stream_rules::Recipient::HandlerSender: return ", no requester";
        default: return "";
    }
}

// The four sends of streaming_sync (they called the broadcast 0x814740: edi = session, cdecl(buffer, length)). The message goes to the
// member it concerns, to everyone only when there is none.
void __cdecl sendStreamingSync(GuestFrame* frame) {
    const uint32_t buffer = frame->args[kBufferArg];
    const uint32_t length = frame->args[kLengthArg];
    const uintptr_t header = readByte(sr2::kStreamingSyncRelFlag) ? game_message::kWideHeaderBytes : game_message::kHeaderBytes;
    const uint8_t sub = readByte(buffer + header);
    const uint16_t group = readWord(buffer + header + sr2::kStreamingGroupOffset);
    const anim_stream_rules::Recipient recipient = anim_stream_rules::recipient(sub, t_handlerSender != 0);
    const uintptr_t member = recipientMember(recipient);
    if (member) {
        guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(member)}, {buffer, length, 0, 0});
        if (g_trace) logger::write("trace: anim sets: sent sub %u group 0x%04X to slot %u", sub, group, anim_stream_state::slotOfMember(member));
        return;
    }
    guest_call::call(sr2::kSendBroadcast, frame->regs, {buffer, length});
    if (g_trace) logger::write("trace: anim sets: sent sub %u group 0x%04X broadcast%s", sub, group, missingRecipientText(recipient));
}

// ---- AM1 to AM3: the remote byte of a need row is a mask ----

// The row's remote byte after the game's store of 1 (a need set) or 0 (a need clear), for the member whose message is handled.
void setNeedBit(uintptr_t row) {
    uint8_t& mask = byteField(row, sr2::kAnimNeedRemoteOffset);
    mask = anim_stream_rules::maskAfterSet(mask, t_needOwner);
}

void clearNeedBit(uintptr_t row) {
    uint8_t& mask = byteField(row, sr2::kAnimNeedRemoteOffset);
    mask = anim_stream_rules::maskAfterClear(mask, t_needOwner);
}

void __cdecl setNeedBitEdx(GuestFrame* frame) { setNeedBit(frame->regs.edx); }
void __cdecl setNeedBitEbp(GuestFrame* frame) { setNeedBit(frame->regs.ebp); }
void __cdecl clearNeedBitEsi(GuestFrame* frame) { clearNeedBit(frame->regs.esi); }

// ---- AL1: a member left ----

struct NeedClears {
    size_t cleared;
    size_t freed;
};

// The game's own clear of each remote row the leaver holds a bit of; the game frees the row when no one needs it any more.
NeedClears clearLeaverNeeds(uint8_t slot) {
    NeedClears clears{0, 0};
    const ScopedValue<std::optional<uint8_t>> needOwner(t_needOwner, ownerOf(slot));
    const anim_trace_rules::NeedTableView view = anim_stream_state::needTableView();
    for (size_t i = 0; i < view.rows; ++i) {
        const uintptr_t row = view.base + i * sr2::kAnimNeedRowBytes;
        const uint16_t group = readWord(row + sr2::kAnimNeedGroupOffset);
        const bool local = readByte(row + sr2::kAnimNeedLocalOffset) != 0;
        if (local || group == sr2::kNoGroup || (readByte(row + sr2::kAnimNeedRemoteOffset) & anim_stream_rules::slotBit(slot)) == 0) continue;
        guest_call::call(sr2::kAnimNeedClear, {.edi = group, .edx = 0});
        ++clears.cleared;
        if (readWord(row + sr2::kAnimNeedGroupOffset) == sr2::kNoGroup) ++clears.freed;
    }
    return clears;
}

// AL1: the host's member left callback cleared every remote need (0xA84170, no registers); now it clears the leaver's bits only and
// releases the stream requests the leaver held. esi = the leaver.
void __cdecl leaverReleases(GuestFrame* frame) {
    const uintptr_t leaver = frame->regs.esi;
    const uint8_t slot = anim_stream_state::slotOfMember(leaver);
    const NeedClears clears = slot == kNoSlot ? NeedClears{0, 0} : clearLeaverNeeds(slot);
    const anim_stream_requests::Released released = anim_stream_requests::releaseLeaver(leaver);
    if (g_trace) {
        logger::write("trace: anim sets: slot %u left: need cleared in %zu rows, %zu freed; stream requests released %u (world %u, character %u)",
                      slot, clears.cleared, clears.freed, released.world + released.character, released.world, released.character);
    }
}

// ---- AT0: the need table moves ----

// The instruction of a need table site with its dword moved along with the table.
std::vector<uint8_t> movedBytes(const NeedTableSite& site, uint32_t newBase, size_t rows) {
    std::vector<uint8_t> bytes = partner_sites::parseHex(site.expected);
    const uint32_t moved = anim_stream_rules::relocatedNeedRef(anim_stream_sites::immediateOf(site), newBase, rows);
    for (size_t i = 0; i < sizeof(moved); ++i) bytes[site.immediateOffset + i] = static_cast<uint8_t>(moved >> (8 * i));
    return bytes;
}

std::vector<CodePatch> needTablePatches(std::span<const NeedTableSite> sites, bool moved, uint32_t newBase, size_t rows) {
    std::vector<CodePatch> patches;
    for (const NeedTableSite& site : sites) {
        patches.push_back({site.id, site.address, partner_sites::parseHex(site.expected),
                           moved ? movedBytes(site, newBase, rows) : std::vector<uint8_t>{}});
    }
    return patches;
}

// Moves the table to a block of the rows `max_players` needs, once. The game's reset (0xA83400) then initialises every row of it.
void moveTable() {
    const size_t rows = anim_stream_rules::needRows(g_maxPlayers);
    if (!codeMatches(needTablePatches(anim_stream_sites::kNeedTable, false, 0, 0))) {
        logger::write("anim sets: need table sites differ, stays at %zu rows", sr2::kAnimNeedRows);
        return;
    }
    void* block = VirtualAlloc(nullptr, rows * sr2::kAnimNeedRowBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!block) {
        logger::write("anim sets: need table stays at %zu rows, %zu rows unavailable", sr2::kAnimNeedRows, rows);
        return;
    }
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    if (!applyCodePatches(needTablePatches(anim_stream_sites::kNeedTable, true, base, rows))) {
        VirtualFree(block, 0, MEM_RELEASE);
        logger::write("anim sets: need table stays at %zu rows, the code edits were not applied", sr2::kAnimNeedRows);
        return;
    }
    logger::write("anim sets: need table %zu -> %zu rows at 0x%08X (thread %lu)", sr2::kAnimNeedRows, rows, base, GetCurrentThreadId());
}

void __cdecl moveNeedTable(GuestFrame*) {
    if (!g_moveAttempted.exchange(true)) moveTable();
}

PARTNER_WRAP_STUB(streamingSyncGateStub, streamingSyncGate, 0)
PARTNER_WRAP_STUB(sendStreamingSyncStub, sendStreamingSync, 0)
PARTNER_WRAP_STUB(setNeedBitEdxStub, setNeedBitEdx, 0)
PARTNER_WRAP_STUB(setNeedBitEbpStub, setNeedBitEbp, 0)
PARTNER_WRAP_STUB(clearNeedBitEsiStub, clearNeedBitEsi, 0)
PARTNER_WRAP_STUB(leaverReleasesStub, leaverReleases, 0)
TRACE_ENTRY_STUB(moveNeedTableStub, moveNeedTable, g_needInitOriginal)

// ---- the phases ----

bool immediatesMatch(std::span<const NeedTableSite> sites) {
    return sites.empty() || codeMatches(needTablePatches(sites, false, 0, 0));
}

}  // namespace

namespace anim_stream {

bool sitesMatch() {
    bool any = false;
    for (size_t i = 0; i < std::size(anim_stream_sites::kPhases); ++i) {
        const anim_stream_sites::Phase& phase = anim_stream_sites::kPhases[i];
        g_phaseMatches[i] = site_installer::sitesMatch(phase.sites) && immediatesMatch(phase.immediates);
        any = any || g_phaseMatches[i];
    }
    return any;
}

void install(uint32_t maxPlayers, bool trace) {
    g_maxPlayers = maxPlayers;
    g_trace = trace;
    anim_stream_requests::setTrace(trace);
    anim_stream_triggers::setTrace(trace);
    for (size_t i = 0; i < std::size(anim_stream_sites::kPhases); ++i) {
        const anim_stream_sites::Phase& phase = anim_stream_sites::kPhases[i];
        if (phase.traceOnly && !trace) continue;
        if (!g_phaseMatches[i]) {
            logger::write("anim streaming: %s not patched, the bytes differ", phase.name);
            continue;
        }
        if (site_installer::install(phase.sites, &bindingOf).installed) {
            logger::write("anim streaming: %s, %zu sites patched", phase.name, anim_stream_sites::writtenCount(phase));
        } else {
            logger::write("anim streaming: %s not patched", phase.name);
        }
    }
}

partner_thunks::Binding bindingOf(Thunk thunk) {
    switch (thunk) {
        case Thunk::StreamingSyncGate: return {reinterpret_cast<void*>(&streamingSyncGateStub), &g_gateOriginal};
        case Thunk::SendStreamingSync: return {reinterpret_cast<void*>(&sendStreamingSyncStub), nullptr};
        case Thunk::SetNeedBitEdx: return {reinterpret_cast<void*>(&setNeedBitEdxStub), nullptr};
        case Thunk::SetNeedBitEbp: return {reinterpret_cast<void*>(&setNeedBitEbpStub), nullptr};
        case Thunk::ClearNeedBitEsi: return {reinterpret_cast<void*>(&clearNeedBitEsiStub), nullptr};
        case Thunk::LeaverReleases: return {reinterpret_cast<void*>(&leaverReleasesStub), nullptr};
        case Thunk::MoveNeedTable: return {reinterpret_cast<void*>(&moveNeedTableStub), &g_needInitOriginal};
        default: break;
    }
    for (const auto bind : {&anim_stream_requests::bind, &anim_stream_triggers::bind, &anim_trace::bind}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace anim_stream
