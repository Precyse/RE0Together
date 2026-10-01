#pragma once

// The controller's party command buttons. The game reads controllers through XInputGetState (import slot 0xcb13b0);
// in every controller type (options: Type A/B/C) Y switches character and LT is Solo/Team. While a peer is connected
// the game never sees those two (its own switch would take this machine's camera, its partner command would act on
// the other player's character); command_input turns them into the adapter's commands instead.
namespace pad_commands {

struct Buttons {
    bool change;
    bool trace;
};

// Points the game's XInputGetState import at the filter. False when the slot is empty.
bool install();

void uninstall();

// While set, the game reads the command buttons as released.
void setHidden(bool hidden);

// The command buttons on any controller, as last read by the game (any thread).
Buttons pressed();

}  // namespace pad_commands
