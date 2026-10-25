#pragma once
#include <cstdint>

// The lines the game shows over black as a save loads in ("Are the faint sounds of footsteps those of survivors?"):
// commonmsg 0x58 to 0x5f, shown by the event script's mes_common op (docs/RE0_NOTES.md "Load-in lines").
namespace load_flavor_rule {

constexpr uint32_t kFirstLine = 0x58;
constexpr uint32_t kLastLine = 0x5f;

constexpr bool skips(uint32_t commonMessageId) { return commonMessageId >= kFirstLine && commonMessageId <= kLastLine; }

}  // namespace load_flavor_rule
