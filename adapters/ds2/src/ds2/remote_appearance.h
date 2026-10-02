#pragma once
// DS2-internal: the remote's costume. The body-variant loader (the object that loads the suit table into the model
// control component) is ticked by the engine only for the index-0 player, so the remote would render with only its
// backpack and boots. After Sam's tick the remote's loader is ticked too, asked once for Sam's variant.
namespace remote_appearance {

void installEarly();

// A new remote has been created: ask for Sam's variant again.
void onSpawned();

}  // namespace remote_appearance
