#pragma once
// Where the local player is and the load serial (pure, unit tested; DD2 design adapter.md 4). Fed with the names of
// MainFlowManager phases and scene types (resolved from the type database at start) and the ferrystone load start.
//
// The load serial (u16, never 0) bumps once per full load: entering InGame from any other phase, a scene type change
// between Field, Event, GameOver and RimStone, and a ferrystone load. Open-world cell streaming is not a load.
#include <array>
#include <cstdint>
#include <string_view>

namespace world_gate_rule {

enum class Place : uint8_t { Title = 0, Loading = 1, InGame = 2, Menu = 3, Cutscene = 4, Down = 5, Travel = 6, Rift = 7 };

enum class Cause : uint8_t { None, EnteredInGame, SceneChange, Ferrystone };

// Phases up to the title menu, before any save is loaded.
constexpr std::array<std::string_view, 11> kTitlePhases{
    "Invalid", "WaitReady", "CheckContinue", "StartUp", "CheckNetwork", "AppVersionError", "ServerVersionError",
    "ServerVersionErrorWait", "TitleMenu", "CheckExtraContentsMount", "CheckExtraContents"};

constexpr std::string_view kInGame = "InGame";

// The scene types whose changes are loads.
constexpr std::array<std::string_view, 4> kLoadScenes{"Field", "Event", "GameOver", "RimStone"};

template <size_t N>
constexpr bool contains(const std::array<std::string_view, N>& list, std::string_view name) {
    for (std::string_view entry : list) {
        if (entry == name) return true;
    }
    return false;
}

class Gate {
public:
    uint16_t loadSerial() const { return serial_; }

    Place place() const {
        if (title_) return Place::Title;
        if (travelling_) return Place::Travel;
        if (!inGame_) return Place::Loading;
        if (scene_ == "Event") return Place::Cutscene;
        if (scene_ == "GameOver") return Place::Down;
        if (scene_ == "RimStone") return Place::Rift;
        return Place::InGame;
    }

    // MainFlowManager.changeFlowPhase, after the change.
    Cause phaseChanged(std::string_view phase) {
        const bool wasInGame = inGame_;
        title_ = contains(kTitlePhases, phase);
        inGame_ = phase == kInGame;
        if (!inGame_ || wasInGame) return Cause::None;
        if (travelling_) {  // the ferrystone load already counted
            travelling_ = false;
            return Cause::None;
        }
        bump();
        return Cause::EnteredInGame;
    }

    // MainFlowManager.changeSceneType, after the change. `scene` must outlive the gate (a resolved enum name).
    Cause sceneChanged(std::string_view scene) {
        const std::string_view before = scene_;
        scene_ = scene;
        if (before == scene || !contains(kLoadScenes, before) || !contains(kLoadScenes, scene)) return Cause::None;
        bump();
        return Cause::SceneChange;
    }

    // MainFlowManager.onCompleteSceneLoad: ends a ferrystone travel that never left InGame.
    void sceneLoadCompleted() { travelling_ = false; }

    // FerrystoneFlowController.startLoad: a travel with a loading screen; Travel until the scene load completes or
    // InGame is entered again.
    Cause ferrystoneStarted() {
        travelling_ = true;
        bump();
        return Cause::Ferrystone;
    }

private:
    void bump() {
        ++serial_;
        if (serial_ == 0) serial_ = 1;
    }

    uint16_t serial_ = 0;
    bool title_ = true;
    bool inGame_ = false;
    bool travelling_ = false;
    std::string_view scene_;
};

inline const char* name(Cause cause) {
    switch (cause) {
        case Cause::None: return "none";
        case Cause::EnteredInGame: return "entered InGame";
        case Cause::SceneChange: return "scene change";
        case Cause::Ferrystone: return "ferrystone";
    }
    return "?";
}

}  // namespace world_gate_rule
