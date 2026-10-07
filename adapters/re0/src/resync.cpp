#include "resync.h"

#include <windows.h>

#include <chrono>
#include <string>

#include "character_owner.h"
#include "debug_overlay.h"
#include "join_sync.h"
#include "log.h"
#include "net_pad.h"
#include "paths.h"
#include "protocol.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kFilePollInterval = std::chrono::seconds(1);
constexpr float kToastSeconds = 2.5f;
constexpr wchar_t kTriggerFile[] = L"\\resync_now.txt";

NetClient* g_net = nullptr;
Clock::time_point g_lastPoll;  // net thread only

// True once per file: the trigger is deleted as it is taken.
bool takeTriggerFile() {
    const std::wstring path = coopDirectory() + kTriggerFile;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && DeleteFileW(path.c_str());
}

}  // namespace

namespace resync {

void request(const char* reason) {
    if (!net_pad::active() || !g_net) return;
    logger::write("resync: %s", reason);
    debug_overlay::toast("Resync", kToastSeconds);
    if (character_owner::isHost()) g_net->send(proto::kMsgResyncRequest, true, proto::kSlotAll, {});
    else join_sync::requestResync();
}

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgResyncRequest || !character_owner::isHostSlot(frame.slot) || character_owner::isHost()) {
        return;
    }
    logger::write("resync: the host asked for one");
    join_sync::requestResync();
}

void onNetTick() {
    const auto now = Clock::now();
    if (now - g_lastPoll < kFilePollInterval) return;
    g_lastPoll = now;
    if (takeTriggerFile()) request("resync_now.txt");
}

void enable(NetClient& net) { g_net = &net; }

}  // namespace resync
