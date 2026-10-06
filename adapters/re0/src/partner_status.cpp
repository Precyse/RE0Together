#include "partner_status.h"

#include <cstdio>

namespace partner_status {

namespace {

constexpr const char kSeparator[] = " / ";
constexpr size_t kRoomTextCapacity = 16;

std::string roomText(const Input& input) {
    if (input.sameRoom) return "same room";
    if (input.room == kNoRoom) return "";
    char text[kRoomTextCapacity];
    std::snprintf(text, sizeof(text), "room 0x%02x", input.room);
    return text;
}

void append(std::string& line, const std::string& part) {
    if (part.empty()) return;
    if (!line.empty()) line += kSeparator;
    line += part;
}

}  // namespace

Condition condition(int hp, int maxHp) {
    const int percent = maxHp > 0 ? hp * 100 / maxHp : 0;
    if (percent > kFinePercent) return Condition::Fine;
    return percent > kCautionPercent ? Condition::Caution : Condition::Danger;
}

const char* conditionName(Condition condition) {
    switch (condition) {
        case Condition::Fine: return "Fine";
        case Condition::Caution: return "Caution";
        case Condition::Danger: break;
    }
    return "Danger";
}

std::string text(const Input& input) {
    if (input.hostLeft) return "Host left / not saved";
    std::string line;
    append(line, input.name.substr(0, kNameChars));
    if (input.hasState) {
        append(line, conditionName(condition(input.hp, input.maxHp)));
        append(line, roomText(input));
    }
    if (input.inMenu) append(line, "in menu");
    return line;
}

}  // namespace partner_status
