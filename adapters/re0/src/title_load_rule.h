#pragma once
#include <cstdint>
#include <optional>

// Pure rules of the session load from the title (title_load.cpp, unit tested; docs/RE0_NOTES.md "Forced save load"):
// the host's Start on the Co-op page and a guest following the host's start load a slot the way the title's own Load
// Game -> Continue does (requestLoad, wait, start mode and Wesker on sGameInfo, the area switch), driven from the
// title's move while the title is parked. A guest never starts a world whose bytes differ from the host's.
namespace title_load_rule {

constexpr int kMaxRequestTries = 5;          // a load that reports an error is asked again this often
constexpr uint64_t kRetryMs = 2000;          // between two tries, and between two looks for a newer save copy
constexpr uint64_t kFileWaitMs = 60000;      // how long a guest waits for its copy to become the host's
constexpr uint64_t kRequestRefusedMs = 10000;  // the save manager refusing the request (another runs) this long fails

// Title states (uGUITitle +0x27c) the load cares about.
constexpr uint32_t kStateMainMenuOpen = 0x1f;
constexpr uint32_t kStateMainMenuInput = 0x20;
constexpr uint32_t kStateOptionsWait = 0x28;  // waits for the Options screen to close, then reopens the main menu (0x1f)
constexpr uint32_t kStateParked = 0x7f;       // title_coop_rule::kStateParked

enum class Stage : uint8_t {
    Idle,
    FadingOut,   // the title is parked; the screen fades to black
    Requesting,  // ask the save manager for the slot
    Loading,     // the read runs
    Retrying,    // wait kRetryMs, then ask again
    Started,     // the area switch ran: the title is going away
};

struct Order {
    int32_t slot = -1;
    bool host = false;
    std::optional<uint32_t> expectedHash;  // guest: the host's slot hash; host: none (its own bytes are the session's)
};

// Where a pending order takes the title: from the main menu waiting for input once its sequence and the fade are done,
// or on the frame the Options screen closed (0x28 -> 0x1f: the screen is already black, the menu must not show).
constexpr bool takesTitle(uint32_t stateBefore, uint32_t stateAfter, bool menuSettled) {
    return (stateBefore == kStateMainMenuInput && menuSettled) || (stateBefore == kStateOptionsWait && stateAfter == kStateMainMenuOpen);
}

enum class Verdict : uint8_t { Commit, Retry, Fail };

// After a load finished: `result` is the save manager's (0 ok), `actualHash` the loaded slot's block hash, `tries` the
// requests made so far, `waitedMs` the time since the first request.
constexpr Verdict afterLoad(const Order& order, int32_t result, std::optional<uint32_t> actualHash, int tries, uint64_t waitedMs) {
    if (result != 0) return tries < kMaxRequestTries ? Verdict::Retry : Verdict::Fail;
    if (!order.expectedHash || (actualHash && *actualHash == *order.expectedHash)) return Verdict::Commit;
    return waitedMs < kFileWaitMs ? Verdict::Retry : Verdict::Fail;
}

// The launcher's save pick on this title, once per game run: a guest never starts one (it follows the host's start),
// a host whose link is not up yet waits, and a slot whose bytes are no longer the ones the launcher hashed is skipped.
enum class LauncherPick : uint8_t { Wait, Start, Skip };

constexpr LauncherPick launcherPick(bool pickActive, bool linked, bool host, std::optional<uint32_t> pickHash,
                                    std::optional<uint32_t> slotHash) {
    if (!pickActive) return LauncherPick::Skip;
    if (!linked) return LauncherPick::Wait;
    if (!host) return LauncherPick::Skip;
    if (pickHash && (!slotHash || *slotHash != *pickHash)) return LauncherPick::Skip;
    return LauncherPick::Start;
}

// The save manager refused the request (another one runs): try again next frame until kRequestRefusedMs passed.
constexpr bool requestGivesUp(uint64_t refusedMs) { return refusedMs >= kRequestRefusedMs; }

}  // namespace title_load_rule
