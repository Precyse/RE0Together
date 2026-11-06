#include "save_notice.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "log.h"
#include "protocol.h"
#include "save_service.h"
#include "session.h"

namespace {

constexpr uint64_t kSettleMs = 1500;  // after the last save hook: the SaveService has started writing by then
constexpr char kSaveName[] = "win64_save";

std::atomic<uint64_t> g_savedMs{0};  // when the newest save hook ran; 0: nothing waits

}  // namespace

namespace save_notice {

void onSaved() { g_savedMs = GetTickCount64(); }

void tick(uint64_t nowMs) {
    uint64_t saved = g_savedMs;
    if (!saved) return;
    if (!session::isHost() || !session::active()) {
        g_savedMs.compare_exchange_strong(saved, 0);
        return;
    }
    if (nowMs < saved + kSettleMs || !save_service::idle(save_service::instance())) return;
    if (!g_savedMs.compare_exchange_strong(saved, 0)) return;
    const bool sent = session::send(proto::kSaveChanged, true, {reinterpret_cast<const uint8_t*>(kSaveName), std::strlen(kSaveName)});
    logger::write("save_notice: save written, %s", sent ? "shared with the guests" : "not sent (link down)");
}

}  // namespace save_notice
