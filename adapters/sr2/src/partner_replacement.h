#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "partner_sites.h"

namespace partner_replacement {

constexpr uint8_t kNop = 0x90;

// What the exe gets at a site: a branch to `thunk`, then the site's tail (call block) or nops (jump out). A retargeted
// call or jmp keeps its opcode. The tail alone for a bytes site. Empty for a hook, which MinHook writes.

inline std::vector<uint8_t> replacementFor(const partner_sites::Site& site, uintptr_t thunk) {
    using partner_sites::Kind;
    const std::vector<uint8_t> expected = partner_sites::parseHex(site.expected);
    std::vector<uint8_t> bytes;
    auto append = [&bytes](uint8_t opcode, uintptr_t from, uintptr_t to) {
        const auto branch = relativeBranch(opcode, from, to);
        bytes.insert(bytes.end(), branch.begin(), branch.end());
    };
    switch (site.kind) {
        case Kind::HookEntry: break;
        case Kind::RetargetCall: append(expected[0], site.address, thunk); break;
        case Kind::CallBlock:
            append(kCallRel32, site.address, thunk);
            for (const uint8_t byte : partner_sites::parseHex(site.tail)) bytes.push_back(byte);
            break;
        case Kind::JumpOut:
            append(kJmpRel32, site.address, thunk);
            bytes.resize(expected.size(), kNop);
            break;
        case Kind::Bytes: return partner_sites::parseHex(site.tail);
        case Kind::PointerSwap:
            for (size_t shift = 0; shift < sizeof(uint32_t) * 8; shift += 8) bytes.push_back(static_cast<uint8_t>(thunk >> shift));
            break;
    }
    return bytes;
}

}  // namespace partner_replacement
