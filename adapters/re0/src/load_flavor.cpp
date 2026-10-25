#include "load_flavor.h"

#include <cstdint>

#include "game.h"
#include "hooks.h"
#include "load_flavor_rule.h"
#include "log.h"

namespace {

// sGameGUI::showCommonMessage(this, id, mode): the id is the script operand's byte.
using ShowCommon = void(__fastcall*)(void* gui, void* edx, uint32_t id, uint32_t mode);

constexpr uint32_t kOperandByteMask = 0xff;

ShowCommon g_original = nullptr;

void __fastcall showDetour(void* gui, void* edx, uint32_t id, uint32_t mode) {
    if (load_flavor_rule::skips(id & kOperandByteMask)) return;
    g_original(gui, edx, id, mode);
}

}  // namespace

namespace load_flavor {

bool install() {
    if (hooks::install("showCommonMessage", game::kShowCommonMessageFunction, reinterpret_cast<void*>(&showDetour),
                       reinterpret_cast<void**>(&g_original))) return true;
    logger::write("load_flavor: hook unavailable, the load-in lines show");
    return false;
}

}  // namespace load_flavor
