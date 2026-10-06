#pragma once
// DS2-internal: how far a partner can be from this machine's player before it is beyond the world the engine has loaded.
namespace far_partner {

// The host simulates the world within about 800 m of its own player (the entity tables' far radius, partner_focus.cpp); a
// partner beyond this leaves the loaded world, where enemies stay asleep and nothing is streamed in.
constexpr double kBeyondLoadedWorldMetres = 700.0;

}  // namespace far_partner
