#include "resync_trigger.h"

#include <windows.h>

#include <string>

#include "log.h"
#include "paths.h"
#include "resync.h"
#include "time_us.h"
#include "toast_queue.h"

namespace {

constexpr TimeUs kPollIntervalUs = 500'000;
constexpr wchar_t kTriggerFile[] = L"\resync_now.txt";
constexpr int kResyncKey = VK_F9;
constexpr SHORT kKeyDown = static_cast<SHORT>(0x8000);
constexpr float kToastSeconds = 2.0f;

TimeUs g_lastPoll = 0;
bool g_keyWasDown = false;

void requestResync(NetClient& net, const char* source) {
    logger::write("resync: manual request (%s), asking every peer for its state and resending mine", source);
    resync::request(net, proto::kSlotAll, resync::kAll);
    resync::requestLocal(resync::kAll);
    toast_queue::push("Resync requested", kToastSeconds);
}

// True on the frame the key goes down, and only while the game's window has focus.
bool keyPressedInGame() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(kResyncKey) & kKeyDown) != 0;
    const bool edge = down && !g_keyWasDown;
    g_keyWasDown = down;
    return edge;
}

}  // namespace

namespace resync_trigger {

void poll(NetClient& net) {
    if (keyPressedInGame()) requestResync(net, "F9");
    const TimeUs now = nowUs();
    if (now - g_lastPoll < kPollIntervalUs) return;
    g_lastPoll = now;
    if (DeleteFileW((coopDirectory() + kTriggerFile).c_str())) requestResync(net, "resync_now.txt");  // succeeds only when the file was there
}

}  // namespace resync_trigger
