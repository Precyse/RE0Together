#include "char_types.h"

#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "char_types_rules.h"
#include "char_types_sites.h"
#include "code_patch.h"
#include "guest_call.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "partner_sites.h"
#include "sr2_addresses.h"

namespace {

namespace rules = char_types_rules;

constexpr uint8_t kNop = 0x90;
constexpr const char* kPlayerTypeName = "styletest_pc";
constexpr uint32_t kRequestKind = 5;  // the arguments of the shipped block for mp2 (0x69CB18 to 0x69CB24)
constexpr uint32_t kRequestFlags = 1;
constexpr uint32_t kLoadedMask = 0xFF;  // 0xA79490 returns al
constexpr const char* kJoiner = ", ";

struct Block {
    void* memory = nullptr;
    size_t bytes = 0;
};

struct TypePair {
    uint32_t type;
    uint32_t record;
};

bool g_trace = false;
void(__cdecl* g_original)() = nullptr;
Block g_cpuBlock;  // kept for the process: the game holds them in category 3 across co-op sessions
Block g_gpuBlock;
const uintptr_t g_ct2Resume = sr2::kCoopLoaderResume;
const uintptr_t g_ct3Resume = sr2::kCoopTableFillResume;
const uintptr_t g_multiplayerFlag = sr2::kMultiplayerFlag;

uint32_t guestAddress(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

std::string shippedName() { return rules::typeName(rules::kShippedTypeNumber); }

uint32_t hashOf(const std::string& name) {
    uint32_t hash = 0;
    guest_call::call(sr2::kStringHash, {.ecx = guestAddress(name.c_str())}, {guestAddress(&hash)});
    return hash;
}

// The character entry whose name hash is `hash` (the walk of 0x4A47B0), or 0.
uintptr_t charEntryOf(uint32_t hash) {
    const int32_t count = static_cast<int32_t>(readDword(sr2::kCharEntryCount));
    const uintptr_t entries = readDword(sr2::kCharEntries);
    for (int32_t i = 0; i < count; ++i) {
        const uintptr_t entry = entries + static_cast<uintptr_t>(i) * sr2::kCharEntryStride;
        if (readDword(entry + sr2::kCharEntryHashOffset) == hash) return entry;
    }
    return 0;
}

// The record in the registry whose id is `id`, or 0.
uintptr_t recordWithId(uint32_t id) {
    std::array<uint32_t, sr2::kRecordRegistryCount> ids{};
    std::array<uintptr_t, sr2::kRecordRegistryCount> records{};
    for (size_t slot = 0; slot < records.size(); ++slot) {
        records[slot] = readDword(sr2::kRecordRegistry + slot * sizeof(uint32_t));
        ids[slot] = records[slot] ? readDword(records[slot] + sr2::kCharRecordIdOffset) : rules::kNoRecordId;
    }
    const int slot = rules::recordFor(id, ids.data(), ids.size());
    return slot == rules::kNoRecord ? 0 : records[static_cast<size_t>(slot)];
}

// ---- CT1 ----

bool ensureBlock(Block& block, size_t bytes) {
    if (block.bytes >= bytes) return true;
    void* memory = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) return false;
    block = {memory, bytes};
    return true;
}

struct SlotSizes {
    uint32_t cpu = 0;
    uint32_t gpu = 0;
};

// Sizes category 3 for `slots` characters as 0x9D65D0 does outside co-op, with adapter memory in place of the MP pool. The reason it
// could not, or nullptr.
const char* sizeCategoryThree(uint32_t slots, SlotSizes& slot) {
    const uintptr_t entry = charEntryOf(hashOf(kPlayerTypeName));
    if (!entry) return "no styletest_pc entry";
    const uint32_t id = readDword(entry + sr2::kCharEntryIdOffset);
    uint32_t cpu = 0;
    uint32_t gpu = 0;
    guest_call::call(sr2::kCategoryNodeSizes, {.edi = guestAddress(&cpu), .esi = guestAddress(&gpu), .edx = id,
                                               .eax = static_cast<uint32_t>(rules::categoryBase(rules::categoryOf(id)))});
    if (cpu == 0 || gpu == 0) return "styletest_pc has no node sizes";
    slot = {rules::slotBytes(cpu), rules::slotBytes(gpu)};
    if (!ensureBlock(g_cpuBlock, static_cast<size_t>(slots) * slot.cpu) || !ensureBlock(g_gpuBlock, static_cast<size_t>(slots) * slot.gpu)) {
        return "allocation failed";
    }
    const uintptr_t category = rules::categoryBase(rules::kExtraPlayerCategory);
    dwordField(category, sr2::kCategoryCpuSlotOffset) = slot.cpu;
    dwordField(category, sr2::kCategoryCpuBaseOffset) = guestAddress(g_cpuBlock.memory);
    dwordField(category, sr2::kCategoryGpuSlotOffset) = slot.gpu;
    dwordField(category, sr2::kCategoryGpuBaseOffset) = guestAddress(g_gpuBlock.memory);
    guest_call::call(sr2::kCategoryResetSlots, {.eax = static_cast<uint32_t>(category)});
    return nullptr;
}

// Stands in for 0x9D65D0: outside co-op the game's own sizing, in co-op category 3 sized for the extra characters.
void __cdecl categoryThreeDetour() {
    if (!readByte(sr2::kCoopFlag)) {
        g_original();
        return;
    }
    const uint32_t slots = rules::extraCharacterSlots(readDword(sr2::kMaxPlayers));
    SlotSizes slot;
    const char* failure = slots == 0 ? "no extra characters at this player count" : sizeCategoryThree(slots, slot);
    if (failure) {
        logger::write("char types: category 3 not sized (%s), remotes keep %s", failure, shippedName().c_str());
        return;
    }
    logger::write("char types: category 3 sized for %u remote characters, slot cpu 0x%X gpu 0x%X, blocks 0x%08X 0x%08X", slots, slot.cpu, slot.gpu,
                  guestAddress(g_cpuBlock.memory), guestAddress(g_gpuBlock.memory));
}

// ---- CT2 ----

// What the shipped block does for styletest_pc_mp2, for styletest_pc_mp<number>: its trace text, or empty when it was skipped.
std::string streamType(uint32_t number) {
    const std::string name = rules::typeName(number);
    const uintptr_t entry = charEntryOf(hashOf(name));
    if (!entry) {
        logger::write("char types: level load skips %s, no entry", name.c_str());
        return {};
    }
    const uint32_t id = readDword(entry + sr2::kCharEntryIdOffset);
    const uintptr_t category = rules::categoryBase(rules::kExtraPlayerCategory);
    if (rules::categoryOf(id) != rules::kExtraPlayerCategory || readDword(category + sr2::kCategoryCpuSlotOffset) == 0) {
        logger::write("char types: level load skips %s, id 0x%08X is not in a sized category 3", name.c_str(), id);
        return {};
    }
    guest_call::call(sr2::kStreamRequest, {.eax = static_cast<uint32_t>(category)}, {id, kRequestKind, 0, kRequestFlags});
    guest_call::call(sr2::kStreamCategoryPump, {}, {rules::kExtraPlayerCategory, 0, 0});
    const uint32_t loaded = guest_call::call(sr2::kStreamLoaded, {.edi = id}) & kLoadedMask;
    const uintptr_t record = recordWithId(id);
    if (record) byteField(record, sr2::kRecordFlagsOffset) |= sr2::kRecordHeldFlag;
    return rules::streamedText(number, id, loaded, static_cast<uint32_t>(record));
}

void __cdecl streamExtraTypes() {
    if (!readByte(sr2::kCoopFlag) || readByte(sr2::kLoaderSkipFlag)) return;  // the entry test of the shipped block
    const uint32_t last = rules::kShippedTypeNumber + rules::extraCharacterSlots(readDword(sr2::kMaxPlayers));
    std::string streamed;
    for (uint32_t number = rules::kFirstExtraTypeNumber; number <= last; ++number) {
        const std::string text = streamType(number);
        if (text.empty()) continue;
        if (!streamed.empty()) streamed += kJoiner;
        streamed += text;
    }
    if (g_trace && !streamed.empty()) logger::write("trace: char types: level load streamed %s", streamed.c_str());
}

// Jumped out of at the merge point after the shipped block; runs the displaced `mov dl, [0x2528B48]` and goes on.
void __declspec(naked) streamTypesStub() {
    __asm {
        pushad
        call streamExtraTypes
        popad
        push eax
        mov eax, dword ptr [g_multiplayerFlag]
        mov dl, byte ptr [eax]
        pop eax
        jmp dword ptr [g_ct2Resume]
    }
}

// ---- CT3 ----

// The type of styletest_pc_mp<number> and its record; 0 for what is not there.
TypePair pairOf(uint32_t number) {
    const uint32_t result = guest_call::call(sr2::kTypeLookup, {}, {hashOf(rules::typeName(number)), 0});
    const uint32_t type = result ? readDword(result) : 0;
    const uint32_t entry = type ? readDword(type + sr2::kTypeEntryOffset) : 0;
    const uint32_t record = entry ? static_cast<uint32_t>(recordWithId(readDword(entry + sr2::kCharEntryIdOffset))) : 0;
    return {type, record};
}

// The pairs of one run of the fill, each looked up once.
class PairCache {
public:
    const TypePair& of(uint32_t number) {
        std::optional<TypePair>& slot = pairs_[number];
        if (!slot) slot = pairOf(number);
        return *slot;
    }

private:
    std::array<std::optional<TypePair>, rules::kLastTypeNumber + 1> pairs_{};
};

void writeCharTable(uint32_t index, const TypePair& pair) {
    const uintptr_t entry = sr2::kCharTables + index * sr2::kCharTableStride;
    dwordField(entry, 0) = pair.type;
    dwordField(entry, sr2::kCharTableRecordOffset) = pair.record;
}

// Fills the char table as the co-op block of 0x9D7FD0 does for mp2, but with the type of each pool index.
void __cdecl fillCharTables() {
    const uint32_t maxPlayers = readDword(sr2::kMaxPlayers);
    const uint32_t local = rules::poolIndexOf(readDword(sr2::kLocalPlayer));
    PairCache pairs;
    std::string filled;
    for (uint32_t index = sr2::kPlayerPoolCount; index-- > 0;) {
        uint32_t number = rules::remoteTypeNumber(index, local, maxPlayers);
        if (number == rules::kNotWritten) continue;
        if (number > rules::kShippedTypeNumber && (pairs.of(number).type == 0 || pairs.of(number).record == 0)) {
            logger::write("char types: pool index %u keeps %s, %s has no type/record", index, shippedName().c_str(), rules::typeName(number).c_str());
            number = rules::kShippedTypeNumber;
        }
        const TypePair& pair = pairs.of(number);
        writeCharTable(index, pair);
        if (index >= maxPlayers) continue;
        if (!filled.empty()) filled += kJoiner;
        filled += rules::assignmentText(index, number, pair.type, pair.record);
    }
    if (g_trace && !filled.empty()) logger::write("trace: char types: char table %s", filled.c_str());
}

// Jumped out of at the co-op fill of 0x9D7FD0; ebx = 1 stays, the code after the fill needs it.
void __declspec(naked) fillTablesStub() {
    __asm {
        pushad
        call fillCharTables
        popad
        jmp dword ptr [g_ct3Resume]
    }
}

// ---- install ----

uintptr_t stubOf(const char_types_sites::Row& row) {
    return reinterpret_cast<uintptr_t>(row.address == sr2::kCoopLoaderMerge ? &streamTypesStub : &fillTablesStub);
}

CodePatch patchOf(const char_types_sites::Row& row) {
    std::vector<uint8_t> expected = partner_sites::parseHex(row.expected);
    std::vector<uint8_t> replacement;
    if (row.kind == char_types_sites::Kind::JumpOut) {
        const auto branch = relativeBranch(kJmpRel32, row.address, stubOf(row));
        replacement.assign(branch.begin(), branch.end());
        replacement.resize(expected.size(), kNop);
    }
    return {row.id, row.address, std::move(expected), std::move(replacement)};
}

}  // namespace

namespace char_types {

void install(uint32_t maxPlayers, bool trace) {
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return;
    g_trace = trace;
    std::vector<CodePatch> patches;
    for (const char_types_sites::Row& row : char_types_sites::kRows) patches.push_back(patchOf(row));
    for (const CodePatch& patch : patches) {
        if (!codeMatches({patch})) {
            logger::write("char types: sites differ (%s), every remote keeps %s", patch.name, shippedName().c_str());
            return;
        }
    }
    if (!applyCodePatches(patches)) {
        logger::write("char types: the code edits were not applied, every remote keeps %s", shippedName().c_str());
        return;
    }
    if (!hooks::install("char_types_category3", sr2::kCharTypeCategorySetup, reinterpret_cast<void*>(&categoryThreeDetour),
                        reinterpret_cast<void**>(&g_original))) {
        logger::write("char types: the category 3 hook is not installed, remotes keep %s", shippedName().c_str());
        return;
    }
    logger::write("char types: %zu sites match, category 3 for remote characters, per-index character types", patches.size());
}

}  // namespace char_types
