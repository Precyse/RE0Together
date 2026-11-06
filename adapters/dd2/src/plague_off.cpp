#include "plague_off.h"

#include <atomic>

#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kThis = 1;
constexpr int kFirstParam = 2;
constexpr uint32_t kPossessionLv = 0x90;  // PawnDataContext._PossessionLv
constexpr uint64_t kReportMs = 60000;
constexpr uint64_t kNoOutbreak = 0;              // judgPossession: false
constexpr uint64_t kNoneKilled = 0xFFFFFFFF;     // killNPC_Possession: its own "nothing found" result (-1)

std::atomic<uint32_t> g_refused{0};
uint32_t g_reported = 0;
uint64_t g_lastReportMs = 0;
thread_local bool t_skippedJudge = false, t_skippedKill = false;

int refuse() {
    g_refused.fetch_add(1, std::memory_order_relaxed);
    return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
}

int preAlwaysRefuse(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long) { return refuse(); }

// setPossessionLv: refused only when it would raise the level (lowering, as a cure does, stays).
int preSetLv(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc <= kFirstParam || !argv[kThis]) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    const int32_t wanted = static_cast<int32_t>(reinterpret_cast<uintptr_t>(argv[kFirstParam]));
    return wanted > refw::field<int32_t>(argv[kThis], kPossessionLv) ? refuse() : REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preJudge(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    t_skippedJudge = true;
    return refuse();
}

void postJudge(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (t_skippedJudge && retVal) *reinterpret_cast<uint64_t*>(retVal) = kNoOutbreak;
    t_skippedJudge = false;
}

int preKill(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    t_skippedKill = true;
    return refuse();
}

void postKill(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (t_skippedKill && retVal) *reinterpret_cast<uint64_t*>(retVal) = kNoneKilled;
    t_skippedKill = false;
}

}  // namespace

namespace plague_off {

bool install() {
    const hook_registry::Handler handlers[] = {
        hook_registry::make<preAlwaysRefuse>(&names::kAddPossessionPoint),
        hook_registry::make<preSetLv>(&names::kSetPossessionLv),
        hook_registry::make<preAlwaysRefuse>(&names::kPawnPossessionLv),
        hook_registry::make<preAlwaysRefuse>(&names::kMainPawnPossessionLv),
        hook_registry::make<preJudge, postJudge>(&names::kJudgePossession),
        hook_registry::make<preKill, postKill>(&names::kKillPossessed),
    };
    return hook_registry::installSet(names::kPlague, handlers);
}

void tick(uint64_t nowMs) {
    const uint32_t refused = g_refused.load(std::memory_order_relaxed);
    if (refused == g_reported || nowMs - g_lastReportMs < kReportMs) return;
    logger::write("plague_off: %u dragonsplague steps refused (%u new)", refused, refused - g_reported);
    g_reported = refused;
    g_lastReportMs = nowMs;
}

}  // namespace plague_off
