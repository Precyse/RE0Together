#include "world_gate.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kFirstParam = 2;

std::unordered_map<int64_t, std::string> g_phases, g_scenes;  // written by install only
std::mutex g_mutex;
world_gate_rule::Gate g_gate;  // under g_mutex
std::atomic<DWORD> g_gameThread{0};
std::atomic<uint32_t> g_offThread{0};

thread_local int64_t t_pendingPhase = -1;
thread_local int64_t t_pendingScene = -1;

std::unordered_map<int64_t, std::string> literals(const char* type) {
    std::unordered_map<int64_t, std::string> out;
    for (auto& [name, value] : refw::enumLiterals(type)) out.emplace(value, std::move(name));
    return out;
}

const std::string& nameOf(const std::unordered_map<int64_t, std::string>& table, int64_t value) {
    static const std::string unknown = "?";
    const auto it = table.find(value);
    return it == table.end() ? unknown : it->second;
}

// Rule T1: the gate is the game thread's; a call from another thread is counted and ignored.
bool onGameThread() {
    const DWORD game = g_gameThread.load(std::memory_order_relaxed);
    if (!game || game == GetCurrentThreadId()) return true;
    g_offThread.fetch_add(1, std::memory_order_relaxed);
    return false;
}

int64_t param(int argc, void** argv, int index) {
    return argc > index ? static_cast<int32_t>(reinterpret_cast<uintptr_t>(argv[index])) : -1;
}

void logCause(world_gate_rule::Cause cause, const char* what) {
    if (cause == world_gate_rule::Cause::None) return;
    logger::write("world_gate: load serial %u (%s, %s)", g_gate.loadSerial(), world_gate_rule::name(cause), what);
}

int prePhase(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    t_pendingPhase = param(argc, argv, kFirstParam);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void postPhase(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (!onGameThread()) return;
    const std::string& phase = nameOf(g_phases, t_pendingPhase);
    std::lock_guard lock(g_mutex);
    logger::write("world_gate: phase %s (%lld)", phase.c_str(), static_cast<long long>(t_pendingPhase));
    logCause(g_gate.phaseChanged(phase), phase.c_str());
}

int preScene(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    t_pendingScene = param(argc, argv, kFirstParam);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void postScene(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (!onGameThread()) return;
    const std::string& scene = nameOf(g_scenes, t_pendingScene);
    std::lock_guard lock(g_mutex);
    logCause(g_gate.sceneChanged(scene), scene.c_str());
}

void postSceneLoad(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (!onGameThread()) return;
    std::lock_guard lock(g_mutex);
    g_gate.sceneLoadCompleted();
    logger::write("world_gate: scene load complete at load serial %u", g_gate.loadSerial());
}

void postFerrystone(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (!onGameThread()) return;
    std::lock_guard lock(g_mutex);
    logCause(g_gate.ferrystoneStarted(), "startLoad");
}

}  // namespace

namespace world_gate {

bool install() {
    g_phases = literals(names::kPhaseEnum);
    g_scenes = literals(names::kSceneTypeEnum);
    logger::write("world_gate: %zu phases, %zu scene types", g_phases.size(), g_scenes.size());
    if (g_phases.empty() || g_scenes.empty()) {
        logger::write("%s: not hooked (enum literals missing)", names::kWorldGate);
        return false;
    }
    const hook_registry::Handler handlers[] = {
        {&names::kChangeFlowPhase, prePhase, postPhase},
        {&names::kChangeSceneType, preScene, postScene},
        {&names::kCompleteSceneLoad, nullptr, postSceneLoad},
        {&names::kFerrystoneLoad, nullptr, postFerrystone},
    };
    return hook_registry::installSet(names::kWorldGate, handlers);
}

world_gate_rule::Place place() {
    std::lock_guard lock(g_mutex);
    return g_gate.place();
}

uint16_t loadSerial() {
    std::lock_guard lock(g_mutex);
    return g_gate.loadSerial();
}

void setGameThread(unsigned long threadId) { g_gameThread.store(threadId, std::memory_order_relaxed); }

}  // namespace world_gate
