#pragma once
#include <atomic>
#include <cstdint>

#include "vehicle_seating_rules.h"

// The one thing the vehicle seating sites hand to each other (docs/SR2_NOTES.md, "Vehicle seating"): MC4 records the session slot of the
// remote whose car a checkpoint was saved in, SE2 restores into that car.
namespace vehicle_seating_state {

// The slot of the remote recorded, vehicle_seating_rules::kDefaultHolder (the default partner's car) when none.
inline std::atomic<int32_t> g_checkpointHolder{vehicle_seating_rules::kDefaultHolder};

inline void rememberCheckpointHolder(int32_t slot) { g_checkpointHolder.store(slot); }
inline int32_t checkpointHolder() { return g_checkpointHolder.load(); }

}  // namespace vehicle_seating_state
