#include "overlay.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "crash_dump.h"
#include "game_state.h"
#include "imgui.h"
#include "log.h"
#include "peer_bodies.h"
#include "refw.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate_rule.h"

namespace {

constexpr uint64_t kRefreshMs = 250;
constexpr float kMarginPx = 16.0f;
constexpr float kBackgroundAlpha = 0.55f;
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_AlwaysAutoResize;

uint64_t g_lastRefreshMs = 0;

std::mutex g_mutex;
std::vector<std::string> g_lines;  // under g_mutex: written by the tick, copied by the draw
std::atomic<bool> g_faulted{false};

std::vector<std::string> buildLines() {
    std::vector<std::string> lines;
    for (const session::PeerView& peer : session::peers()) {
        const auto place = peer.place ? game_state_rule::placeOf(static_cast<world_gate_rule::Place>(peer.place->place))
                                      : game_state_rule::Place::Loading;  // no PLACE from them yet
        lines.push_back(game_state_rule::partnerLine(peer.name, peer.stale, place, peer_bodies::distance(peer.slot)));
    }
    if (const auto report = game_state::current()) {
        const auto host = session::hostPlace();
        const int hostSlot = host ? host->loadSlot : game_state_rule::kSlotUnknown;
        for (std::string& problem : game_state_rule::problems(*report, hostSlot)) lines.push_back(std::move(problem));
    }
    return lines;
}

// ImGui calls only: no object with a destructor lives here, so the fault guard around it is allowed.
void drawPanel(const std::vector<std::string>& lines) {
    ImGui::SetNextWindowPos(ImVec2(kMarginPx, kMarginPx));
    ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
    if (ImGui::Begin("##dd2coop_overlay", nullptr, kPanelFlags)) {
        for (const std::string& line : lines) ImGui::TextUnformatted(line.c_str());
    }
    ImGui::End();
}

void guardedDraw(const std::vector<std::string>& lines) {
    __try {
        drawPanel(lines);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "overlay")) {
        g_faulted = true;
    }
}

// REFramework's context is the one to draw into, with its allocator.
void onImGuiFrame(REFImGuiFrameCbData* data) {
    if (g_faulted || !data || !data->context) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(data->context));
    ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(data->malloc_fn), reinterpret_cast<ImGuiMemFreeFunc>(data->free_fn),
                                 data->user_data);
    std::vector<std::string> lines;
    {
        std::lock_guard lock(g_mutex);
        lines = g_lines;
    }
    if (!lines.empty()) guardedDraw(lines);
}

}  // namespace

namespace overlay {

bool install() {
    const bool registered = refw::param()->functions->on_imgui_frame(onImGuiFrame);
    logger::write("overlay: %s", registered ? "panel registered" : "not registered (REFramework refused the ImGui callback)");
    return registered;
}

void tick(uint64_t nowMs) {
    if (nowMs - g_lastRefreshMs < kRefreshMs) return;
    g_lastRefreshMs = nowMs;
    std::vector<std::string> lines = buildLines();
    std::lock_guard lock(g_mutex);
    g_lines = std::move(lines);
}

}  // namespace overlay
