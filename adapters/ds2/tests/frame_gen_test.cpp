// frame_gen: the registry value mapping and which modes keep the overlay (no game).
#include <cstdio>
#include <cstring>

#include "../src/ds2/frame_gen.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace frame_gen;

    check(fromValue(0) == Mode::Off && fromValue(1) == Mode::Dlss && fromValue(2) == Mode::Fsr && fromValue(3) == Mode::Xess,
          "values 0 to 3 map to off, DLSS, FSR, XeSS");
    check(fromValue(4) == Mode::Unknown, "any other value is unknown");
    check(overlaySafe(Mode::Off) && overlaySafe(Mode::Dlss), "off and DLSS keep the overlay");
    check(!overlaySafe(Mode::Fsr) && !overlaySafe(Mode::Xess), "FSR and XeSS turn the overlay off");
    check(overlaySafe(Mode::Unknown), "an unreadable setting keeps the overlay");
    check(std::strcmp(name(Mode::Dlss), "DLSSG") == 0, "DLSS is named as the game logs it");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
