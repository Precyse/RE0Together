#pragma once

// Skips the load-in lines (load_flavor_rule): sGameGUI::showCommonMessage leaves them unshown, so the script's
// mes_common op finds no message busy on its next step and goes on, the way it does after the player closes one.
namespace load_flavor {

bool install();

}  // namespace load_flavor
