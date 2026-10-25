#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Timed on-screen messages. Any thread pushes; the overlay's render thread reads the ones still showing.
namespace toast_queue {

constexpr size_t kMaxVisible = 3;

// Where and how a banner is drawn: a line of text on a soft band at the bottom, or an important message on the panel at the top.
enum class Style : uint8_t { Line, Panel };

struct Visible {
    std::string text;
    Style style;
    float alpha;  // toast_rule::alpha of its age
};

void push(const char* text, float seconds, Style style);

// Drops expired toasts; returns the newest kMaxVisible, oldest first.
std::vector<Visible> visible();

}  // namespace toast_queue
