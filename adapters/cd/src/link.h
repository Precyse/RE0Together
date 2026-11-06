#pragma once
// The launcher link and the session-level CD messages (CD_HELLO, CD_WORLD, CD_MARKER, CD_COUNTERS). Runs on the main
// tick: polls the launcher client, sends this machine's state, keeps what each peer reported and hands the CHAR_*
// frames of peers whose adapter matches ours to presence_recv.
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "game_state_rule.h"
#include "wire_cd.h"

namespace link {

// Starts the launcher client (its own thread). `siteSetMask` and `catalogueOk` go into CD_HELLO.
void start(uint16_t port, uint32_t siteSetMask, bool catalogueOk);

// Main tick only.
void tick(uint64_t nowMs);

// A time marker for lining up both machines' logs and captures (dev command `marker`).
void sendMarker(uint64_t nowMs);

// To every peer. False while not linked.
bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload);

// The launcher link is up (WELCOME received).
bool isLinked();

// Each peer's link state for GAME_STATE: live, stale (its CD_WORLD stopped) or mismatch (its adapter is not ours).
const std::vector<game_state_rule::Peer>& peerLinks();

// A peer joined (or rejoined) in this tick: modules announce their state again.
bool peerJoinedThisTick();

// This machine is the session's host.
bool isHost();

// The host's slot, when this machine is a linked guest.
std::optional<uint8_t> hostSlot();

// The slots of the peers the launcher reports.
std::vector<uint8_t> peerSlots();

// A peer's last CD_WORLD, if one arrived in the last 6 s.
std::optional<wire_cd::World> peerWorld(uint8_t slot);

}  // namespace link
