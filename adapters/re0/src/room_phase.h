#pragma once
#include <array>
#include <cstdint>

// The room phase machine (sRoomControl +0xb8, current phase at +0x14): which screen the game is in. Pure data, no
// game access. Indices read from the manager's phase array in game (2026-09-29).
namespace room_phase {

enum Phase : int32_t {
    Init = 0,
    Main = 1,
    Message = 2,
    MessageImm = 3,
    DoorLoad = 4,
    SubScreen = 5,
    Save = 6,
    UpCut = 7,
    Option = 8,
    Change = 9,
    EventDemo = 10,
    Map = 11,
    Movie = 12,
    Event = 13,
    Dead = 14,
    Opening = 15,
    StaffRoll = 16,
    Ranking = 17,
    WeskerTitle = 18,
    WeskerRanking = 19,
    OmakeTitle = 20,
    OmakeResult = 21,
    PlayDemo = 22,
    Exit = 23,
};

constexpr int32_t kUnreadable = -1;

constexpr std::array<const char*, 24> kNames = {
    "Init",   "Main",     "Message",     "MessageImm",    "DoorLoad",   "SubScreen",   "Save",     "UpCut",
    "Option", "Change",   "EventDemo",   "Map",           "Movie",      "Event",       "Dead",     "Opening",
    "StaffRoll", "Ranking", "WeskerTitle", "WeskerRanking", "OmakeTitle", "OmakeResult", "PlayDemo", "Exit"};

constexpr const char* name(int32_t phase) {
    return phase >= 0 && phase < static_cast<int32_t>(kNames.size()) ? kNames[phase] : "?";
}

// Screens that hold the other player's world until this one leaves them: menus, reading, saving, the map and
// cutscenes (a player who skips a cutscene waits for the other to finish it).
constexpr bool pausesWorld(int32_t phase) {
    return phase == Message || phase == MessageImm || phase == SubScreen || phase == Save || phase == Option ||
           phase == Map || phase == EventDemo || phase == Movie;
}

// The player is in the game world (not the boot, title, game over, ending or extras screens).
constexpr bool isGameplay(int32_t phase) {
    return phase != kUnreadable && phase != Init && phase != Dead && phase != Opening && phase != StaffRoll &&
           phase != Ranking && phase < WeskerTitle;
}

}  // namespace room_phase
