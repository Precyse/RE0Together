#pragma once
// The world gate as a pure state machine (cd design, adapter.md 8): Title, Loading, AwaitLocalReady, Ready, and the
// load serial every actor-bearing wire message carries. Fed by world_gate.cpp from the funnels' load bracket; the
// caller logs each transition with its cause.
//
// Phase 1 readiness: the first own player MoveActorReq after the load completed, and 1.5 s since the completion. The
// actor login check (u16 +0x5E == 0x10, +0xCF != 0) joins when the presence phase reads actors.
#include <cstdint>

namespace world_gate_rule {

enum class State : uint8_t { Title = 0, Loading = 1, AwaitLocalReady = 2, Ready = 3 };

enum class Cause : uint8_t { None, LoadStart, LoadComplete, LoadCompleteUnseenStart, LocalReady, Logout, LoadTimeout };

constexpr uint64_t kSettleMs = 1500;
constexpr uint64_t kLoadingCapMs = 120000;

struct Transition {
    State from = State::Title;
    State to = State::Title;
    Cause cause = Cause::None;
    bool changed() const { return cause != Cause::None; }
};

class Gate {
public:
    State state() const { return state_; }
    uint16_t loadSerial() const { return serial_; }

    // Our own GameLoadingStartReq or ReviveLoadingReq.
    Transition loadStarted(uint64_t nowMs) {
        if (state_ == State::Loading) return {};
        bumpSerial();
        return enter(State::Loading, Cause::LoadStart, nowMs);
    }

    // LoadingCompleteAck. A completion with no start seen (the adapter installed mid-load) still counts as a load.
    Transition loadCompleted(uint64_t nowMs) {
        if (state_ == State::AwaitLocalReady || state_ == State::Ready) return {};
        Cause cause = Cause::LoadComplete;
        if (state_ == State::Title) {
            bumpSerial();
            cause = Cause::LoadCompleteUnseenStart;
        }
        ownMoveSeen_ = false;
        return enter(State::AwaitLocalReady, cause, nowMs);
    }

    // Our own MoveActorReq for a player-class key.
    void ownPlayerMoved() {
        if (state_ == State::AwaitLocalReady) ownMoveSeen_ = true;
    }

    // LogoutCharacterToFieldServerReq or LogoutUserToServerAck.
    Transition loggedOut(uint64_t nowMs) {
        if (state_ == State::Title) return {};
        return enter(State::Title, Cause::Logout, nowMs);
    }

    Transition tick(uint64_t nowMs) {
        if (state_ == State::Loading && nowMs - enteredMs_ >= kLoadingCapMs) {
            return enter(State::Title, Cause::LoadTimeout, nowMs);
        }
        if (state_ == State::AwaitLocalReady && ownMoveSeen_ && nowMs - enteredMs_ >= kSettleMs) {
            return enter(State::Ready, Cause::LocalReady, nowMs);
        }
        return {};
    }

private:
    void bumpSerial() {
        ++serial_;
        if (serial_ == 0) serial_ = 1;  // 0 means "no load yet" on the wire
    }

    Transition enter(State to, Cause cause, uint64_t nowMs) {
        const Transition t{state_, to, cause};
        state_ = to;
        enteredMs_ = nowMs;
        return t;
    }

    State state_ = State::Title;
    uint16_t serial_ = 0;
    uint64_t enteredMs_ = 0;
    bool ownMoveSeen_ = false;
};

inline const char* name(State state) {
    switch (state) {
        case State::Title: return "Title";
        case State::Loading: return "Loading";
        case State::AwaitLocalReady: return "AwaitLocalReady";
        case State::Ready: return "Ready";
    }
    return "?";
}

inline const char* name(Cause cause) {
    switch (cause) {
        case Cause::None: return "none";
        case Cause::LoadStart: return "load start";
        case Cause::LoadComplete: return "load complete";
        case Cause::LoadCompleteUnseenStart: return "load complete (start not seen)";
        case Cause::LocalReady: return "local player ready";
        case Cause::Logout: return "logout";
        case Cause::LoadTimeout: return "no completion in 120 s";
    }
    return "?";
}

}  // namespace world_gate_rule
