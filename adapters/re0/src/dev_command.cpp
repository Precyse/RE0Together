#include "dev_command.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "dev_command_rule.h"
#include "event_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "paths.h"
#include "rescue.h"

namespace {

namespace rule = dev_command_rule;
using Clock = std::chrono::steady_clock;

constexpr auto kPollInterval = std::chrono::seconds(1);
constexpr wchar_t kCommandFile[] = L"\\cmd.txt";

std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_stallMs{0};  // net thread to game thread: the sleep the next tick takes
std::atomic<bool> g_lockRequested{false};  // net thread to game thread: set orphan pause bits at the next tick
Clock::time_point g_lastPoll;        // net thread only

// The text of cmd.txt, which is deleted as it is taken; nothing when there is no file or it cannot be removed.
std::optional<std::string> takeFile() {
    const std::wstring path = coopDirectory() + kCommandFile;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    if (!DeleteFileW(path.c_str())) {
        logger::writeUnlessRepeated("dev_command: cannot delete cmd.txt, ignored");
        return std::nullopt;
    }
    return text;
}

// The one place a command does something.
void run(const rule::Command& command) {
    switch (command.kind) {
        case rule::Kind::Stall:
            g_stallMs = command.stallMs;
            break;
        case rule::Kind::Lock:
            g_lockRequested = true;
            break;
        case rule::Kind::Rescue:
            rescue::press();
            break;
    }
}

// Leaves the pause bits a script that ended without clearing them would leave: the orphan lock the rescue must free.
void setOrphanLock() {
    const uintptr_t address = game::modeFlagsAddress();
    uint32_t flags = 0;
    if (!game_state::playing() || event_sync::scriptThreadActive() || !game::readMemory(address, flags)) {
        logger::write("dev_command: lock refused, not plain gameplay or a script runs");
        return;
    }
    constexpr uint32_t kLockBits = game::kTriggerPauseBit | game::kPlayerPauseBit;
    game::writeMemory(address, flags | kLockBits);
    logger::write("dev_command: lock, pause bits 0x%x set", kLockBits);
}

// Game thread, every tick: sleeps once for a stall command, after the frame thread's last beat, and sets a requested lock.
void onGameTick() {
    if (g_lockRequested.exchange(false)) setOrphanLock();
    const uint32_t stallMs = g_stallMs.exchange(0);
    if (!stallMs) return;
    logger::write("dev_command: stall %u ms", stallMs);
    Sleep(stallMs);
}

}  // namespace

namespace dev_command {

void enable() {
    g_enabled = true;
    game_tick::addCallback("dev_command", onGameTick);
    logger::write("dev_command: on, polling coop\\cmd.txt");
}

void onNetTick() {
    if (!g_enabled) return;
    const auto now = Clock::now();
    if (now - g_lastPoll < kPollInterval) return;
    g_lastPoll = now;
    const std::optional<std::string> text = takeFile();
    if (!text) return;
    const rule::Parsed parsed = rule::parse(*text);
    for (const std::string& line : parsed.refused) logger::write("dev_command: refused '%s'", line.c_str());
    for (const rule::Command& command : parsed.commands) run(command);
}

}  // namespace dev_command
