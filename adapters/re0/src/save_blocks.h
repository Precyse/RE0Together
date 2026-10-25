#pragma once
#include <cstdint>
#include <optional>

#include "save_index_rule.h"

// The save manager's decrypted slot blocks (game.h kSaveSlotBlocksOffset): the bytes the last file read filled (the
// boot read of all 20 slots, then every load), SEH-safe reads, any thread. A slot is identified across machines by the
// FNV-1a 32 of its block, the same value the launcher's SaveIndex computes from the file.
namespace save_blocks {

// The request object of the save manager, 0 before it exists.
uintptr_t request();

// The hash of slot `slot`'s block in `request` (the load hook's `this`) or in the current request object.
std::optional<uint32_t> hash(int32_t slot, uintptr_t requestObject = request());

// What the load list shows of the slot's block.
std::optional<save_index_rule::Summary> summary(int32_t slot);

// What the working slot holds after a load (the bytes Continue reads its cleared and Wesker flags from).
std::optional<save_index_rule::Summary> loaded(int32_t slot);

}  // namespace save_blocks
