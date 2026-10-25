#pragma once
#include <cstdint>

// Pure rules of the rescue chord and the orphan-lock watchdog (no game, unit tested; docs/RE0_NOTES.md "Rescue chord and
// orphan lock"): when a held chord counts as a press, what each press of a scene does, when pause bits that no script
// holds are released on their own, and what an event release may pop off the engine's phase stack.
namespace rescue_rule {

constexpr uint32_t kHoldMs = 3000;         // the chord is held this long to count as one press
constexpr uint32_t kLadderResetMs = 30000;  // a ladder that was not pressed for this long starts over
constexpr uint32_t kOrphanMs = 4000;        // lock bits held this long with no script running are released
constexpr int32_t kEventPhase = 13;         // room_phase::Event
constexpr int32_t kNoPhase = -1;            // the phase manager's "nothing requested"
constexpr uint16_t kNoScene = 0xffff;       // scene::kNone: no room is loaded

// ---- The chord ---------------------------------------------------------------------------------------------------

// A chord held for kHoldMs is one press; it must be let go before it can press again.
class Chord {
public:
    bool update(uint32_t nowMs, bool held) {
        if (!held) {
            holding_ = false;
            return false;
        }
        if (!holding_) {
            holding_ = true;
            fired_ = false;
            sinceMs_ = nowMs;
        }
        if (fired_ || nowMs - sinceMs_ < kHoldMs) return false;
        fired_ = true;
        return true;
    }

private:
    bool holding_ = false;
    bool fired_ = false;
    uint32_t sinceMs_ = 0;
};

// ---- The ladder --------------------------------------------------------------------------------------------------

enum class Action : uint8_t {
    Release,  // free what holds the world: followed scripts, a stuck event, a demo, pause bits, the peer's menu hold
    Reload,   // run the door that led into the room again
    Toast,    // nothing left to try: tell the player to quit and rejoin
};

enum class Refusal : uint8_t { None, Door, Menu, Boss, NoDoor, AlreadyReloaded };

constexpr const char* name(Refusal refusal) {
    switch (refusal) {
        case Refusal::Door: return "door active";
        case Refusal::Menu: return "menu open";
        case Refusal::Boss: return "boss alive";
        case Refusal::NoDoor: return "no door into this scene";
        case Refusal::AlreadyReloaded: return "already reloaded in this scene";
        case Refusal::None: break;
    }
    return "none";
}

// What the game says when a press asks for a reload.
struct Conditions {
    bool doorActive = false;
    bool menuOpen = false;
    bool bossAlive = false;
    bool hasDoor = false;  // the own character has a last door into the loaded scene
};

struct Decision {
    Action action = Action::Release;
    Refusal refusal = Refusal::None;  // why a Reload became a Toast
};

constexpr Refusal refusalOf(const Conditions& conditions, bool alreadyReloaded) {
    if (conditions.doorActive) return Refusal::Door;
    if (conditions.menuOpen) return Refusal::Menu;
    if (conditions.bossAlive) return Refusal::Boss;
    if (!conditions.hasDoor) return Refusal::NoDoor;
    return alreadyReloaded ? Refusal::AlreadyReloaded : Refusal::None;
}

// Per scene: the first press releases, the second reloads the room, every later one only toasts. The ladder starts over
// when the scene changes or kLadderResetMs pass after its last press; a room reloaded once is not reloaded again until
// the player has been somewhere else (the second rescue in a scene only frees).
class Ladder {
public:
    // The loaded scene this tick: a different one starts everything over (no scene while a room loads is not a change, so a
    // reload does not start over).
    void onScene(uint16_t scene) {
        if (scene == kNoScene || scene == scene_) return;
        scene_ = scene;
        presses_ = 0;
        reloaded_ = false;
    }

    Decision press(uint32_t nowMs, uint16_t scene, const Conditions& conditions) {
        onScene(scene);
        if (presses_ > 0 && nowMs - lastMs_ >= kLadderResetMs) presses_ = 0;
        lastMs_ = nowMs;
        const unsigned step = presses_++;
        if (step == 0) return {Action::Release, Refusal::None};
        if (step > 1) return {Action::Toast, Refusal::None};
        const Refusal refusal = refusalOf(conditions, reloaded_);
        if (refusal != Refusal::None) return {Action::Toast, refusal};
        reloaded_ = true;
        return {Action::Reload, Refusal::None};
    }

private:
    uint16_t scene_ = kNoScene;
    unsigned presses_ = 0;
    uint32_t lastMs_ = 0;
    bool reloaded_ = false;
};

// ---- The orphan lock ---------------------------------------------------------------------------------------------

// Pause bits (cutscene bits and the event phase bit) that stay set with no script thread running, in plain gameplay: a
// script that ended without clearing them. They are released after kOrphanMs; a thread or leaving plain gameplay (a
// phase change) starts the count again, and so does a release.
class OrphanLock {
public:
    bool observe(uint32_t nowMs, uint32_t lockBits, bool threadActive, bool playing) {
        if (lockBits == 0 || threadActive || !playing) {
            armed_ = false;
            return false;
        }
        if (!armed_) {
            armed_ = true;
            sinceMs_ = nowMs;
            return false;
        }
        if (nowMs - sinceMs_ < kOrphanMs) return false;
        sinceMs_ = nowMs;
        return true;
    }

private:
    bool armed_ = false;
    uint32_t sinceMs_ = 0;
};

// ---- What an event release may pop -------------------------------------------------------------------------------

enum class Release : uint8_t { Nothing, PopEvent };

// The engine's own end of an event (a pop of the phase stack) only when the current phase is Event, nothing is requested
// and a phase is stacked below it; the engine's pop has no depth guard, so a pop at depth 0 would read below the stack,
// and a script thread that is still running ends its event itself.
constexpr Release releasePlan(int32_t phase, int32_t depth, int32_t requested, bool threadActive) {
    const bool popsEvent = phase == kEventPhase && requested == kNoPhase && depth > 0 && !threadActive;
    return popsEvent ? Release::PopEvent : Release::Nothing;
}

}  // namespace rescue_rule
