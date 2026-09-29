#include "input_redirect.h"

#include "character_owner.h"
#include "game.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"

namespace {

using GetPadFunction = void*(__fastcall*)(void* self, void* edx, uint32_t index);
using AnalogFunction = void*(__fastcall*)(void* self, void* edx);

constexpr size_t kAnalogBytes = 0x40;  // bytes of the analog block the player think reads

GetPadFunction g_originalGetPad = nullptr;
AnalogFunction g_originalAnalog = nullptr;
// Input source for the character whose update is running on this thread.
enum class Route : uint8_t { Real, Remote, Blocked };
thread_local Route t_route = Route::Real;
uint8_t g_neutralAnalog[kAnalogBytes] = {};  // an untouched stick

void* gamePad() { return reinterpret_cast<void*>(game::readPointer(game::kGamePadGlobal)); }

Route routeFor(uintptr_t player) {
    switch (character_owner::controlOf(character_owner::identify(player))) {
        case character_owner::Control::Remote: return Route::Remote;
        case character_owner::Control::Locked: return Route::Blocked;
        default: return Route::Real;
    }
}

void onPlayerMove(uintptr_t player, bool entering) { t_route = entering ? routeFor(player) : Route::Real; }

void* __fastcall getPadDetour(void* self, void* edx, uint32_t index) {
    void* pad = g_originalGetPad(self, edx, index);
    if (!game::isRealPad(pad)) return pad;
    if (t_route == Route::Remote) return net_pad::object(pad);
    if (t_route == Route::Blocked) return static_cast<uint8_t*>(self) + game::kBlockedPadOffset;
    return pad;
}

void* __fastcall analogDetour(void* self, void* edx) {
    if (t_route == Route::Remote) return net_pad::analog();
    if (t_route == Route::Blocked) return g_neutralAnalog;
    return g_originalAnalog(self, edx);
}

}  // namespace

namespace input_redirect {

bool install() {
    if (!hooks::install("sGamePad::getPad", game::kGetPadFunction, reinterpret_cast<void*>(&getPadDetour),
                        reinterpret_cast<void**>(&g_originalGetPad)) ||
        !hooks::install("sGamePad::analog", game::kAnalogGetterFunction, reinterpret_cast<void*>(&analogDetour),
                        reinterpret_cast<void**>(&g_originalAnalog))) {
        return false;
    }
    game_tick::setMoveScope(onPlayerMove);
    return true;
}

void uninstall() {
    hooks::remove(game::kGetPadFunction);
    hooks::remove(game::kAnalogGetterFunction);
}

bool replayingRemoteInput() { return t_route == Route::Remote; }

void* realPad(uint32_t index) {
    void* self = gamePad();
    return self && g_originalGetPad ? g_originalGetPad(self, nullptr, index) : nullptr;
}

void* realAnalog() {
    void* self = gamePad();
    return self && g_originalAnalog ? g_originalAnalog(self, nullptr) : nullptr;
}

}  // namespace input_redirect
