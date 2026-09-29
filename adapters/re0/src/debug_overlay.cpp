#include "debug_overlay.h"

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx9.h"
#include "d3d9_hook.h"
#include "game.h"
#include "debug_lines.h"
#include "debug_stats.h"
#include "imgui.h"
#include "log.h"
#include "toast_queue.h"
#include "window_focus.h"

namespace {

constexpr int kToggleKey = VK_F8;
constexpr float kMargin = 8.0f;
constexpr float kToastGap = 4.0f;
constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr ImVec4 kPanelColor{0.04f, 0.04f, 0.04f, 0.63f};
constexpr ImVec4 kTextColor{0.78f, 0.78f, 0.78f, 1.0f};
constexpr ImVec4 kBadColor{0.88f, 0.31f, 0.31f, 1.0f};
constexpr char kModTitle[] = "RE0 Together";
constexpr float kTitleScale = 2.0f;
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

std::atomic<uint32_t> g_frames{0};
std::atomic<uint32_t> g_drawn{0};
std::atomic<bool> g_disabled{false};
std::atomic<bool> g_visible{false};
bool g_keyWasDown = false;  // render thread only, like everything below

IDirect3DDevice9* g_device = nullptr;  // the device the ImGui backend is bound to; null until first shown

void pollToggle() {
    const bool down = (GetAsyncKeyState(kToggleKey) & 0x8000) != 0;
    if (down && !g_keyWasDown && gameIsForeground()) g_visible = !g_visible;
    g_keyWasDown = down;
}

// Only the backbuffer pass gets the panel; EndScene also ends offscreen passes.
bool backbufferSize(IDirect3DDevice9* device, ImVec2& size) {
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* backbuffer = nullptr;
    device->GetRenderTarget(0, &target);
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    D3DSURFACE_DESC desc{};
    const bool same = target && target == backbuffer && SUCCEEDED(backbuffer->GetDesc(&desc));
    if (target) target->Release();
    if (backbuffer) backbuffer->Release();
    size = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
    return same;
}

std::string narrow(const std::wstring& text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

void shutdownImGui() {
    if (!g_device) return;
    ImGui_ImplDX9_Shutdown();
    ImGui::DestroyContext();
    g_device = nullptr;
}

void styleImGui() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = kPanelColor;
    style.Colors[ImGuiCol_Text] = kTextColor;
}

bool ensureImGui(IDirect3DDevice9* device) {
    if (g_device == device) return true;
    shutdownImGui();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = false;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    styleImGui();
    if (!ImGui_ImplDX9_Init(device)) {
        ImGui::DestroyContext();
        return false;
    }
    g_device = device;
    return true;
}

void drawLines() {
    const auto lines = debug_lines::build(debug_stats::snapshot());
    const float charWidth = ImGui::CalcTextSize("0").x;
    if (!ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, charWidth * debug_lines::kLabelChars);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, charWidth * debug_lines::kValueChars);
    for (const auto& line : lines) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(narrow(line.label).c_str());
        ImGui::TableSetColumnIndex(1);
        if (line.bad) ImGui::PushStyleColor(ImGuiCol_Text, kBadColor);
        ImGui::TextUnformatted(narrow(line.value).c_str());
        if (line.bad) ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

// Stacked top-center boxes; a fading toast dims through the style alpha.
void drawToasts(const std::vector<toast_queue::Visible>& toasts, float displayWidth) {
    float y = kMargin;
    for (size_t i = 0; i < toasts.size(); ++i) {
        ImGui::SetNextWindowPos(ImVec2(displayWidth * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, toasts[i].alpha);
        if (ImGui::Begin(("toast" + std::to_string(i)).c_str(), nullptr, kPanelFlags)) {
            ImGui::TextUnformatted(toasts[i].text.c_str());
            y += ImGui::GetWindowHeight() + kToastGap;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

// Mod name in the bottom-right corner while no player is loaded (title screen and main menu).
void drawTitleMark(const ImVec2& display) {
    ImGui::SetNextWindowPos(ImVec2(display.x - kMargin, display.y - kMargin), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    if (ImGui::Begin("title", nullptr, kPanelFlags | ImGuiWindowFlags_NoBackground)) {
        ImGui::SetWindowFontScale(kTitleScale);
        ImGui::TextUnformatted(kModTitle);
    }
    ImGui::End();
}

void drawFrame(IDirect3DDevice9* device, const std::vector<toast_queue::Visible>& toasts, bool showTitle) {
    ImVec2 size;
    if (!backbufferSize(device, size) || !ensureImGui(device)) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = size;
    io.DeltaTime = kFrameSeconds;
    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    if (g_visible) {
        ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin));
        if (ImGui::Begin("status", nullptr, kPanelFlags)) drawLines();
        ImGui::End();
    }
    if (showTitle) drawTitleMark(size);
    drawToasts(toasts, size.x);
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    ++g_drawn;
}

void frame(IDirect3DDevice9* device) {
    pollToggle();
    const auto toasts = toast_queue::visible();
    const bool showTitle = game::controlled() == 0;
    if (g_visible || showTitle || !toasts.empty()) drawFrame(device, toasts, showTitle);
}

void invalidate(IDirect3DDevice9*) {
    if (g_device) ImGui_ImplDX9_InvalidateDeviceObjects();
}

void recreate(IDirect3DDevice9*) {
    if (g_device) ImGui_ImplDX9_CreateDeviceObjects();
}

// Runs an overlay step; a fault disables the overlay for the session instead of taking the game down.
void guarded(void (*step)(IDirect3DDevice9*), IDirect3DDevice9* device) {
    if (g_disabled) return;
    __try {
        step(device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
        logger::write("overlay: exception 0x%08lx, overlay disabled for this session", GetExceptionCode());
        debug_stats::setError("overlay disabled after a fault");
    }
}

void onEndScene(IDirect3DDevice9* device) {
    ++g_frames;
    guarded(frame, device);
}

void onBeforeReset() { guarded(invalidate, nullptr); }

void onAfterReset() { guarded(recreate, nullptr); }

}  // namespace

namespace debug_overlay {

bool install() { return d3d9_hook::install({onEndScene, onBeforeReset, onAfterReset}); }

void uninstall() { d3d9_hook::uninstall(); }

uint32_t framesSeen() { return g_frames.load(); }

uint32_t framesDrawn() { return g_drawn.load(); }

bool disabled() { return g_disabled.load(); }

void setVisible(bool visible) { g_visible = visible; }

void toast(const char* text, float seconds) { toast_queue::push(text, seconds); }

}  // namespace debug_overlay
