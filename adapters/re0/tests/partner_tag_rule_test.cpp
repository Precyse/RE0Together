// Checks the partner tag's trace: the style of each condition, the polyline of one to three beats, the sweep and the part
// of the trace it has drawn (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/partner_tag_rule.h"

namespace {

using namespace partner_tag_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testStyle() {
    check(styleOf(Condition::Fine).rgb == 0x5FBF74 && styleOf(Condition::Fine).beats == 1 && near(styleOf(Condition::Fine).periodSeconds, 1.6f),
          "fine is green, one beat, 1.6 s");
    check(styleOf(Condition::Caution).rgb == 0xD9A93A && styleOf(Condition::Caution).beats == 2 && near(styleOf(Condition::Caution).periodSeconds, 1.1f),
          "caution is amber, two beats, 1.1 s");
    check(styleOf(Condition::Danger).rgb == 0xD0503F && styleOf(Condition::Danger).beats == 3 && near(styleOf(Condition::Danger).periodSeconds, 0.7f),
          "danger is red, three beats, 0.7 s");
}

void testTrace() {
    const std::vector<Point> one = trace(1);
    const Point expected[] = {{0, 7}, {14, 7}, {17, 7}, {19, 1}, {22, 13}, {24, 7}, {44, 7}};
    check(one.size() == 7, "one beat is seven points");
    for (size_t i = 0; i < one.size() && i < 7; ++i) check(near(one[i].x, expected[i].x) && near(one[i].y, expected[i].y), "the single beat's points");
    check(trace(2).size() == 12 && trace(3).size() == 17, "each further beat adds a spike");
    for (int beats = 1; beats <= 3; ++beats) {
        const std::vector<Point> points = trace(beats);
        bool ordered = true;
        bool inside = true;
        for (size_t i = 0; i < points.size(); ++i) {
            ordered = ordered && (i == 0 || points[i].x >= points[i - 1].x);
            inside = inside && points[i].x >= 0.0f && points[i].x <= kTraceWidth && points[i].y >= 0.0f && points[i].y <= kTraceHeight;
        }
        check(ordered && inside, "every trace runs left to right inside its box");
        check(near(points.front().x, 0.0f) && near(points.back().x, kTraceWidth), "and spans the whole width");
    }
}

void testSweep() {
    check(near(sweepX(0.0f, 1.6f), 0.0f), "the sweep starts at the left");
    check(near(sweepX(0.8f, 1.6f), kTraceWidth * 0.5f), "half a period is half the width");
    check(near(sweepX(1.6f + 0.4f, 1.6f), kTraceWidth * 0.25f), "and it starts over each period");
}

void testVisible() {
    const std::vector<Point> points = trace(1);
    check(visible(points, 0.0f).size() == 1, "nothing but the start at the left");
    const std::vector<Point> half = visible(points, 18.0f);
    check(half.size() == 4 && near(half.back().x, 18.0f) && near(half.back().y, 4.0f), "the sweep cuts a segment where it stands");
    check(visible(points, kTraceWidth).size() == points.size(), "the whole trace at the right end");
    check(visible(points, 14.0f).size() == 2, "a point exactly at the sweep is kept");
}

}  // namespace

int main() {
    testStyle();
    testTrace();
    testSweep();
    testVisible();
    if (g_failures == 0) std::printf("partner_tag_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
