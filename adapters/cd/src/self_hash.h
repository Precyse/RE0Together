#pragma once
// SHA-256 of this adapter DLL as installed: CD_HELLO carries it so two adapters of different builds never pair.
#include <array>
#include <cstdint>

namespace self_hash {

// Zeros when the file cannot be read. Computed once, then cached.
const std::array<uint8_t, 32>& adapterHash();

}  // namespace self_hash
