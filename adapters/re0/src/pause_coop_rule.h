#pragma once
#include <cstdint>

// Pure rules of the pause menu's Co-op row (pause_coop.cpp, unit tested; docs/RE0_NOTES.md "Pause menu: the Co-op row"):
// three rows instead of the game's two, laid out 40 apart from the first, and what one frame of input does with them.
namespace pause_coop_rule {

enum Row : int { kBack, kCoop, kQuit, kRowCount };

constexpr float kFirstRowY = 400.0f;  // i_exit and the arrow as the layout and uGUIPause's open (0x5ccb40) place them
constexpr float kRowPitch = 40.0f;    // 0xcbdf74, the step the game moves its arrow by
constexpr float kHelpY = 540.0f;      // the help line, one row lower than the layout's 500

constexpr float rowY(int row) { return kFirstRowY + kRowPitch * static_cast<float>(row); }

struct Input {
    bool cancel = false;
    bool decide = false;
    bool up = false;
    bool down = false;
    int clickedRow = -1;  // the row a mouse click landed on, -1 none
};

enum class Action : uint8_t { None, Move, Back, Coop, Quit, Cancel };

struct Step {
    Action action = Action::None;
    int row = kBack;
};

// What one frame of input does with the cursor on `row`: cancel wins, then a move (wrapping), then a click (on another
// row it moves there, on the cursor row it decides), then decide.
constexpr Step step(int row, const Input& in) {
    if (in.cancel) return {Action::Cancel, row};
    if (in.up || in.down) {
        const int next = (row + (in.down ? 1 : kRowCount - 1)) % kRowCount;
        return {Action::Move, next};
    }
    const bool clickedHere = in.clickedRow == row;
    if (in.clickedRow >= 0 && !clickedHere) return {Action::Move, in.clickedRow};
    if (!in.decide && !clickedHere) return {Action::None, row};
    switch (row) {
        case kBack: return {Action::Back, row};
        case kCoop: return {Action::Coop, row};
        default: return {Action::Quit, row};
    }
}

}  // namespace pause_coop_rule
