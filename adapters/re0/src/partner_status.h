#pragma once
#include <cstdint>
#include <string>

// The partner status line (pure text, no game, unit tested): name, condition, same room or its room, and "in menu".
// "Host left / not saved" replaces it on a guest whose host went away.
namespace partner_status {

enum class Condition { Fine, Caution, Danger };

constexpr size_t kNameChars = 16;
constexpr int kBaseMaxHp = 150;  // lowest full health assumed (Rebecca 150, Billy 161 seen), raised by what is reported
constexpr int kFinePercent = 50;     // above this share of full health: Fine
constexpr int kCautionPercent = 25;  // above this: Caution, else Danger
constexpr uint16_t kNoRoom = 0xffff;

struct Input {
    std::string name;
    bool hostLeft = false;
    bool hasState = false;   // a PLAYER_STATE arrived
    int hp = 0;
    int maxHp = kBaseMaxHp;  // the highest hp seen from this player
    bool sameRoom = false;
    uint16_t room = kNoRoom;  // the partner's loaded scene id
    bool inMenu = false;
};

Condition condition(int hp, int maxHp);

const char* conditionName(Condition condition);

// The status line; empty when there is nothing to show.
std::string text(const Input& input);

}  // namespace partner_status
