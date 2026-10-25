#pragma once
#include <cstdint>
#include <string>

// The partner tag's content (pure text, no game, unit tested): the name, the condition, and the detail line while the
// partner is in another room or in a menu. Nothing to show without a partner.
namespace partner_status {

enum class Condition { Fine, Caution, Danger };

constexpr size_t kNameChars = 16;
constexpr int kBaseMaxHp = 150;  // lowest full health assumed (Rebecca 150, Billy 161 seen), raised by what is reported
constexpr int kFinePercent = 50;     // above this share of full health: Fine
constexpr int kCautionPercent = 25;  // above this: Caution, else Danger
constexpr uint16_t kNoRoom = 0xffff;

struct Input {
    std::string name;
    bool hasState = false;   // a PLAYER_STATE arrived
    int hp = 0;
    int maxHp = kBaseMaxHp;  // the highest hp seen from this player
    bool sameRoom = false;
    uint16_t room = kNoRoom;  // the partner's loaded scene id
    bool inMenu = false;
};

Condition condition(int hp, int maxHp);

// What the HUD's partner tag shows. An empty name means nothing is shown.
struct Tag {
    std::string name;
    bool hasCondition = false;  // false until a PLAYER_STATE arrived
    Condition condition = Condition::Fine;
    std::string detail;         // "Hall", "In menu" or both ("Room 0xNN" for a room with no known name); empty while together

    bool operator==(const Tag&) const = default;
};

Tag tag(const Input& input);

// A PLAYER_STATE older than this is not the partner's condition any more (they send one 50 times a second).
constexpr uint32_t kStateFreshMs = 2000;

// The tag shows only while both games are in play and the partner's own state is fresh: not after a game over, a reload
// or the partner closing its game while its launcher stays in the session.
constexpr bool shows(bool localInPlay, bool partnerInPlay, bool hasState, uint32_t stateAgeMs) {
    return localInPlay && partnerInPlay && hasState && stateAgeMs < kStateFreshMs;
}

}  // namespace partner_status
