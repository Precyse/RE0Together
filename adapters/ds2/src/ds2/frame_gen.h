#pragma once
// DS2-internal: the game's frame generation setting (HKCU registry value FrameGen of the Graphics key). The overlay draws on
// the presented buffers, which is verified with DLSS frame generation; FSR and XeSS frame generation replace the swap chain
// with their own present path, where drawing was never tried.
#include <cstdint>

namespace frame_gen {

enum class Mode { Off, Dlss, Fsr, Xess, Unknown };

// The setting's registry value (0 off, 1 DLSS, 2 FSR, 3 XeSS) as a mode; any other value is Unknown.
Mode fromValue(uint32_t value);

// Reads the setting now; Unknown when the value cannot be read.
Mode current();

// Whether the overlay may draw: only FSR and XeSS frame generation turn it off.
bool overlaySafe(Mode mode);

// Log name of the mode.
const char* name(Mode mode);

}  // namespace frame_gen
