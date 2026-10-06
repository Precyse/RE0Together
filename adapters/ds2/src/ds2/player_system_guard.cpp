#include "ds2/player_system_guard.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_camera.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kPlayerSystemGlobal = 0x14623E9C8;
constexpr uintptr_t kComponentOwner = 0x48;
constexpr size_t kScratchSize = 0x227000;  // past the last singleton the two inits store (+0x226618)

struct Handler {
    const char* name;
    uintptr_t begin, end;  // file VA range of the function, scanned for loads of the system pointer
};
constexpr Handler kEquipmentInit = {"equipment manager init", 0x140F69970, 0x140F6A6D4};
constexpr uintptr_t kEquipmentUpdate = 0x140F6B9B0;  // the component's per-frame handler (component, message with the time step)
constexpr Handler kFacialRigInit = {"facial rig init", 0x140E86A80, 0x140E86CBD};

// `mov rax, [rip + disp32]`: the load of the system pointer in front of every singleton store.
constexpr uint8_t kLoadRaxRip[] = {0x48, 0x8B, 0x05};
constexpr size_t kLoadSize = 7;
constexpr size_t kDispOffset = sizeof(kLoadRaxRip);
constexpr int64_t kDispRange = INT32_MAX;
constexpr uintptr_t kNearStep = 0x10000;  // allocation granularity
constexpr uintptr_t kNearLimit = 0x60000000;

using HandlerFn = void (*)(uintptr_t component, uintptr_t message);
HandlerFn g_equipmentInit = nullptr, g_facialRigInit = nullptr, g_equipmentUpdate = nullptr;

// What the patched loads read: Sam's DSPlayerSystem, or a scratch block for the remote's init, whose stores
// then land nowhere. Both live in one block near the game image so the loads can reach the cell.
uintptr_t* g_cell = nullptr;
uint8_t* g_scratch = nullptr;

// Reserves memory below the game image (a rip-relative load reaches +-2 GB).
uint8_t* allocateNearImage(size_t size) {
    const uintptr_t image = ds2::at(0x140000000);
    for (uintptr_t offset = kNearStep; offset < kNearLimit; offset += kNearStep) {
        void* block = VirtualAlloc(reinterpret_cast<void*>(image - offset), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (block) return static_cast<uint8_t*>(block);
    }
    return nullptr;
}

// Points every `mov rax, [system pointer]` in [begin, end) at the cell. Returns how many it patched.
int redirectSystemLoads(const Handler& handler) {
    const uintptr_t global = ds2::at(kPlayerSystemGlobal);
    int patched = 0;
    for (uintptr_t site = ds2::at(handler.begin); site < ds2::at(handler.end) - kLoadSize; ++site) {
        const auto* code = reinterpret_cast<const uint8_t*>(site);
        int32_t disp = 0;
        std::memcpy(&disp, code + kDispOffset, sizeof(disp));
        if (std::memcmp(code, kLoadRaxRip, sizeof(kLoadRaxRip)) != 0 || site + kLoadSize + disp != global) continue;
        const int64_t toCell = reinterpret_cast<intptr_t>(g_cell) - static_cast<int64_t>(site + kLoadSize);
        if (toCell > kDispRange || toCell < -kDispRange) return -1;
        const auto newDisp = static_cast<int32_t>(toCell);
        DWORD protection = 0;
        VirtualProtect(const_cast<uint8_t*>(code) + kDispOffset, sizeof(newDisp), PAGE_EXECUTE_READWRITE, &protection);
        std::memcpy(const_cast<uint8_t*>(code) + kDispOffset, &newDisp, sizeof(newDisp));
        FlushInstructionCache(GetCurrentProcess(), code, kLoadSize);
        VirtualProtect(const_cast<uint8_t*>(code) + kDispOffset, sizeof(newDisp), protection, &protection);
        ++patched;
    }
    return patched;
}

bool ownedByRemote(uintptr_t component) {
    const uintptr_t remote = remote_player::entity();
    return remote && ds2::field<uintptr_t>(component, kComponentOwner) == remote;
}

// The remote's components are the ones created while this thread spawns it, or any component whose entity is the remote.
bool belongsToRemote(uintptr_t component) { return remote_camera::spawning() || ownedByRemote(component); }

void runInit(HandlerFn original, uintptr_t component, uintptr_t message) {
    *g_cell = belongsToRemote(component) ? reinterpret_cast<uintptr_t>(g_scratch)
                                         : decima::readPointer(ds2::at(kPlayerSystemGlobal));
    original(component, message);
}

void equipmentInit(uintptr_t component, uintptr_t message) { runInit(g_equipmentInit, component, message); }
void facialRigInit(uintptr_t component, uintptr_t message) { runInit(g_facialRigInit, component, message); }

// The per-frame update reads the player state's equipment records (state +0x56e0 -> +0xce8), which the remote's state does
// not have (-1 there, a read fault at 0x140F75E12); the body's gear comes from equip_sync.
void equipmentUpdate(uintptr_t component, uintptr_t message) {
    if (!ownedByRemote(component)) g_equipmentUpdate(component, message);
}

// Hooks the init first (it sets the cell before every run), then points its loads at the cell.
void install(const Handler& handler, void* detour, HandlerFn* original) {
    if (!hooks::install(handler.name, ds2::at(handler.begin), detour, reinterpret_cast<void**>(original))) return;
    logger::write("player_system_guard: %s: %d loads of the system pointer redirected", handler.name, redirectSystemLoads(handler));
}

}  // namespace

namespace player_system_guard {


void installEarly() {
    uint8_t* block = allocateNearImage(kScratchSize + sizeof(uintptr_t));
    if (!block) {
        logger::write("player_system_guard: no memory near the game image");
        return;
    }
    g_cell = reinterpret_cast<uintptr_t*>(block);
    g_scratch = block + sizeof(uintptr_t);
    *g_cell = decima::readPointer(ds2::at(kPlayerSystemGlobal));
    install(kEquipmentInit, reinterpret_cast<void*>(&equipmentInit), &g_equipmentInit);
    install(kFacialRigInit, reinterpret_cast<void*>(&facialRigInit), &g_facialRigInit);
    hooks::install("equipment manager update", ds2::at(kEquipmentUpdate), reinterpret_cast<void*>(&equipmentUpdate),
                   reinterpret_cast<void**>(&g_equipmentUpdate));
}

}  // namespace player_system_guard
