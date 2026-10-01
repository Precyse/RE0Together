#include "cargo_menu.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "cargo_transfer.h"
#include "imgui.h"
#include "player_sync.h"

namespace {

constexpr int kToggleKey = VK_F7;
constexpr size_t kVirtualKeys = 256;
constexpr SHORT kKeyDown = static_cast<SHORT>(0x8000);

constexpr float kLeft = 60.0f;
constexpr float kTopFraction = 0.22f;  // of the screen height
constexpr float kColumnWidth = 360.0f;
constexpr float kArrowGap = 56.0f;
constexpr float kRowHeight = 30.0f;
constexpr float kTextSize = 20.0f;
constexpr float kPadding = 10.0f;
constexpr float kArrowHalf = 14.0f;
constexpr float kLine = 1.0f;
constexpr ImU32 kPanel = IM_COL32(0, 0, 0, 200);
constexpr ImU32 kSelected = IM_COL32(95, 95, 95, 235);
constexpr ImU32 kText = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kHeaderText = IM_COL32(160, 160, 160, 255);
constexpr ImU32 kSeparator = IM_COL32(110, 110, 110, 255);
constexpr ImU32 kArrow = IM_COL32(235, 235, 235, 255);

enum Column { kMine = 0, kTheirs = 1, kColumns = 2 };

std::atomic<bool> g_open{false};  // read by input_filter on the game's window thread
int g_column = kMine;
int g_row = 0;
std::array<bool, kVirtualKeys> g_wasDown{};

bool gameInFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// True once per press.
bool pressed(int key) {
    const bool down = (GetAsyncKeyState(key) & kKeyDown) != 0;
    const bool edge = down && !g_wasDown[key];
    g_wasDown[key] = down;
    return edge;
}

std::string partnerName(uint8_t slot) {
    for (const player_sync::RemotePlayer& peer : player_sync::remotePlayers()) {
        if (peer.slot == slot) return peer.name;
    }
    return "Player " + std::to_string(slot);
}

void handleKeys(const std::array<std::vector<game::Cargo>, kColumns>& lists) {
    const bool front = gameInFront();
    const auto hit = [front](int key) { return pressed(key) && front; };  // every key polled each frame
    const bool toggle = hit(kToggleKey), up = hit(VK_UP), down = hit(VK_DOWN), move = hit(VK_RETURN);
    const bool side = hit(VK_LEFT) | hit(VK_RIGHT);
    if (toggle) g_open = !g_open.load();
    if (!g_open) return;
    if (side) g_column = 1 - g_column;
    const int rows = static_cast<int>(lists[g_column].size());
    if (up) --g_row;
    if (down) ++g_row;
    g_row = std::clamp(g_row, 0, std::max(rows - 1, 0));
    if (!move || rows == 0) return;
    const game::Cargo& piece = lists[g_column][g_row];
    if (g_column == kMine) {
        cargo_transfer::give(piece);
    } else {
        cargo_transfer::take(piece);
    }
}

void drawColumn(ImDrawList* list, float x, float top, const std::string& title, const std::vector<game::Cargo>& cargo,
                int selected) {
    ImFont* font = ImGui::GetFont();
    list->AddText(font, kTextSize, ImVec2(x + kPadding, top + (kRowHeight - kTextSize) * 0.5f), kHeaderText,
                  title.c_str());
    const float rowsTop = top + kRowHeight;
    list->AddLine(ImVec2(x, rowsTop), ImVec2(x + kColumnWidth, rowsTop), kSeparator, kLine);
    for (size_t i = 0; i < cargo.size(); ++i) {
        const float y = rowsTop + i * kRowHeight;
        if (static_cast<int>(i) == selected) {
            list->AddRectFilled(ImVec2(x, y), ImVec2(x + kColumnWidth, y + kRowHeight), kSelected);
        }
        list->AddText(font, kTextSize, ImVec2(x + kPadding, y + (kRowHeight - kTextSize) * 0.5f), kText,
                      cargo[i].name.c_str());
    }
}

// The direction the selected piece would move: from the chosen column toward the other one.
void drawArrow(ImDrawList* list, float centreX, float y, bool toRight) {
    const float tip = toRight ? kArrowHalf : -kArrowHalf;
    list->AddTriangleFilled(ImVec2(centreX - tip, y - kArrowHalf), ImVec2(centreX + tip, y),
                            ImVec2(centreX - tip, y + kArrowHalf), kArrow);
}

}  // namespace

namespace cargo_menu {

void draw(float, float height) {
    const auto partner = cargo_transfer::isHost() ? cargo_transfer::partner() : std::nullopt;
    if (!partner) {
        g_open = false;
        return;
    }
    const std::array<std::vector<game::Cargo>, kColumns> lists = {cargo_transfer::localCargo(), partner->cargo};
    handleKeys(lists);
    if (!g_open) return;

    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const float top = height * kTopFraction;
    const size_t rows = std::max(lists[kMine].size(), lists[kTheirs].size());
    const float panelHeight = kRowHeight * (rows + 1);
    const float theirsX = kLeft + kColumnWidth + kArrowGap;
    list->AddRectFilled(ImVec2(kLeft, top), ImVec2(theirsX + kColumnWidth, top + panelHeight), kPanel);
    list->AddLine(ImVec2(kLeft + kColumnWidth, top), ImVec2(kLeft + kColumnWidth, top + panelHeight), kSeparator, kLine);
    list->AddLine(ImVec2(theirsX, top), ImVec2(theirsX, top + panelHeight), kSeparator, kLine);
    drawColumn(list, kLeft, top, "You", lists[kMine], g_column == kMine ? g_row : -1);
    drawColumn(list, theirsX, top, partnerName(partner->slot), lists[kTheirs], g_column == kTheirs ? g_row : -1);
    if (!lists[g_column].empty()) {
        drawArrow(list, kLeft + kColumnWidth + kArrowGap * 0.5f, top + kRowHeight * (g_row + 1.5f), g_column == kMine);
    }
}

bool claimsKey(unsigned virtualKey) {
    if (!g_open.load()) return false;
    switch (virtualKey) {
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_RETURN:
            return true;
        default:
            return false;
    }
}

}  // namespace cargo_menu
