#pragma once
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

#include "../src/code_patch.h"
#include "../src/partner_sites.h"

// Lookups and checks the site table tests share: a row or patch by id or address, the size of a row, and where a row's `call rel32` goes.

constexpr uintptr_t kCallRel32Bytes = 5;  // the opcode and the displacement

// The number of bytes a row checks (any site table row with an `expected` hex text).
template <typename Row>
size_t sizeOf(const Row& row) {
    return partner_sites::parseHex(row.expected).size();
}

// True when no id and no address occurs twice in `sites`.
inline bool idsAndAddressesAreUnique(const std::vector<partner_sites::Site>& sites) {
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    for (const partner_sites::Site& site : sites) {
        if (!ids.insert(site.id).second || !addresses.insert(site.address).second) return false;
    }
    return true;
}

// The row of `sites` with this id, nullptr for none.
inline const partner_sites::Site* siteNamed(const std::vector<partner_sites::Site>& sites, std::string_view id) {
    for (const partner_sites::Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

// The patch at `address`, nullptr for none.
inline const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
}

// The 32-bit little endian value at `bytes[at]`.
inline uint32_t dwordOf(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}

// Where the `call rel32` at the start of a site goes.
inline uintptr_t callTarget(const partner_sites::Site& site) {
    return site.address + kCallRel32Bytes + dwordOf(partner_sites::parseHex(site.expected), 1);
}
