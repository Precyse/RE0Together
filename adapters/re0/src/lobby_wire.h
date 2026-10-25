#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "protocol.h"

// Wire of the in-game lobby (docs/RE0_NOTES.md "Menus: the native Co-op page", docs/CONTRACT.md): what the launcher
// tells its adapter about the lobby (LOBBY_STATE 0x0062, text), the option a player changed on the page that the
// adapter tells its launcher (OPTION_SET 0x0061), each game's place at the title (LOBBY_PRESENCE 0x0118) and the host's
// start (SESSION_LOAD 0x0117). Both game messages are outside the state-mode block (0x0140..0x017f): they run in every
// partner mode. Pure, no game access, unit tested.
namespace lobby_wire {

constexpr uint16_t kMsgSessionLoad = proto::kFirstGameType + 0x17;    // 0x0117, host to all, reliable
constexpr uint16_t kMsgLobbyPresence = proto::kFirstGameType + 0x18;  // 0x0118, to all, reliable

constexpr uint8_t kNoSlot = 0xff;  // a member the session has no slot for yet
constexpr char kLineSeparator = '\n';
constexpr char kFieldSeparator = '\t';

// ---------------------------------------------------------------- LOBBY_STATE (launcher to adapter)

struct Member {
    uint8_t slot = kNoSlot;
    bool host = false;
    bool local = false;
    std::string character;  // the character option's value (rebecca), empty when unknown
    std::string characterLabel;
    std::string outfitLabel;
    std::string name;
};

// A value of an option and its label.
struct Value {
    std::string value;
    std::string label;
};

struct KeyedValue {
    std::string key;  // the option key, or for an outfit the character value
    Value value;
};

struct LobbyState {
    std::vector<Member> members;       // host first
    std::vector<KeyedValue> options;   // the host options' current values (partner_mode, world_pause, character)
    std::vector<KeyedValue> values;    // every value those options offer
    std::vector<KeyedValue> outfits;   // per character the outfits offered in this game folder, in profile order
    std::vector<KeyedValue> own;       // this player's own option values (label empty)

    // The current value of a host option, or null.
    const Value* option(std::string_view key) const { return find(options, key); }
    // The value of one of this player's own options, or null.
    const Value* ownValue(std::string_view key) const { return find(own, key); }
    // What a host option or a character's outfits offer, in order.
    std::vector<Value> valuesOf(std::string_view key) const { return all(values, key); }
    std::vector<Value> outfitsOf(std::string_view character) const { return all(outfits, character); }

private:
    static const Value* find(const std::vector<KeyedValue>& list, std::string_view key) {
        for (const KeyedValue& entry : list) {
            if (entry.key == key) return &entry.value;
        }
        return nullptr;
    }
    static std::vector<Value> all(const std::vector<KeyedValue>& list, std::string_view key) {
        std::vector<Value> out;
        for (const KeyedValue& entry : list) {
            if (entry.key == key) out.push_back(entry.value);
        }
        return out;
    }
};

namespace detail {

inline std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t at = text.find(separator); at != std::string_view::npos; at = text.find(separator, start)) {
        parts.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    parts.push_back(text.substr(start));
    return parts;
}

inline std::optional<uint8_t> slotOf(std::string_view text) {
    if (text.empty() || text.size() > 3) return std::nullopt;
    unsigned value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return value <= kNoSlot ? std::optional<uint8_t>(static_cast<uint8_t>(value)) : std::nullopt;
}

constexpr size_t kMemberFields = 8;
constexpr size_t kKeyedFields = 4;
constexpr size_t kOwnFields = 3;

}  // namespace detail

// The lines this adapter knows (member, option, value, outfit, own); a line with another tag or the wrong field count is
// skipped, so a newer launcher's extra lines do no harm.
inline LobbyState parseLobbyState(std::string_view text) {
    LobbyState state;
    for (const std::string_view line : detail::split(text, kLineSeparator)) {
        const std::vector<std::string_view> f = detail::split(line, kFieldSeparator);
        const std::string_view tag = f[0];
        if (tag == "member" && f.size() == detail::kMemberFields) {
            const auto slot = detail::slotOf(f[1]);
            if (!slot) continue;
            state.members.push_back({*slot, f[2] == "1", f[3] == "1", std::string(f[4]), std::string(f[5]), std::string(f[6]),
                                     std::string(f[7])});
        } else if ((tag == "option" || tag == "value" || tag == "outfit") && f.size() == detail::kKeyedFields) {
            KeyedValue entry{std::string(f[1]), {std::string(f[2]), std::string(f[3])}};
            (tag == "option" ? state.options : tag == "value" ? state.values : state.outfits).push_back(std::move(entry));
        } else if (tag == "own" && f.size() == detail::kOwnFields) {
            state.own.push_back({std::string(f[1]), {std::string(f[2]), {}}});
        }
    }
    return state;
}

// ---------------------------------------------------------------- OPTION_SET (adapter to launcher)

constexpr size_t kMaxKeyChars = 32;
constexpr size_t kMaxValueChars = 32;

constexpr bool keyChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
constexpr bool valueChar(char c) { return c >= ' ' && c <= '~' && c != ';' && c != '='; }

// The payload `key=value`, or nothing when the key or value breaks the wire's rules.
inline std::optional<std::string> encodeOptionSet(std::string_view key, std::string_view value) {
    if (key.empty() || key.size() > kMaxKeyChars || value.size() > kMaxValueChars) return std::nullopt;
    for (const char c : key) {
        if (!keyChar(c)) return std::nullopt;
    }
    for (const char c : value) {
        if (!valueChar(c)) return std::nullopt;
    }
    return std::string(key) + "=" + std::string(value);
}

// ---------------------------------------------------------------- LOBBY_PRESENCE (each game to all)

// Where a game is: in the boot screens or the title's menus, on the Co-op page, loading the session's save, or in game.
enum class Place : uint8_t { Title, Page, Loading, InGame };
constexpr uint8_t kLastPlace = static_cast<uint8_t>(Place::InGame);

constexpr uint8_t kHasPick = 1;       // host: pickSlot / pickHash name the save the session will play
constexpr uint8_t kSaveChecked = 2;   // guest: it has compared its copy with the host's pick
constexpr uint8_t kSaveMatches = 4;   // guest: and its copy of that slot is the same bytes
constexpr uint8_t kKnownFlags = kHasPick | kSaveChecked | kSaveMatches;
constexpr uint8_t kPlayerSlots = 20;  // save_load_rule::kPlayerSlots (checked where both are visible)

struct Presence {
    uint8_t place;
    uint8_t flags;
    uint8_t pickSlot;  // kNoSlot without kHasPick
    uint8_t reserved;
    uint32_t pickHash;
};
static_assert(sizeof(Presence) == 8);

inline std::optional<Presence> decodePresence(std::span<const uint8_t> bytes) {
    Presence presence{};
    if (bytes.size() != sizeof(presence)) return std::nullopt;
    std::memcpy(&presence, bytes.data(), sizeof(presence));
    const bool hasPick = (presence.flags & kHasPick) != 0;
    if (presence.place > kLastPlace || (presence.flags & ~kKnownFlags) != 0 || presence.reserved != 0 ||
        (hasPick ? presence.pickSlot >= kPlayerSlots : presence.pickSlot != kNoSlot)) {
        return std::nullopt;
    }
    return presence;
}

// ---------------------------------------------------------------- SESSION_LOAD (host to all)

// The host loaded `slot` from its title and started it: every guest at its title loads the same slot when its copy
// hashes the same. `serial` numbers the host's starts (never 0), so a resend of an old start is not taken twice.
struct SessionLoad {
    int32_t slot;
    uint32_t slotHash;  // FNV-1a 32 of the slot's block (save_blocks::hash)
    uint32_t serial;
};
static_assert(sizeof(SessionLoad) == 12);

inline std::optional<SessionLoad> decodeSessionLoad(std::span<const uint8_t> bytes) {
    SessionLoad load{};
    if (bytes.size() != sizeof(load)) return std::nullopt;
    std::memcpy(&load, bytes.data(), sizeof(load));
    if (load.slot < 0 || load.slot >= kPlayerSlots || load.serial == 0) return std::nullopt;
    return load;
}

}  // namespace lobby_wire
