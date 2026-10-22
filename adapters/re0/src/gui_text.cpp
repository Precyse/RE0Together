#include "gui_text.h"

#include <array>
#include <cstdint>

#include "game.h"
#include "gui_text_rule.h"
#include "hooks.h"
#include "log.h"

namespace {

// mov edx, [esp+4]; cmp [ecx+0x88], edx: the lookup's first two instructions (thiscall table in ECX, id on the stack).
constexpr std::array<uint8_t, 10> kSiteBytes = {0x8b, 0x54, 0x24, 0x04, 0x39, 0x91, 0x88, 0x00, 0x00, 0x00};

using GetStringFunction = const char*(__fastcall*)(void* table, void* edx, uint32_t id);

GetStringFunction g_original = nullptr;

// The message table slot of a table object, or -1 for one that is not a slot's table.
int slotOf(const void* table) {
    const uintptr_t gui = game::readPointer(game::kGameGuiGlobal);
    if (!gui || !table) return -1;
    for (int slot = 0; slot < game::kGuiTableSlots; ++slot) {
        const uintptr_t slotTable = game::readPointer(gui + game::kGuiTablesOffset + slot * sizeof(uint32_t));
        if (slotTable == reinterpret_cast<uintptr_t>(table)) return slot;
    }
    return -1;
}

const char* __fastcall getStringDetour(void* table, void* edx, uint32_t id) {
    const int slot = slotOf(table);
    const char* text = slot < 0 ? nullptr : gui_text_rule::find(slot, id);
    return text ? text : g_original(table, edx, id);
}

}  // namespace

namespace gui_text {

bool enable() {
    if (!game::bytesMatch(game::kGetStringFunction, kSiteBytes)) {
        logger::write("gui_text: the text lookup at 0x%x is not the expected code, text left alone",
                      static_cast<unsigned>(game::kGetStringFunction));
        return false;
    }
    return hooks::install("rGUIMessage::getString", game::kGetStringFunction, reinterpret_cast<void*>(&getStringDetour),
                          reinterpret_cast<void**>(&g_original));
}

void uninstall() { hooks::remove(game::kGetStringFunction); }

}  // namespace gui_text
