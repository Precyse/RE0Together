#pragma once
#include <vector>

#include "coop_page_rule.h"

// What the overlay draws for the Co-op page. The game thread publishes the page every frame it is open; the overlay's
// render thread reads it. A page that stops being refreshed (the screen was torn down) disappears by itself.
namespace coop_page_view {

constexpr const char kTitle[] = "Co-op";
constexpr const char kBackLabel[] = "Back";

// The page is open with these rows.
void publish(std::vector<coop_page_rule::Row> rows);

void clear();

// The rows of the page when it is open and fresh, else empty.
std::vector<coop_page_rule::Row> current();

}  // namespace coop_page_view
