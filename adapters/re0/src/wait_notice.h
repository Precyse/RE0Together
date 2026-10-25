#pragma once
#include <optional>
#include <string>

#include "wait_notice_rule.h"

// The one on-screen wait notice (docs/RE0_NOTES.md "Wait notices"): set by the world hold (menu_mirror) while this
// machine waits for a partner, drawn by the overlay until it is cleared. Any thread.
namespace wait_notice {

struct Shown {
    wait_notice_rule::Notice notice;
    std::string text;
};

void show(wait_notice_rule::Notice notice, std::string text);

void clear();

std::optional<Shown> current();

}  // namespace wait_notice
