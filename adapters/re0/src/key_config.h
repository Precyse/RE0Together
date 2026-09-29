#pragma once
#include <string_view>

// The game's keyboard bindings for the two party commands, read from its config.ini.
namespace key_config {

struct CommandKeys {
    int change;  // KC_change: switch character (virtual-key code)
    int trace;   // KC_trace: partner stay/follow (virtual-key code)
};

// Virtual-key code of a "KB_<KEY>" name (A..Z, 0..9, SPACE, SHIFT, TAB, UP, DOWN, LEFT, RIGHT), or 0 when unknown.
int virtualKeyOf(std::string_view name);

// Parses the ini text; a missing or unknown binding keeps its default (V and E).
CommandKeys parse(std::string_view iniText);

// Reads %LOCALAPPDATA%\CAPCOM\RESIDENT EVIL 0 HD REMASTER\config.ini; the defaults when it cannot be read.
CommandKeys load();

}  // namespace key_config
