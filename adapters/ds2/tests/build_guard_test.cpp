// build_guard: the identity read from a PE header copy and the compare against the supported build (no game).
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "../src/build_guard.h"

namespace {

constexpr uint32_t kHeaderBytes = 0x400;
constexpr LONG kNtOffset = 0x80;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void makeHeader(uint8_t* page, uint32_t stamp, uint32_t size) {
    std::memset(page, 0, kHeaderBytes);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(page);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = kNtOffset;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(page + kNtOffset);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.TimeDateStamp = stamp;
    nt->OptionalHeader.SizeOfImage = size;
}

}  // namespace

int main() {
    uint8_t page[kHeaderBytes];
    makeHeader(page, build_guard::kSupported.timeDateStamp, build_guard::kSupported.sizeOfImage);
    check(build_guard::supported(build_guard::identityOf(page, kHeaderBytes)), "the supported build is accepted");

    makeHeader(page, build_guard::kSupported.timeDateStamp + 1, build_guard::kSupported.sizeOfImage);
    check(!build_guard::supported(build_guard::identityOf(page, kHeaderBytes)), "another stamp is refused");

    makeHeader(page, build_guard::kSupported.timeDateStamp, build_guard::kSupported.sizeOfImage + 1);
    check(!build_guard::supported(build_guard::identityOf(page, kHeaderBytes)), "another image size is refused");

    check(!build_guard::supported(build_guard::identityOf(page, kNtOffset)), "a truncated header is refused");

    std::memset(page, 0, kHeaderBytes);
    check(!build_guard::supported(build_guard::identityOf(page, kHeaderBytes)), "a non-PE page is refused");
    return g_failures ? 1 : 0;
}
