#pragma once
// ANIM_EVENT: a one-shot animation edge (a pulse, see pulse_detector.h) sent reliably next to the unreliable
// ANIM_STATE, because losing either of its two edges loses the whole motion. The payload is an anim_wire report. The
// receiver does not store the pulse as the variable's value (nothing would ever turn it off again): it holds it for a
// short time and the remote's animation writes it on top of the partner's continuous state for that long.
#include <cstdint>
#include <vector>

#include "anim_change.h"
#include "net_client.h"
#include "time_us.h"

namespace anim_event {

constexpr TimeUs kHoldUs = 100'000;  // several graph evaluations

// Net thread: the partner's events.
void onFrame(const GameFrame& frame, TimeUs now = nowUs());

// The pulses to write into the remote body of the peer in `slot` right now, any thread. Expired ones are dropped.
std::vector<remote_animation::Change> active(uint8_t slot, TimeUs now = nowUs());

}  // namespace anim_event
