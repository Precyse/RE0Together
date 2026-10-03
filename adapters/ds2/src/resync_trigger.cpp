#include "resync_trigger.h"

#include <windows.h>

#include <string>

#include "log.h"
#include "paths.h"
#include "resync.h"
#include "time_us.h"

namespace {

constexpr TimeUs kPollIntervalUs = 500'000;
constexpr wchar_t kTriggerFile[] = L"\\resync_now.txt";

TimeUs g_lastPoll = 0;

}  // namespace

namespace resync_trigger {

void poll(NetClient& net) {
    const TimeUs now = nowUs();
    if (now - g_lastPoll < kPollIntervalUs) return;
    g_lastPoll = now;
    if (!DeleteFileW((coopDirectory() + kTriggerFile).c_str())) return;  // succeeds only when the file was there
    logger::write("resync: manual request, asking every peer for its state and resending mine");
    resync::request(net, proto::kSlotAll, resync::kAll);
    resync::requestLocal(resync::kAll);
}

}  // namespace resync_trigger
