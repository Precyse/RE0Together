// firstMismatch: the compare behind the start-up code check (no game).
#include <cstdio>

#include "../src/byte_pattern.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    const uint8_t expected[] = {0x89, 0x2D, 0x34, 0xC4};
    uint8_t actual[] = {0x89, 0x2D, 0x34, 0xC4};
    check(!firstMismatch(actual, expected, sizeof(expected)), "equal bytes match");
    actual[3] = 0xC5;
    check(firstMismatch(actual, expected, sizeof(expected)) == size_t{3}, "last byte differs");
    actual[0] = 0xE9;
    check(firstMismatch(actual, expected, sizeof(expected)) == size_t{0}, "a jump written over the first byte is reported at 0");
    check(!firstMismatch(actual, expected, 0), "zero bytes compare equal");
    return g_failures == 0 ? 0 : 1;
}
