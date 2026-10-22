// The Mayhem counters and score blocks of every remote (rows MB0 to MB4 of walk_sites.h). State lives here and is touched on the game thread
// only: the destruction counters per district entry, the score blocks, and the template a new block starts from.
#include "mayhem_blocks.h"

#include <array>
#include <bit>

#include "activity_state.h"
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_stubs.h"
#include "diversion_trace.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_rules.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "walk_rules.h"

namespace {

using partner_sites::Thunk;
using walk_rules::Side;

using ScoreBlock = std::array<uint8_t, sr2::kMayhemBlockSize>;

constexpr size_t kNone = partner_rules::kNone;

const uintptr_t g_loopTop = sr2::kMayhemBonusLoopTop;
const uintptr_t g_loopAfter = sr2::kMayhemBonusLoopAfter;
const uintptr_t g_epsilon = sr2::kMayhemBonusEpsilon;

diversion_rules::SlotTable<int32_t> g_counts[sr2::kMayhemEntryCount];  // per district entry: the destructions of each other remote
diversion_rules::SlotTable<ScoreBlock> g_blocks;                       // the score block of each other remote
ScoreBlock g_template{};                                               // a fresh score block: what the init leaves in the second one
rate_limit::RateLimit g_countLimit;
rate_limit::RateLimit g_blockLimit;

bool tracing() { return activity_state::tracing(); }

// Who destroyed something, as the pairing sees it: the index of the remote (kNone for anyone that is not a remote player human, and
// when the pairing cannot be trusted, which leaves the game's own counters) and whether it is the default partner.
struct Destroyer {
    size_t at;
    bool isDefault;
};

Destroyer destroyerOf(const diversion_remotes::View& view, uintptr_t human) {
    const size_t at = view.usable() ? view.indexOf(human) : kNone;
    return {at, at != kNone && at == view.defaultIndex};
}

// The position of a district entry in the level's table of three, kNone for any other pointer.
size_t entryIndexOf(uintptr_t level, uintptr_t entry) {
    for (size_t i = 0; i < sr2::kMayhemEntryCount; ++i) {
        if (readDword(level + sr2::kMayhemEntriesOffset + i * sizeof(uint32_t)) == entry) return i;
    }
    return kNone;
}

// MB0 (ebx = the level): the init resets the score blocks; this keeps the reset the replaced store made and starts the adapter's counters and
// blocks over, from a copy of the fresh second block.
void __cdecl blocksInit(BlockFrame* frame) {
    const uintptr_t level = frame->regs.ebx;
    dwordField(level, sr2::kMayhemResetField) = 0;
    readMemory(level + sr2::kMayhemPartnerBlockOffset, g_template.data(), g_template.size());
    for (diversion_rules::SlotTable<int32_t>& counts : g_counts) counts.clearAll();
    g_blocks.clearAll();
    if (tracing()) logger::write("trace: mayhem: score blocks reset for level 0x%X", static_cast<unsigned>(level));
}

// MB1, MB2 (eax = the district entry, edx = the destroyer is not the local player, edi = the record, esi = the level): the destruction raises
// the counter the rule names. The local and the game's counters are the add the block replaced.
void __cdecl countDestruction(BlockFrame* frame) {
    const uintptr_t entry = frame->regs.eax;
    const uintptr_t destroyer = readDword(frame->regs.edi + sr2::kMayhemDestroyerOffset);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    const Destroyer who = destroyerOf(view, destroyer);
    const size_t index = entryIndexOf(frame->regs.esi, entry);
    Side side = walk_rules::destructionCounter(frame->regs.edx == 0, who.at != kNone, who.isDefault);
    if (side == Side::Remote && index == kNone) side = Side::Game;
    if (side == Side::Local) {
        dwordField(entry, sr2::kMayhemEntryLocalCount) += 1;
    } else if (side == Side::Game) {
        dwordField(entry, sr2::kMayhemEntryRemoteCount) += 1;
    } else {
        const uint8_t slot = view.items()[who.at].slot;
        const int32_t count = g_counts[index].valueOr(slot, destroyer, 0) + 1;
        g_counts[index].set(slot, destroyer, count);
        if (diversion_trace::dueNow(g_countLimit)) {
            FpuGuard guard;
            logger::write("trace: mayhem: destruction by remote 0x%X counted for slot %u (entry 0x%X, count %d)", static_cast<unsigned>(destroyer),
                          static_cast<unsigned>(slot), static_cast<unsigned>(entry), count);
        }
    }
}

// MB3 (esi = the entry's pointer in the level's table, ebp = the level): the bonus loop pays the local player and the default partner; this
// pays every other remote its own count, with the rule of the game's loop, and starts its count over. The float math is under the guard.
void __cdecl payRemotes(JumpFrame* frame) {
    const uintptr_t first = frame->regs.ebp + sr2::kMayhemEntriesOffset;
    const uintptr_t pointer = frame->regs.esi;
    if (pointer < first) return;
    const uintptr_t entry = readDword(pointer);
    const size_t index = (pointer - first) / sizeof(uint32_t);
    if (!entry || index >= sr2::kMayhemEntryCount) return;
    FpuGuard guard;
    diversion_remotes::View view;
    diversion_remotes::take(view);
    if (!view.usable()) return;
    const float multiplier = std::bit_cast<float>(readDword(entry + sr2::kMayhemEntryMultiplier));
    const float epsilon = std::bit_cast<float>(readDword(sr2::kMayhemBonusEpsilon));
    const int32_t perUnit = static_cast<int32_t>(readDword(entry + sr2::kMayhemEntryPerUnit));
    for (size_t i = 0; i < view.count(); ++i) {
        const partner_rules::Remote& remote = view.items()[i];
        if (!remote.human || i == view.defaultIndex) continue;
        const int32_t count = g_counts[index].valueOr(remote.slot, remote.human, 0);
        if (count <= 0) continue;
        const walk_rules::Bonus bonus = walk_rules::bonusFor(count, multiplier, epsilon, perUnit);
        if (bonus.pay) {
            guest_call::call(sr2::kMayhemBonus, {.ecx = static_cast<uint32_t>(remote.human)},
                             {readDword(entry + sr2::kMayhemEntryId), readDword(entry + sr2::kMayhemEntryTarget), bonus.kind,
                              static_cast<uint32_t>(bonus.amount)});
            if (tracing()) {
                logger::write("trace: mayhem: bonus kind %u amount %d to remote 0x%X (slot %u)", bonus.kind, bonus.amount,
                              static_cast<unsigned>(remote.human), static_cast<unsigned>(remote.slot));
            }
        }
        g_counts[index].set(remote.slot, remote.human, 0);
    }
}

// The game drops the 0.01 it keeps on the x87 stack through the loop (so 0x668030 runs with an empty stack, as the game itself calls it),
// the payout runs, and the loop goes on as the instructions this replaced did: the next entry, or the end.
void __declspec(naked) payRemotesStub() {
    __asm {
        fstp st(0)
        pushfd
        pushad
        push esp
        call payRemotes
        add esp, 4
        popad
        popfd
        push eax
        mov eax, g_epsilon
        fld dword ptr [eax]
        pop eax
        add esi, 4
        sub dword ptr [esp + 0x14], 1
        je finished
        jmp dword ptr [g_loopTop]
    finished:
        jmp dword ptr [g_loopAfter]
    }
}

// The adapter's score block of a remote, started from the template when the slot is new or holds another human. 0 when the slot has no table.
uintptr_t remoteBlock(uint8_t slot, uintptr_t human) {
    ScoreBlock* block = g_blocks.get(slot, human);
    if (!block) {
        g_blocks.set(slot, human, g_template);
        block = g_blocks.get(slot, human);
    }
    return block ? reinterpret_cast<uintptr_t>(block->data()) : 0;
}

// MB4 (eax = the game's index, 0 the local player's block and 1 the second one, esi = the level, edi = the record): ebx = the block the
// destruction is written to.
void __cdecl scoreBlockOf(BlockFrame* frame) {
    const uintptr_t level = frame->regs.esi;
    const uint32_t gameIndex = frame->regs.eax;
    const uintptr_t destroyer = readDword(frame->regs.edi + sr2::kMayhemDestroyerOffset);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    const Destroyer who = destroyerOf(view, destroyer);
    uintptr_t block = level + sr2::kMayhemBlocksOffset + gameIndex * sr2::kMayhemBlockSize;
    if (walk_rules::scoreBlock(gameIndex, who.at != kNone, who.isDefault) == Side::Remote) {
        const uint8_t slot = view.items()[who.at].slot;
        const uintptr_t own = remoteBlock(slot, destroyer);
        if (own) block = own;
        if (own && diversion_trace::dueNow(g_blockLimit)) {
            FpuGuard guard;
            logger::write("trace: mayhem: score block of slot %u human 0x%X at 0x%X total %u", static_cast<unsigned>(slot),
                          static_cast<unsigned>(destroyer), static_cast<unsigned>(block), readDword(block + sr2::kMayhemBlockTotalOffset));
        }
    }
    frame->regs.ebx = static_cast<uint32_t>(block);
}

DIVERSION_BLOCK_STUB(blocksInitStub, blocksInit)
DIVERSION_BLOCK_STUB(countDestructionStub, countDestruction)
DIVERSION_BLOCK_STUB(scoreBlockStub, scoreBlockOf)

}  // namespace

namespace mayhem_blocks {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::MayhemBlocksInit: return {reinterpret_cast<void*>(&blocksInitStub), nullptr};
        case Thunk::MayhemCountDestruction: return {reinterpret_cast<void*>(&countDestructionStub), nullptr};
        case Thunk::MayhemPayRemotes: return {reinterpret_cast<void*>(&payRemotesStub), nullptr};
        case Thunk::MayhemScoreBlock: return {reinterpret_cast<void*>(&scoreBlockStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace mayhem_blocks
