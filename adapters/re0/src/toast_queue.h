#pragma once
#include <string>
#include <vector>

// Timed on-screen messages. Any thread pushes; the overlay's render thread reads the ones still showing.
namespace toast_queue {

constexpr size_t kMaxVisible = 3;
constexpr float kFadeSeconds = 0.5f;

struct Visible {
    std::string text;
    float alpha;  // 1 until the last kFadeSeconds, then falls to 0
};

void push(const char* text, float seconds);

// Drops expired toasts; returns the newest kMaxVisible, oldest first.
std::vector<Visible> visible();

}  // namespace toast_queue
