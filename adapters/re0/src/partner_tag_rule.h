#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

#include "partner_status.h"

// Pure rules of the partner tag's heartbeat trace (hud_partner.cpp, unit tested): the colour, speed and number of beats
// of each condition, the polyline of the beats and how much of it the sweep has drawn at a moment. Coordinates are in the
// reference 44 x 14 box at 720 lines; the overlay scales them.
namespace partner_tag_rule {

using partner_status::Condition;

constexpr float kTraceWidth = 44.0f;
constexpr float kTraceHeight = 14.0f;
constexpr float kBaseline = 7.0f;
constexpr float kLeadIn = 14.0f;  // one beat's flat run before its spike, in a trace of one beat

struct Point {
    float x;
    float y;
};

// One beat's spike after its lead-in: flat, a small rise, a deep fall, back to the baseline.
constexpr Point kSpike[] = {{0.0f, kBaseline}, {3.0f, kBaseline}, {5.0f, 1.0f}, {8.0f, 13.0f}, {10.0f, kBaseline}};

struct Style {
    uint32_t rgb;           // 0xRRGGBB
    float periodSeconds;    // one sweep of the trace
    int beats;
};

constexpr Style styleOf(Condition condition) {
    switch (condition) {
        case Condition::Fine: return {0x5FBF74, 1.6f, 1};
        case Condition::Caution: return {0xD9A93A, 1.1f, 2};
        case Condition::Danger: break;
    }
    return {0xD0503F, 0.7f, 3};
}

// The whole trace: `beats` spikes spread over the width, each with the lead-in scaled to its share, flat to both ends.
inline std::vector<Point> trace(int beats) {
    std::vector<Point> points{{0.0f, kBaseline}};
    const float share = kTraceWidth / static_cast<float>(beats);
    for (int beat = 0; beat < beats; ++beat) {
        const float start = static_cast<float>(beat) * share + share * (kLeadIn / kTraceWidth);
        for (const Point& spike : kSpike) points.push_back({start + spike.x, spike.y});
    }
    points.push_back({kTraceWidth, kBaseline});
    return points;
}

// The sweep's position in the trace's width at `seconds`: it starts at the left and reaches the right end every period.
inline float sweepX(float seconds, float periodSeconds) {
    const float phase = std::fmod(seconds, periodSeconds) / periodSeconds;
    return phase * kTraceWidth;
}

// The part of `points` left of `x`, ending exactly at `x` on the segment it crosses.
inline std::vector<Point> visible(const std::vector<Point>& points, float x) {
    std::vector<Point> out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].x <= x) {
            out.push_back(points[i]);
            continue;
        }
        if (i > 0 && x > points[i - 1].x) {
            const float along = (x - points[i - 1].x) / (points[i].x - points[i - 1].x);
            out.push_back({x, points[i - 1].y + (points[i].y - points[i - 1].y) * along});
        }
        break;
    }
    return out;
}

}  // namespace partner_tag_rule
