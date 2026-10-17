#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "join_deny_rules.h"
#include "join_status.h"

// What the guest does on each front end tick (pure, unit tested): wait for the game to be ready, log in, search the
// browser, join the host that was found, and count a join that ended back at the idle front end as a failed attempt.
// docs/SR2_NOTES.md "Force join", guest.
namespace guest_join_rules {

constexpr uint64_t kLoginRetryMs = 30000;     // between the login calls
constexpr unsigned kLoginCalls = 2;           // then the player has to log in
constexpr uint64_t kSearchTimeoutMs = 12000;  // a query without a match ends
constexpr uint64_t kSearchRetryMs = 5000;     // before the next query
constexpr uint64_t kIdleFailMs = 10000;       // idle front end after a join: the attempt failed
constexpr unsigned kMaxFailures = 3;          // failed attempts before giving up

// The browser module's state (its +0x10).
enum class Browser : uint8_t { Idle, Querying, Done, Error };

struct Inputs {
    uint64_t nowMs = 0;
    bool loggedIn = false;
    bool frontEndFree = false;  // no pending start, no pending game, no dialog, no session
    bool dialogOpen = false;
    bool sessionActive = false;
    Browser browser = Browser::Idle;
    std::optional<size_t> match;  // the host's index among the listed entries
    size_t listed = 0;
    uint8_t denyReason = join_deny_rules::kReasonNone;  // the last join_deny received since the join call
};

// What the executor does now.
enum class Step : uint8_t { None, Login, Search, Join, GiveUp };

struct Decision {
    Step step = Step::None;
    size_t index = 0;  // Join
    join_status::Status status;
};

class Machine {
public:
    Decision update(const Inputs& in);

    // Back to the start, for a new request (a new token, or the launcher's Join pressed again).
    void reset() { *this = Machine(); }

private:
    enum class Phase : uint8_t { Searching, Attempt, Joined, Failed };

    Decision searching(const Inputs& in);
    Decision login(const Inputs& in);
    Decision attempt(const Inputs& in);
    Decision failedAttempt(const Inputs& in);

    Phase phase_ = Phase::Searching;
    bool queryRunning_ = false;
    uint64_t queryStartMs_ = 0;
    uint64_t nextQueryMs_ = 0;
    unsigned loginCalls_ = 0;
    uint64_t lastLoginMs_ = 0;
    unsigned failures_ = 0;
    bool idle_ = false;
    uint64_t idleSinceMs_ = 0;
    const char* failDetail_ = "no join";
};

inline Decision Machine::update(const Inputs& in) {
    switch (phase_) {
        case Phase::Failed: return {Step::None, 0, {"failed", failDetail_}};
        case Phase::Joined: return {Step::None, 0, {in.sessionActive ? "joined" : "stopped", ""}};
        case Phase::Attempt: return attempt(in);
        case Phase::Searching: break;
    }
    if (in.sessionActive) {
        phase_ = Phase::Joined;
        return {Step::None, 0, {"joined", ""}};
    }
    return searching(in);
}

inline Decision Machine::searching(const Inputs& in) {
    if (!in.frontEndFree) return {Step::None, 0, {"waiting", "front end"}};
    if (!in.loggedIn) return login(in);
    loginCalls_ = 0;
    if (!queryRunning_) {
        if (in.nowMs < nextQueryMs_) return {Step::None, 0, {"searching", std::to_string(in.listed)}};
        queryRunning_ = true;
        queryStartMs_ = in.nowMs;
        return {Step::Search, 0, {"searching", "0"}};
    }
    if (in.match) {
        phase_ = Phase::Attempt;
        idle_ = false;
        return {Step::Join, *in.match, {"found", ""}};
    }
    const bool over = in.browser == Browser::Done || in.browser == Browser::Error ||
                      in.nowMs - queryStartMs_ >= kSearchTimeoutMs;
    if (over) {
        queryRunning_ = false;
        nextQueryMs_ = in.nowMs + kSearchRetryMs;
    }
    return {Step::None, 0, {"searching", std::to_string(in.listed)}};
}

inline Decision Machine::login(const Inputs& in) {
    const bool due = loginCalls_ == 0 || (loginCalls_ < kLoginCalls && in.nowMs - lastLoginMs_ >= kLoginRetryMs);
    if (due) {
        ++loginCalls_;
        lastLoginMs_ = in.nowMs;
        return {Step::Login, 0, {"waiting", "login"}};
    }
    const bool needed = loginCalls_ == kLoginCalls && in.nowMs - lastLoginMs_ >= kLoginRetryMs;
    return {Step::None, 0, needed ? join_status::Status{"login_needed", ""} : join_status::Status{"waiting", "login"}};
}

inline Decision Machine::attempt(const Inputs& in) {
    if (in.sessionActive) {
        phase_ = Phase::Joined;
        return {Step::None, 0, {"joined", ""}};
    }
    if (const char* detail = join_deny_rules::finalDetail(in.denyReason)) {
        phase_ = Phase::Failed;
        failDetail_ = detail;
        return {Step::GiveUp, 0, {"failed", detail}};
    }
    if (in.dialogOpen) {
        idle_ = false;
        return {Step::None, 0, {"dialog", ""}};
    }
    if (!in.frontEndFree) {
        idle_ = false;
        return {Step::None, 0, {"joining", ""}};
    }
    if (!idle_) {
        idle_ = true;
        idleSinceMs_ = in.nowMs;
    }
    if (in.nowMs - idleSinceMs_ < kIdleFailMs) return {Step::None, 0, {"joining", ""}};
    return failedAttempt(in);
}

inline Decision Machine::failedAttempt(const Inputs& in) {
    if (++failures_ >= kMaxFailures) {
        phase_ = Phase::Failed;
        failDetail_ = "no join";
        return {Step::GiveUp, 0, {"failed", failDetail_}};
    }
    phase_ = Phase::Searching;
    queryRunning_ = false;
    nextQueryMs_ = in.nowMs + kSearchRetryMs;
    return {Step::None, 0, {"searching", "0"}};
}

}  // namespace guest_join_rules
