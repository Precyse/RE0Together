#include "ds2/remote_context.h"

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"

namespace {

constexpr uintptr_t kEntityParamLike = 0x56c8;         // component the param query helper asks
constexpr uintptr_t kParamLikeTarget = 0x30;
constexpr uintptr_t kPlayerControlledId = 0x4c0;       // player -> id of the entity it controls (-1 none)
constexpr uintptr_t kEntityTableGlobal = 0x14623EAD8;  // [global] + 0x58 = entity table, indexed by (id >> 41) & 0x7fff
constexpr uintptr_t kEntityTable = 0x58;
constexpr int kEntityIndexShift = 41;
constexpr uint64_t kEntityIndexMask = 0x7fff;

constexpr uintptr_t kGetLocalController = 0x140da1210;  // () -> index-0 entity + 0x5658
constexpr uintptr_t kLocalParamQuery = 0x140da4500;     // (out*, int) -> query on [[entity + 0x56c8] + 0x30]
constexpr uintptr_t kParamQueryTarget = 0x140e601d0;    // (object, out*, int)
constexpr uintptr_t kLocalControlled = 0x140da4590;     // () -> entity from the index-0 player's controlled id
constexpr uintptr_t kNotifyLocalEntity = 0x140f61ca0;   // (const u32* param) -> a message to the index-0 entity
constexpr uintptr_t kSendToEntity = 0x140130c60;        // (entity, message*, int)

using NoArgFn = uintptr_t (*)();
using QueryFn = uintptr_t (*)(uintptr_t out, int arg);
using QueryTargetFn = void (*)(uintptr_t object, uintptr_t out, int arg);
using NotifyFn = void (*)(const uint32_t* param);
using SendFn = void (*)(uintptr_t entity, uintptr_t message, int flags);

NoArgFn g_getController = nullptr;
QueryFn g_paramQuery = nullptr;
NoArgFn g_controlled = nullptr;
NotifyFn g_notify = nullptr;
SendFn g_send = nullptr;
thread_local int t_remoteDepth = 0;
thread_local bool t_inNotify = false;

uintptr_t getControllerDetour() {
    if (!remote_context::active()) return g_getController();
    return decima::readPointer(remote_player::entity() + ds2::kEntityController);
}

uintptr_t paramQueryDetour(uintptr_t out, int arg) {
    if (remote_context::active()) {
        const uintptr_t object =
            decima::readPointer(decima::readPointer(remote_player::entity() + kEntityParamLike) + kParamLikeTarget);
        if (object) {
            reinterpret_cast<QueryTargetFn>(ds2::at(kParamQueryTarget))(object, out, arg);
            return out;
        }
    }
    return g_paramQuery(out, arg);
}

uintptr_t controlledDetour() {
    if (!remote_context::active()) return g_controlled();
    const int64_t id = *reinterpret_cast<int64_t*>(remote_player::player() + kPlayerControlledId);
    if (id != -1) {
        const uintptr_t table = decima::readPointer(decima::readPointer(ds2::at(kEntityTableGlobal)) + kEntityTable);
        const uint64_t index = (static_cast<uint64_t>(id) >> kEntityIndexShift) & kEntityIndexMask;
        const uintptr_t entity = decima::readPointer(table + index * sizeof(uintptr_t));
        if (entity) return entity;
    }
    return remote_player::entity();
}

void notifyDetour(const uint32_t* param) {
    t_inNotify = remote_context::active();
    g_notify(param);
    t_inNotify = false;
}

void sendDetour(uintptr_t entity, uintptr_t message, int flags) {
    if (t_inNotify && entity == remote_player::samEntity()) entity = remote_player::entity();
    g_send(entity, message, flags);
}

}  // namespace

namespace remote_context {

bool active() { return t_remoteDepth > 0 && remote_player::entity(); }

bool enter(uintptr_t handlerList) {
    const uintptr_t remote = remote_player::entity();
    const bool isRemote = remote && handlerList - ds2::kEntityHandlerList == remote;
    if (isRemote) ++t_remoteDepth;
    return isRemote;
}

void leave(bool remote) {
    if (remote) --t_remoteDepth;
}

void install() {
    hooks::install("local controller helper", ds2::at(kGetLocalController),
                   reinterpret_cast<void*>(&getControllerDetour), reinterpret_cast<void**>(&g_getController));
    hooks::install("local param query helper", ds2::at(kLocalParamQuery), reinterpret_cast<void*>(&paramQueryDetour),
                   reinterpret_cast<void**>(&g_paramQuery));
    hooks::install("local controlled entity helper", ds2::at(kLocalControlled),
                   reinterpret_cast<void*>(&controlledDetour), reinterpret_cast<void**>(&g_controlled));
    hooks::install("local entity notify helper", ds2::at(kNotifyLocalEntity), reinterpret_cast<void*>(&notifyDetour),
                   reinterpret_cast<void**>(&g_notify));
    hooks::install("send to entity", ds2::at(kSendToEntity), reinterpret_cast<void*>(&sendDetour),
                   reinterpret_cast<void**>(&g_send));
}

}  // namespace remote_context
