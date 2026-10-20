#pragma once
#include "save_gate_rule.h"

// The host's save gate in partner_mode=state (rule and reasons in save_gate_rule.h): a save waits until the partner stands
// in plain gameplay, so the save captures settled states.
namespace save_gate {

// Game thread, host with a partner: Plain when the save may be written now, else why it waits.
save_gate_rule::Verdict check();

}  // namespace save_gate
