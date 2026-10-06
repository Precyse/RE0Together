#include "inventory_sync.h"

#include <array>
#include <cstring>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "equip_refresh.h"
#include "equip_rule.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "join_sync.h"
#include "log.h"
#include "protocol.h"
#include "settled_copy.h"

namespace {

using character_owner::Character;
using character_owner::kCharacterCount;
using character_owner::kCharacters;
using Block = SettledCopy<game::kInventoryBlockSize>;

constexpr uint64_t kResendMs = 5000;
constexpr size_t kHeaderSize = 2;
constexpr size_t kPayloadSize = kHeaderSize + game::kInventoryBlockSize;

enum class Origin : uint8_t { Owner = 0, MenuExchange = 1 };

NetClient* g_net = nullptr;
std::array<Block, kCharacterCount> g_blocks;  // game thread
std::array<bool, kCharacterCount> g_equipPending{};  // game thread: a received block changed the equipped slot
uint32_t g_frame = 0;
uint32_t g_lastLocalChangeFrame = 0;
bool g_localChanged = false;
std::array<Block::Bytes, kCharacterCount> g_menuSnapshot{};  // game thread: blocks when the local menu opened
bool g_menuWatch = false;                                     // game thread: a menu opened and has not been checked

std::mutex g_mutex;
std::array<std::optional<Block::Bytes>, kCharacterCount> g_pending;  // latest received block per character

uintptr_t blockAddress(Character character) {
    const uintptr_t items = game::readPointer(game::kItemGlobal);
    if (!items) return 0;
    return items + (character == Character::Billy ? game::kItemBillyBlockOffset : game::kItemRebeccaBlockOffset);
}

bool sendBytes(Character character, Origin origin, const Block::Bytes& bytes) {
    std::array<uint8_t, kPayloadSize> payload{};
    payload[0] = static_cast<uint8_t>(character);
    payload[1] = static_cast<uint8_t>(origin);
    std::memcpy(payload.data() + kHeaderSize, bytes.data(), game::kInventoryBlockSize);
    if (!g_net->send(proto::kMsgInventory, true, proto::kSlotAll, payload)) return false;
    debug_stats::count(debug_stats::Counter::InventorySent);
    return true;
}

void send(Character character, Block& block) {
    if (sendBytes(character, Origin::Owner, block.current())) block.markSent(monotonicMs());
}

// First tick after the local menu closed: the other player's character changed only through an exchange.
void sendMenuExchanges() {
    g_menuWatch = false;
    for (const Character character : kCharacters) {
        Block::Bytes bytes;
        const size_t index = static_cast<size_t>(character);
        if (!character_owner::isRemoteOwned(character) || !game::readMemory(blockAddress(character), bytes) ||
            bytes == g_menuSnapshot[index]) {
            continue;
        }
        if (sendBytes(character, Origin::MenuExchange, bytes)) debug_stats::count(debug_stats::Counter::InventoryExchanges);
    }
}

void syncLocal(Character character) {
    Block& block = g_blocks[static_cast<size_t>(character)];
    Block::Bytes bytes;
    if (!game::readMemory(blockAddress(character), bytes)) return;
    if (block.observe(bytes, g_frame)) {
        g_lastLocalChangeFrame = g_frame;
        g_localChanged = true;
    }
    if (join_sync::caughtUp() && block.due(g_frame, monotonicMs(), kResendMs)) send(character, block);
}

void applyRemote(Character character) {
    std::optional<Block::Bytes> pending;
    {
        std::lock_guard lock(g_mutex);
        pending.swap(g_pending[static_cast<size_t>(character)]);
    }
    Block::Bytes before{};
    if (!pending || !game::readMemory(blockAddress(character), before) ||
        !game::writeMemory(blockAddress(character), *pending)) {
        return;
    }
    debug_stats::count(debug_stats::Counter::InventoryApplied);
    if (character_owner::isRemoteOwned(character) && equip_rule::needsRefresh(before.data(), pending->data())) {
        g_equipPending[static_cast<size_t>(character)] = true;
    }
    // An exchange applied to our own character is already the state the peer has.
    if (character_owner::isLocalOwned(character)) g_blocks[static_cast<size_t>(character)].adopt(*pending, monotonicMs());
}

// The game's equip step for a remote-owned character whose equipped slot changed: it needs plain gameplay (no menu,
// no door) and the character in the loaded room, so a change that arrives earlier waits.
void refreshEquipped(Character character) {
    bool& pending = g_equipPending[static_cast<size_t>(character)];
    if (!pending) return;
    if (!character_owner::isRemoteOwned(character)) {
        pending = false;
        return;
    }
    const uintptr_t player = character_owner::find(character);
    if (!player || !game_state::playing() || game_state::menuOpen() || !game_state::inCurrentRoom(player)) return;
    pending = false;
    if (equip_refresh::run(player)) debug_stats::count(debug_stats::Counter::EquipRefreshes);
    else logger::write("inventory_sync: the equip step faulted for %s", character_owner::name(character));
}

void onTick() {
    ++g_frame;
    if (g_menuWatch && !game_state::menuOpen()) sendMenuExchanges();
    for (const Character character : kCharacters) {
        if (character_owner::isLocalOwned(character)) {
            applyRemote(character);
            syncLocal(character);
            refreshEquipped(character);
            continue;
        }
        g_blocks[static_cast<size_t>(character)].reset();
        if (character_owner::isRemoteOwned(character)) applyRemote(character);
        refreshEquipped(character);
    }
}

}  // namespace

namespace inventory_sync {

void onFrame(const GameFrame& frame) {
    if (frame.payload.size() != kPayloadSize || frame.payload[0] >= kCharacterCount) return;
    const auto character = static_cast<Character>(frame.payload[0]);
    const auto origin = static_cast<Origin>(frame.payload[1]);
    // Our own character only takes the peer's menu exchanges; everything else about it is ours to send.
    if (character_owner::isLocalOwned(character) && origin != Origin::MenuExchange) return;
    Block::Bytes bytes;
    std::memcpy(bytes.data(), frame.payload.data() + kHeaderSize, bytes.size());
    std::lock_guard lock(g_mutex);
    g_pending[frame.payload[0]] = bytes;
}

void applySnapshot(uint8_t characterId, const InventoryBlock& block) {
    if (characterId >= kCharacterCount) return;
    const auto character = static_cast<Character>(characterId);
    Block::Bytes bytes;
    std::memcpy(bytes.data(), block, bytes.size());
    if (!game::writeMemory(blockAddress(character), bytes)) return;
    if (character_owner::isLocalOwned(character)) g_blocks[characterId].adopt(bytes, monotonicMs());
}

bool readBlock(uint8_t characterId, InventoryBlock& block) {
    if (characterId >= kCharacterCount) return false;
    Block::Bytes bytes;
    if (!game::readMemory(blockAddress(static_cast<Character>(characterId)), bytes)) return false;
    std::memcpy(block, bytes.data(), bytes.size());
    return true;
}

void onMenuOpen() {
    for (const Character character : kCharacters) {
        game::readMemory(blockAddress(character), g_menuSnapshot[static_cast<size_t>(character)]);
    }
    g_menuWatch = true;
}

uint32_t framesSinceLocalChange() { return g_localChanged ? g_frame - g_lastLocalChangeFrame : UINT32_MAX; }

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("inventory_sync", onTick);
}

}  // namespace inventory_sync
