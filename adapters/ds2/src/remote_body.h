#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

#include "game.h"

// The partner's body: a second player entity the engine creates the way it creates a network player, with its own
// camera, a body of Sam's costume, and the engine's own state machine, so it walks, rides and renders like Sam. It
// stands where the partner's smoothed pose says, and rides the vehicle the partner drives (vehicle_sync) through the
// game's own get-in and get-off. One partner; the first peer that reports is the one it follows. Poses come from the
// render thread; the engine calls run on the game's per-frame update.
namespace remote_body {

void setEnabled(bool enabled);

// Start-up, before the game builds its player tables (needs setEnabled(true) first): installs the engine hooks.
void installEarly();

// Render thread, once per frame per visible peer: where that peer's body should be and how fast it is moving.
void setTarget(uint8_t slot, const game::Pose& pose, const world_to_screen::Vec3& velocity);

// The baggage owner key of the body (its network id), and the slot of the peer it stands for; empty / 0xFF until it
// is live. Any thread.
std::optional<uint64_t> ownerKey();
uint8_t slot();

// The body's backpack owner and its marker come up some time after the body itself: until then a piece created for
// it has no slot to land in and is dropped on the ground.
constexpr std::chrono::seconds kBackpackReadyAfter{20};

// How long the body has been live (empty until it is), and where the partner's pose puts it. Any thread.
std::optional<std::chrono::steady_clock::duration> liveFor();
std::optional<world_to_screen::Vec3> position();

}  // namespace remote_body
