#pragma once
// The forced load from the title (pure, unit tested): when the plugin asks the game to continue by itself, and how it tells a
// load that went through from one that did not. The game's own load, MainFlowManager.requestStartType(StartType.Latest), is
// called directly (title_load.cpp); no row of the title menu is chosen. Evidence: docs/codemap/dd2.md "Title load".
#include <cstdint>
#include <optional>
#include <string_view>

namespace title_load_rule {

// The title has been ready this long before the request: the GUI's own save conversion and fades are done.
constexpr uint64_t kSettleMs = 2000;

// After a request the flow must have left the TitleMenu phase this soon, else the game did not take it.
constexpr uint64_t kTitleStallMs = 5000;

// Past the title and not in an error phase this long: the load went through (the phases of a load can pass within one
// frame, so a single look does not see them all).
constexpr uint64_t kSurviveMs = 3000;

constexpr int kMaxTries = 3;

// What one tick of the title shows.
struct Title {
    bool menuPhase;   // MainFlowManager is in its TitleMenu phase
    bool guiWaiting;  // the title GUI (app.ui030201) is up and idle: waiting for a key (flow Title) or a choice (flow SelectMenu)
    bool saveIdle;    // the SaveService has nothing running
    bool safeToLoad;  // nothing is loaded from the real save folder (save_redirect::safeToLoad)
};

inline bool ready(const Title& title) { return title.menuPhase && title.guiWaiting && title.saveIdle && title.safeToLoad; }

// A MainFlowManager.Phase by name, as the load cares about it.
enum class Phase : uint8_t { Title, Loaded, Error, Other };

inline Phase classify(std::string_view name) {
    if (name == "TitleMenu") return Phase::Title;
    if (name == "RequestLoad") return Phase::Loaded;  // the save was read: the success callbacks of requestLoadGameData
    if (name == "RequestLoadDataError" || name == "LoadDataError" || name == "LoadDataError0014") return Phase::Error;
    return Phase::Other;
}

enum class Action : uint8_t { None, Request, Succeeded, GaveUp };

// Request once the title has been ready for kSettleMs; a try that fails (the call refused, an error phase, still at the title
// after kTitleStallMs) waits for the title to be ready again and asks again, kMaxTries in all, then gives up.
class Loader {
public:
    Action step(uint64_t nowMs, bool titleReady, Phase phase) {
        if (state_ == State::Waiting) return waiting(nowMs, titleReady);
        if (state_ == State::Requested) return requested(nowMs, phase);
        return Action::None;
    }

    // The request call itself was refused (no manager, the call threw).
    Action refused() { return failed(); }

    int tries() const { return tries_; }

    // Succeeded or given up: nothing more to do.
    bool finished() const { return state_ == State::Done; }

private:
    enum class State : uint8_t { Waiting, Requested, Done };

    Action waiting(uint64_t nowMs, bool titleReady) {
        if (!titleReady) {
            readySince_.reset();
            return Action::None;
        }
        if (!readySince_) readySince_ = nowMs;
        if (nowMs - *readySince_ < kSettleMs) return Action::None;
        ++tries_;
        state_ = State::Requested;
        requestedMs_ = nowMs;
        pastTitleSince_.reset();
        return Action::Request;
    }

    Action requested(uint64_t nowMs, Phase phase) {
        switch (phase) {
            case Phase::Loaded: return succeeded();
            case Phase::Error: return failed();
            case Phase::Title:
                pastTitleSince_.reset();
                return nowMs - requestedMs_ >= kTitleStallMs ? failed() : Action::None;
            case Phase::Other:
                if (!pastTitleSince_) pastTitleSince_ = nowMs;
                return nowMs - *pastTitleSince_ >= kSurviveMs ? succeeded() : Action::None;
        }
        return Action::None;
    }

    Action succeeded() {
        state_ = State::Done;
        return Action::Succeeded;
    }

    Action failed() {
        readySince_.reset();
        if (tries_ >= kMaxTries) {
            state_ = State::Done;
            return Action::GaveUp;
        }
        state_ = State::Waiting;
        return Action::None;
    }

    State state_ = State::Waiting;
    int tries_ = 0;
    uint64_t requestedMs_ = 0;
    std::optional<uint64_t> readySince_;
    std::optional<uint64_t> pastTitleSince_;
};

}  // namespace title_load_rule
