#include "partner_status.h"

#include <cstdio>

namespace partner_status {

namespace {

constexpr const char kSeparator[] = " / ";
constexpr size_t kRoomTextCapacity = 16;

std::string roomText(const Input& input) {
    if (input.sameRoom || input.room == kNoRoom) return "";
    char text[kRoomTextCapacity];
    std::snprintf(text, sizeof(text), "Room 0x%02x", input.room);
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

Tag tag(const Input& input) {
    Tag out;
    out.name = input.name.substr(0, kNameChars);
    if (input.hasState) {
        out.hasCondition = true;
        out.condition = condition(input.hp, input.maxHp);
        append(out.detail, roomText(input));
    }
    if (input.inMenu) append(out.detail, "In menu");
    return out;
}

}  // namespace partner_status
