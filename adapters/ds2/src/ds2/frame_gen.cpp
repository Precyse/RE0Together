#include "ds2/frame_gen.h"

#include <windows.h>

namespace {

constexpr const wchar_t* kGraphicsKey = L"Software\\KOJIMA PRODUCTIONS\\DEATH STRANDING 2 - ON THE BEACH\\Graphics";
constexpr const wchar_t* kFrameGenValue = L"FrameGen";

}  // namespace

namespace frame_gen {

Mode fromValue(uint32_t value) {
    switch (value) {
        case 0: return Mode::Off;
        case 1: return Mode::Dlss;
        case 2: return Mode::Fsr;
        case 3: return Mode::Xess;
        default: return Mode::Unknown;
    }
}

Mode current() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kGraphicsKey, kFrameGenValue, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
        ERROR_SUCCESS) {
        return Mode::Unknown;
    }
    return fromValue(value);
}

bool overlaySafe(Mode mode) { return mode != Mode::Fsr && mode != Mode::Xess; }

const char* name(Mode mode) {
    switch (mode) {
        case Mode::Off: return "off";
        case Mode::Dlss: return "DLSSG";
        case Mode::Fsr: return "FSR";
        case Mode::Xess: return "XeSS";
        case Mode::Unknown: return "unknown";
    }
    return "unknown";
}

}  // namespace frame_gen
