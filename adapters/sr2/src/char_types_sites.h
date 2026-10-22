#pragma once
#include <cstdint>
#include <iterator>
#include <string_view>

#include "sr2_addresses.h"

// Where M16 W1 changes or reads the exe (docs/SR2_NOTES.md, "One character type per remote"). Copied from the research table of
// M16_W1_CHARTYPE.md; the bytes are what the exe has there. The eight rows are one set, checked together and written all or nothing,
// and only with 3 or more players.
namespace char_types_sites {

enum class Kind : uint8_t {
    HookEntry,  // MinHook detour of the function that starts here
    JumpOut,    // a jmp to the stub, nops over the rest; the stub jumps back to `resume`
    Check,      // checked only
};

struct Row {
    const char* id;
    Kind kind;
    uintptr_t address;
    std::string_view expected;  // hex bytes separated by spaces
    uintptr_t resume;           // JumpOut only: where the stub continues
};

inline constexpr Row kRows[] = {
    {"CT1", Kind::HookEntry, sr2::kCharTypeCategorySetup, "A0 49 8B 52 02 83 EC 0C", 0},
    {"CT1g", Kind::Check, 0x9D65DE, "0F 85 EE 00 00 00", 0},
    {"CT2", Kind::JumpOut, sr2::kCoopLoaderMerge, "8A 15 48 8B 52 02", sr2::kCoopLoaderResume},
    {"CT2r", Kind::Check, sr2::kCoopLoaderResume, "80 FA 01 0F 85 BE 02 00 00", 0},
    {"CT2b", Kind::Check, 0x69C9D0, "8A 15 49 8B 52 02 84 D2 0F 84 8E 01 00 00 80 3D 00 7C 52 02 00 0F 85 81 01 00 00", 0},
    {"CT3", Kind::JumpOut, sr2::kCoopTableFill, "8D 4C 24 10 51 33 C0 B9 44 A7 DD 00 E8 23 4B 20 00", sr2::kCoopTableFillResume},
    {"CT3s", Kind::Check, 0x9D8045, "89 3C F5 D8 03 17 02", 0},
    {"CT3r", Kind::Check, sr2::kCoopTableFillResume, "A0 48 8B 52 02 3A C3", 0},
};

inline constexpr size_t kRowCount = std::size(kRows);

}  // namespace char_types_sites
