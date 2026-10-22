// msvc_rtti: a class's primary vtable found by name in this executable's own RTTI, and the second lookup of a name kept (no game).
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "../src/msvc_rtti.h"

class RttiProbe {
public:
    virtual ~RttiProbe() = default;
    virtual int value() const { return 1; }
};

namespace {

constexpr double kKeptLookupMaxMicros = 100.0;  // a kept answer is a map lookup, far below one scan

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

double microsFor(const char* name, uintptr_t& vtable) {
    const auto start = std::chrono::steady_clock::now();
    vtable = msvc_rtti::vtableOf(name);
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main() {
    RttiProbe* probe = new RttiProbe;
    const uintptr_t actual = *reinterpret_cast<const uintptr_t*>(probe);
    uintptr_t first = 0, second = 0;
    const double scanMicros = microsFor("RttiProbe", first);
    const double keptMicros = microsFor("RttiProbe", second);
    check(first == actual, "the vtable found by name is the object's vtable");
    check(second == first, "the second lookup gives the same vtable");
    check(keptMicros < kKeptLookupMaxMicros, "the second lookup does not scan again");
    uintptr_t missing = 1;
    microsFor("NoSuchClassInThisImage", missing);
    check(missing == 0, "an unknown class gives 0");
    std::printf("scan %.0f us, kept lookup %.1f us\n", scanMicros, keptMicros);
    delete probe;
    return g_failures == 0 ? 0 : 1;
}
