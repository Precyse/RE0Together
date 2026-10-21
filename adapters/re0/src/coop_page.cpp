#include "coop_page.h"

#include <cstdint>

#include "appearance.h"
#include "character_owner.h"
#include "coop_page_rule.h"
#include "coop_page_view.h"
#include "debug_stats.h"
#include "game.h"
#include "log.h"
#include "options_coop_rule.h"

namespace {

using MenuTriggerFunction = uint32_t(__cdecl*)();

// The trigger bits (pressed this frame) of pad, keyboard and mouse merged, the word every menu screen reads.
uint32_t menuTrigger() { return reinterpret_cast<MenuTriggerFunction>(game::kMenuTriggerFunction)(); }

// The pad's trigger bits for confirm or cancel (0 when the pad object is not there).
uint32_t padMask(uintptr_t offset) {
    const uintptr_t pad = game::readPointer(game::kGamePadGlobal);
    uint32_t mask = 0;
    if (pad) game::readMemory(pad + offset, mask);
    return mask;
}

void publishRows() {
    const character_owner::Character character = character_owner::plannedLocalCharacter();
    const coop_page_rule::Local local{character, appearance::outfitOf(character)};
    coop_page_view::publish(coop_page_rule::rows(debug_stats::snapshot(), local));
}

}  // namespace

namespace coop_page {

void open() {
    publishRows();
    logger::write("coop_page: opened");
}

bool update() {
    publishRows();
    return options_coop_rule::closesPage(menuTrigger(), padMask(game::kPadDecideMaskOffset),
                                         padMask(game::kPadCancelMaskOffset));
}

void close() {
    coop_page_view::clear();
    logger::write("coop_page: closed");
}

}  // namespace coop_page
