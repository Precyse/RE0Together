#pragma once

// Logs every room phase change (cutscene, game over, map, save) with its name and shows the phase on the F8 panel.
namespace phase_watch {

// Net thread, every tick: the game tick stops during cutscenes and menus, the net thread does not.
void onNetTick();

}  // namespace phase_watch
