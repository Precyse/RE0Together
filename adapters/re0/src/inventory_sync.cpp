#include "inventory_sync.h"

#include <array>
#include <cstring>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "protocol.h"
#include "settled_copy.h"

namespace {

using character_owner::Character;
using Block = SettledCopy<game::kInventoryBlockSize>;

constexpr uint64_t kResendMs = 5000;
constexpr size_t kCharacterCount = 2;
constexpr size_t kPayloadSize = 1 + game::kInventoryBlockSize;

NetClient* g_net = nullptr;
std::array<Block, kCharacterCount> g_blocks;  // game thread
uint32_t g_frame = 0;
uint32_t g_lastLocalChangeFrame = 0;
bool g_localChanged = false;

std::mutex g_mutex;
std::array<std::optional<Block::Bytes>, kCharacterCount> g_pending;  // latest received block per character

uintptr_t blockAddress(Character character) {
    const uintptr_t items = game::readPointer(game::kItemGlobal);
    if (!items) return 0;
    return items + (character == Character::Billy ? game::kItemBillyBlockOffset : game::kItemRebeccaBlockOffset);
}

void send(Character character, Block& block) {
    std::array<uint8_t, kPayloadSize> payload{};
    payload[0] = static_cast<uint8_t>(character);
    std::memcpy(payload.data() + 1, block.current().data(), game::kInventoryBlockSize);
    if (!g_net->send(proto::kMsgInventory, true, proto::kSlotAll, payload)) return;
    block.markSent(monotonicMs());
    debug_stats::count(debug_stats::Counter::InventorySent);
}

void syncLocal(Character character) {
    Block& block = g_blocks[static_cast<size_t>(character)];
    Block::Bytes bytes;
    if (!game::readMemory(blockAddress(character), bytes)) return;
    if (block.observe(bytes, g_frame)) {
        g_lastLocalChangeFrame = g_frame;
        g_localChanged = true;
    }
    if (block.due(g_frame, monotonicMs(), kResendMs)) send(character, block);
}

void applyRemote(Character character) {
    std::optional<Block::Bytes> pending;
    {
        std::lock_guard lock(g_mutex);
        pending.swap(g_pending[static_cast<size_t>(character)]);
    }
    if (pending && game::writeMemory(blockAddress(character), *pending)) {
        debug_stats::count(debug_stats::Counter::InventoryApplied);
    }
}

void onTick() {
    ++g_frame;
    for (const Character character : {Character::Billy, Character::Rebecca}) {
        if (character_owner::isLocalOwned(character)) {
            syncLocal(character);
            continue;
        }
        g_blocks[static_cast<size_t>(character)].reset();
        if (character_owner::isRemoteOwned(character)) applyRemote(character);
    }
}

}  // namespace

namespace inventory_sync {

void onFrame(const GameFrame& frame) {
    if (frame.payload.size() != kPayloadSize || frame.payload[0] >= kCharacterCount) return;
    Block::Bytes bytes;
    std::memcpy(bytes.data(), frame.payload.data() + 1, bytes.size());
    std::lock_guard lock(g_mutex);
    g_pending[frame.payload[0]] = bytes;
}

uint32_t framesSinceLocalChange() { return g_localChanged ? g_frame - g_lastLocalChangeFrame : UINT32_MAX; }

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("inventory_sync", onTick);
}

}  // namespace inventory_sync
