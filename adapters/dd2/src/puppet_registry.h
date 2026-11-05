#pragma once
// The puppets this machine built and the engine objects of each once its body is ready (DD2 design players.md 3-4).
// The builder announces an online CharacterID; the ready hook binds the body that carries it, with the controllers the
// brain-off gates compare against. Gates run on any thread and only read; everything else is game thread.
#include <cstdint>
#include <optional>

namespace puppet_registry {

struct Body {
    void* character = nullptr;
    void* actions = nullptr;     // ActionManager
    void* selector = nullptr;    // HumanActionSelector
    void* navigation = nullptr;  // CharacterNavigationController
    void* hit = nullptr;         // HitController
    void* turn = nullptr;        // TargetAngleController
};

// Game thread.
void expect(uint32_t characterId);
void forget(uint32_t characterId);
std::optional<Body> body(uint32_t characterId);

// The engine destroyed this puppet's body on its own (it needs building again).
bool destroyed(uint32_t characterId);

// Hooks (game thread): a body reported ready, a body destroyed.
void onReady(void* character);
void onDestroyed(void* character);

// Any thread: is this id or engine object one of our puppets'.
bool isPuppetId(uint32_t characterId);
bool isPuppetObject(const void* object);

// Requests on a puppet pass the gates only inside a driver scope (this thread).
class DriverScope {
public:
    DriverScope();
    ~DriverScope();
    DriverScope(const DriverScope&) = delete;
    DriverScope& operator=(const DriverScope&) = delete;
};
bool inDriverScope();

}  // namespace puppet_registry
