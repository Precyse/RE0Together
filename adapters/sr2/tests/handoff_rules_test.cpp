// The object hand-over target rule of milestone 9 (no game): the nearest remote that has the chunk.
#include <cstdio>

#include "../src/handoff_rules.h"
#include "check.h"

namespace {

using namespace handoff_rules;

constexpr uint16_t kChunk = 7;
constexpr uintptr_t kNear = 0x100;
constexpr uintptr_t kMiddle = 0x200;
constexpr uintptr_t kFar = 0x300;
constexpr uintptr_t kFallback = 0x900;

}  // namespace

int main() {
    Candidate candidates[] = {{kFar, 900}, {kNear, 25}, {kMiddle, 400}};
    nearestFirst(candidates, 3);
    check(candidates[0].member == kNear && candidates[1].member == kMiddle && candidates[2].member == kFar, "nearest first");

    size_t asked = 0;
    const auto has = [&](uintptr_t wanted) {
        return [&asked, wanted](uintptr_t member) {
            ++asked;
            return member == wanted;
        };
    };
    check(target(candidates, 3, kChunk, has(kNear), kFallback) == kNear && asked == 1, "the nearest has the chunk: it, after one query");
    asked = 0;
    check(target(candidates, 3, kChunk, has(kMiddle), kFallback) == kMiddle && asked == 2,
          "only the second has it: the nearest is not handed an object it would hand back");
    check(target(candidates, 3, kChunk, has(0), kFallback) == kNear, "none has the chunk: the nearest, as before");
    asked = 0;
    check(target(candidates, 3, kNoChunk, has(kFar), kFallback) == kNear && asked == 0, "an object with no chunk is not asked about");
    check(target(candidates, 0, kChunk, has(kNear), kFallback) == kFallback, "no remote with a human: the fallback member");

    const Candidate one[] = {{kMiddle, 10}};
    check(target(one, 1, kChunk, has(kMiddle), kFallback) == kMiddle && target(one, 1, kChunk, has(0), kFallback) == kMiddle,
          "with one remote it is that remote whether or not it has the chunk (the 2-player case)");
    return g_failures == 0 ? 0 : 1;
}
