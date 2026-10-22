#pragma once
#include <cstdint>

// The pure rules of the guest's free-roam autosave (unit tested; docs/SR2_NOTES.md, "Stalled peers and rejoins"). A co-op guest
// plays from its own save: its cash (+0x1114), respect, weapons and ammo, looks and garage load from it, the host's game never
// holds them (cash, weapon and ammo grants go to the owner, nothing is sent back), and a guest writes its save only at a
// completion screen (the autosave 0x696600 from 0x6A29B0, 0x53A050, 0x750550, 0x53EDB0) or from the pause menu. A guest whose
// game dies loses everything since then. So after kIntervalMs of play since its last save, at a safe moment of free roam, the
// guest's adapter asks for the game's own autosave, which it loads again when it rejoins.
namespace coop_autosave_rules {

constexpr uint64_t kMsPerMinute = 60 * 1000;
constexpr uint64_t kIntervalMs = 5 * kMsPerMinute;
constexpr int32_t kFreeRoam = -1;     // 0x6A74E0: no mission and no activity
constexpr int32_t kMaxSaves = 0x1FF;  // 0x696600 refuses an autosave with more saves than this

// The game's own test of 0x696600 that opens the device full dialog instead of saving.
constexpr bool deviceDialog(int32_t saveCount, bool autosaveListed, bool noDevice) {
    return (saveCount < 1 || !autosaveListed) && ((noDevice && saveCount < 1) || saveCount > kMaxSaves);
}

struct World {
    bool guest;           // a session this machine does not host
    bool inGame;          // our member is at 0xD
    bool saving;          // a save is being written (0x2527BD2)
    int32_t missionType;  // 0x6A74E0
    bool held;            // the game holds saves: suppressed (0x2527BE6) or an autosave waits for a fade (0x2527BDB)
    bool cutscene;
    bool paused;          // the world's pause count is not 0
    bool playerUp;        // the local player is alive and not down
    bool deviceDialog;
};

// Only the game's free roam with nothing on screen, the player standing: an autosave in a mission writes the mission's start.
constexpr bool safeMoment(const World& world) {
    return world.missionType == kFreeRoam && !world.held && !world.cutscene && !world.paused && world.playerUp && !world.deviceDialog;
}

// The time since the guest's last save: a save the game writes itself (completion, pause menu) starts it again too.
class Timer {
public:
    // True when the adapter asks for an autosave now.
    bool due(uint64_t nowMs, const World& world) {
        if (!world.guest || !world.inGame || world.saving || lastMs_ == 0) {
            lastMs_ = nowMs;
            return false;
        }
        if (nowMs - lastMs_ < kIntervalMs || !safeMoment(world)) return false;
        lastMs_ = nowMs;
        return true;
    }

private:
    uint64_t lastMs_ = 0;
};

}  // namespace coop_autosave_rules
