#include "title_load.h"

#include <windows.h>

#include <atomic>
#include <optional>
#include <string>

#include "calls.h"
#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"
#include "save_redirect.h"
#include "save_service.h"
#include "title_load_rule.h"
#include "world_gate.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`.
constexpr int kThisParam = 1;

// The title GUI updated within this long ago: it is up.
constexpr uint64_t kGuiSeenMs = 300;

constexpr int32_t kNoFlow = INT32_MIN;

constexpr const char* kGuiType = "app.ui030201";
constexpr const char* kFlowField = "FlowNow";
constexpr const char* kFlowEnum = "app.ui030201.FlowId";
constexpr const char* kKeyWaitLiteral = "Title";  // the title screen waiting for a key
constexpr const char* kChoiceWaitLiteral = "SelectMenu";  // the rows shown, waiting for a choice
constexpr const char* kStartEnum = "app.MainFlowManager.StartType";
constexpr const char* kContinueLiteral = "Latest";
constexpr const char* kMainFlowManager = "app.MainFlowManager";

title_load_rule::Loader g_loader;
bool g_armed = false;
int32_t g_keyWaitFlow = kNoFlow, g_choiceWaitFlow = kNoFlow, g_continueType = kNoFlow;
std::atomic<int32_t> g_flow{kNoFlow};   // the title GUI's FlowNow at its newest update
std::atomic<uint64_t> g_seenMs{0};      // when the title GUI last updated
std::atomic<game_state_rule::Load> g_result{game_state_rule::Load::None};

int32_t literal(const char* type, const char* name) {
    const std::optional<int64_t> value = refw::enumLiteral(type, name);
    return value ? static_cast<int32_t>(*value) : kNoFlow;
}

// Only watches: the title GUI runs as it always does.
int preUpdate(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc <= kThisParam) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    int32_t flow = kNoFlow;
    if (!refw::instanceField(kGuiType, kFlowField, argv[kThisParam], flow)) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    g_flow = flow;
    g_seenMs = GetTickCount64();
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

bool guiWaiting(uint64_t nowMs) {
    const int32_t flow = g_flow;
    return nowMs - g_seenMs <= kGuiSeenMs && (flow == g_keyWaitFlow || flow == g_choiceWaitFlow);
}

// The game's own load: MainFlowManager.requestStartType(Latest), as the menu's tail makes it. False when the call was refused.
bool requestContinue() {
    void* flow = refw::managedSingleton(kMainFlowManager);
    if (!flow) return false;
    return refw::invoke(calls::method(calls::kRequestStartType), flow, {reinterpret_cast<void*>(static_cast<intptr_t>(g_continueType))}) != nullptr;
}

void onAction(title_load_rule::Action action, const std::string& phase) {
    switch (action) {
        case title_load_rule::Action::Request:
            logger::write("title_load: Continue requested (try %d, phase %s)", g_loader.tries(), phase.c_str());
            if (!requestContinue()) {
                logger::write("title_load: the game refused the request");
                onAction(g_loader.refused(), phase);
            }
            return;
        case title_load_rule::Action::Succeeded:
            g_result = game_state_rule::Load::Ok;
            logger::write("title_load: the load went through (phase %s)", phase.c_str());
            return;
        case title_load_rule::Action::GaveUp:
            g_result = game_state_rule::Load::Failed;
            logger::write("title_load: the session's save did not load (%d tries), giving up", g_loader.tries());
            return;
        case title_load_rule::Action::None: return;
    }
}

}  // namespace

namespace title_load {

bool install() {
    g_keyWaitFlow = literal(kFlowEnum, kKeyWaitLiteral);
    g_choiceWaitFlow = literal(kFlowEnum, kChoiceWaitLiteral);
    g_continueType = literal(kStartEnum, kContinueLiteral);
    if (!calls::enabled(calls::Feature::Title) || g_keyWaitFlow == kNoFlow || g_choiceWaitFlow == kNoFlow || g_continueType == kNoFlow) {
        logger::write("title_load: off (title calls or enum literals missing)");
        return false;
    }
    const hook_registry::Handler handlers[] = {hook_registry::make<preUpdate>(&names::kTitleUpdate)};
    g_armed = hook_registry::installSet(names::kTitleLoad, handlers);
    return g_armed;
}

void tick(uint64_t nowMs) {
    if (!g_armed || g_loader.finished()) return;
    const std::string phase = world_gate::phase();
    const title_load_rule::Phase kind = title_load_rule::classify(phase);
    const title_load_rule::Title title{kind == title_load_rule::Phase::Title, guiWaiting(nowMs),
                                       save_service::idle(save_service::instance()), save_redirect::safeToLoad()};
    onAction(g_loader.step(nowMs, title_load_rule::ready(title), kind), phase);
}

game_state_rule::Load result() { return g_result; }

}  // namespace title_load
