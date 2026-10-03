#pragma once
// DS2-internal: the hooks that let a guest follow the host's clock and weather (see ds2/world_env.cpp).
namespace world_env {

// Start-up: detours on the time and weather updates; they do nothing until game::followWorldEnv is called.
void installEarly();

}  // namespace world_env
