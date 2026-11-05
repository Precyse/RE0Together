#include "puppet_builder.h"

#include <cmath>

#include "calls.h"
#include "log.h"
#include "refw.h"

namespace {

// Layouts from the type database dump of build 0x6ae0ce34 (static_checks.md).
constexpr uint32_t kOrderCharaId = 0x20;        // StrayPawnFactoryOrder.<CharaID>
constexpr uint32_t kManagerFactory = 0xE8;      // StrayPawnManager._Factory
constexpr uint32_t kContainerCommon = 0x18;     // GenerateInfoContainer -> CommonInfoData
constexpr uint32_t kCommonObjectId = 0x70;      // CommonInfoData._ObjectID
constexpr uint32_t kCommonCategory = 0xB0;      // CommonInfoData._Category
constexpr uint32_t kCommonRequestType = 0x58;   // CommonInfoData._RequestType
constexpr uint32_t kCommonRequestId = 0x60;     // CommonInfoData._RequestID
constexpr uint32_t kRequestUniqueId = 0x18;     // RequestID -> UniqueID
constexpr uint32_t kRequestGeneratorId = 0x10;  // RequestID -> GeneratorID
constexpr uint32_t kProductWriter = 0x20;       // StrayPawnFactoryProductOnline.ContextWriter

// The values the riftstone summon writes (disassembly 0x140ec604d, 0x140ec605b, 0x140ec6108).
constexpr int32_t kCategoryStrayPawn = 2;
constexpr uint64_t kRequestTypeStrayPawn = 8;
constexpr uint64_t kGenerateOptions = 0x200008;
constexpr uint32_t kNoFreeCharacterId = 0x6EBC2F00;  // what getUnusedOnlineCharacterID returns when every id is taken

// requestDestroy flags (calls.h): no re-generate, with the fade, engine timing, context not saved, context removed.
constexpr bool kReGenerate = false, kDisableDissolve = false, kExternalTiming = false, kDisableContextSave = true,
               kRemoveContext = true;

void* asArg(uint64_t value) { return reinterpret_cast<void*>(static_cast<uintptr_t>(value)); }

bool stop(const char* step) {
    logger::write("puppet_builder: stopped at %s", step);
    return false;
}

// The generate request; returns its container (one reference of ours), or null.
void* requestGenerate(uint32_t id, const double position[3], float yaw) {
    void* generate = refw::managedSingleton("app.GenerateManager");
    void* records = refw::managedSingleton("app.ContextDBMS");
    if (!generate || !records) return stop("GenerateManager / ContextDBMS singletons"), nullptr;
    void* request = calls::objectResult(calls::kGetRequestIDfromCharacterID, generate, {asArg(id)});
    void* container = calls::objectResult(calls::kGetPoolGenerateInfoContainer, generate);
    if (!request || !container) return stop("request id / container"), nullptr;
    void* info = refw::field<void*>(container, kContainerCommon);
    if (!info || !refw::invoke(calls::method(calls::kSetRequestID), info, {request})) return stop("setRequestID"), nullptr;
    void* wrapper = calls::objectResult(calls::kGetIdWrapper, generate, {asArg(id)});
    if (!wrapper) return stop("getIdWrapper"), nullptr;
    refw::writeRef(info, kCommonObjectId, wrapper);
    refw::field<int32_t>(info, kCommonCategory) = kCategoryStrayPawn;
    refw::field<uint64_t>(info, kCommonRequestType) = kRequestTypeStrayPawn;

    double at[3] = {position[0], position[1], position[2]};
    float rotation[4] = {0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    if (!refw::invoke(calls::method(calls::kSetTransform), container, {at, rotation, scale})) return stop("setTransform"), nullptr;
    refw::invoke(calls::method(calls::kDeleteRecord), records, {refw::field<void*>(request, kRequestUniqueId)});
    void* generatorId = refw::field<void*>(refw::field<void*>(info, kCommonRequestId), kRequestGeneratorId);
    if (!refw::invoke(calls::method(calls::kRegistGeneratorID), generate, {generatorId})) return stop("registGeneratorID"), nullptr;
    void* controller = calls::objectResult(calls::kRequestGenerate, generate,
                                           {container, asArg(kGenerateOptions), nullptr, nullptr, nullptr, nullptr});
    logger::write("puppet_builder: generate requested for 0x%08X at (%.1f, %.1f, %.1f): %s", id, at[0], at[1], at[2],
                  controller ? "controller returned" : "no controller");
    if (!controller) return nullptr;
    refw::addRef(container);
    return container;
}

}  // namespace

namespace puppet_builder {

std::optional<Puppet> create(void* writer) {
    void* strays = refw::managedSingleton("app.StrayPawnManager");
    if (!strays) return stop("StrayPawnManager singleton"), std::nullopt;
    const refw::InvokeResult* free = refw::invoke(calls::method(calls::kGetUnusedOnlineCharacterID), strays, {nullptr});
    if (!free || free->dword == kNoFreeCharacterId) return stop("a free online CharacterID"), std::nullopt;
    const uint32_t id = free->dword;
    void* order = refw::createInstance("app.StrayPawnFactoryOrder");
    void* product = refw::createInstance("app.StrayPawnFactoryProductOnline", false);
    void* factory = refw::field<void*>(strays, kManagerFactory);
    bool ok = order && product && factory;
    if (ok) {
        refw::field<uint32_t>(order, kOrderCharaId) = id;
        ok = refw::invoke(calls::method(calls::kProductOnlineCtor), product, {order, writer}) &&
             refw::invoke(calls::method(calls::kAddProduct), factory, {asArg(id), product});
    }
    refw::release(order);
    if (!ok) {
        refw::release(product);
        return stop("registering the product"), std::nullopt;
    }
    logger::write("puppet_builder: product registered for online CharacterID 0x%08X", id);
    return Puppet{id, product, nullptr};
}

void rewrite(Puppet& puppet, void* writer) {
    if (puppet.product && writer) refw::writeRef(puppet.product, kProductWriter, writer);
}

bool generate(Puppet& puppet, const double position[3], float yaw) {
    if (!puppet.product || puppet.container) return false;
    puppet.container = requestGenerate(puppet.characterId, position, yaw);
    return puppet.container != nullptr;
}

bool destroy(void* container) {
    void* generate = refw::managedSingleton("app.GenerateManager");
    const refw::InvokeResult* done =
        generate && container ? refw::invoke(calls::method(calls::kRequestDestroy), generate,
                                             {container, asArg(kReGenerate), asArg(kDisableDissolve), asArg(kExternalTiming),
                                              asArg(kDisableContextSave), asArg(kRemoveContext)})
                              : nullptr;
    return done && done->byte;
}

void despawn(Puppet& puppet) {
    if (!puppet.container) return;
    logger::write("puppet_builder: removal of 0x%08X %s", puppet.characterId, destroy(puppet.container) ? "requested" : "refused");
    bodyGone(puppet);
}

void bodyGone(Puppet& puppet) {
    if (puppet.container) refw::release(puppet.container);
    puppet.container = nullptr;
}

void release(Puppet& puppet) {
    bodyGone(puppet);
    if (puppet.product) refw::release(puppet.product);
    puppet = {};
}

}  // namespace puppet_builder
