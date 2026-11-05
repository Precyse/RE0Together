#include "self_hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {

std::array<uint8_t, 32> compute() {
    std::array<uint8_t, 32> hash{};
    HMODULE self = nullptr;
    wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&compute), &self) ||
        !GetModuleFileNameW(self, path, MAX_PATH)) {
        return hash;
    }
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
               reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
               hash.data(), static_cast<ULONG>(hash.size()));
    return hash;
}

}  // namespace

namespace self_hash {

const std::array<uint8_t, 32>& adapterHash() {
    static const std::array<uint8_t, 32> hash = compute();
    return hash;
}

}  // namespace self_hash
