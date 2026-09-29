#pragma once
#include <string>
#include <vector>

#include "debug_stats.h"

namespace debug_lines {

constexpr size_t kLabelChars = 20;
constexpr size_t kValueChars = 40;

struct Line {
    std::wstring label;
    std::wstring value;
    bool bad;  // drawn red: a down link or an error
};

// One fact per line: only labels and values.
std::vector<Line> build(const debug_stats::Snapshot& stats);

}  // namespace debug_lines
