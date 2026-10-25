#include "save_blocks.h"

#include <array>

#include "fnv1a32.h"
#include "game.h"
#include "save_load_rule.h"

namespace {

constexpr size_t kHashChunkBytes = 2384;  // divides the slot block (49 chunks), so a read never leaves the block
static_assert(game::kSaveSlotBytes % kHashChunkBytes == 0);

uintptr_t slotAddress(uintptr_t requestObject, uintptr_t offset, int32_t slot) {
    return requestObject + offset + static_cast<size_t>(slot) * game::kSaveSlotBytes;
}

std::optional<save_index_rule::Summary> summaryAt(uintptr_t offset, int32_t slot) {
    const uintptr_t requestObject = save_blocks::request();
    std::array<uint8_t, save_index_rule::kSummaryBytes> head{};
    if (!requestObject || !save_load_rule::isPlayerSlot(slot) || !game::readMemory(slotAddress(requestObject, offset, slot), head)) {
        return std::nullopt;
    }
    return save_index_rule::summarize(head);
}

}  // namespace

namespace save_blocks {

uintptr_t request() {
    const uintptr_t manager = game::readPointer(game::kSaveManagerGlobal);
    return manager ? game::readPointer(manager + game::kSaveManagerRequestOffset) : 0;
}

std::optional<uint32_t> hash(int32_t slot, uintptr_t requestObject) {
    if (!requestObject || !save_load_rule::isPlayerSlot(slot)) return std::nullopt;
    uintptr_t address = slotAddress(requestObject, game::kSaveSlotBlocksOffset, slot);
    uint32_t state = fnv1a32::kOffsetBasis;
    for (size_t done = 0; done < game::kSaveSlotBytes; done += kHashChunkBytes, address += kHashChunkBytes) {
        std::array<uint8_t, kHashChunkBytes> chunk{};
        if (!game::readMemory(address, chunk)) return std::nullopt;
        state = fnv1a32::hash(chunk, state);
    }
    return state;
}

std::optional<save_index_rule::Summary> summary(int32_t slot) { return summaryAt(game::kSaveSlotBlocksOffset, slot); }

std::optional<save_index_rule::Summary> loaded(int32_t slot) { return summaryAt(game::kSaveWorkingSlotsOffset, slot); }

}  // namespace save_blocks
