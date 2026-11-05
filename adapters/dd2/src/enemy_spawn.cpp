#include "enemy_spawn.h"

#include "calls.h"
#include "log.h"
#include "puppet_builder.h"
#include "refw.h"
#include "serializer.h"

namespace {

constexpr const char* kGenerateManager = "app.GenerateManager";
constexpr const char* kRecordType = "app.GenerateManager.GenerateRequestCharacterSaveData";
constexpr const char* kOptionEnum = "app.GenerateManager.GenerateOprionFlags";
constexpr const char* kSpawnOption = "OverrideContextPosition";  // the record's current position, not a stored context's
constexpr uint32_t kContainerCommon = 0x18;     // GenerateInfoContainer -> CommonInfoData
constexpr uint32_t kCommonRequestId = 0x60;     // CommonInfoData._RequestID
constexpr uint32_t kRequestGeneratorId = 0x10;  // RequestID -> GeneratorID

std::optional<uint64_t> g_options;

void* asArg(uint64_t value) { return reinterpret_cast<void*>(static_cast<uintptr_t>(value)); }

bool stop(const char* step) {
    logger::write("enemy_spawn: stopped at %s", step);
    return false;
}

void* containerOf(void* character, void*& info) {
    info = character ? calls::objectResult(calls::kGetGenerateInfo, character) : nullptr;
    return info ? calls::objectResult(calls::kGetGenerateContainer, info) : nullptr;
}

}  // namespace

namespace enemy_spawn {

bool init() {
    for (const auto& [name, value] : refw::enumLiterals(kOptionEnum)) {
        if (name == kSpawnOption) g_options = static_cast<uint64_t>(value);
    }
    logger::write("enemy_spawn: %s literal %s", kSpawnOption, g_options ? "found" : "missing");
    return g_options.has_value();
}

std::optional<std::string> recordOf(void* character) {
    void* generate = refw::managedSingleton(kGenerateManager);
    void* info = nullptr;
    void* container = containerOf(character, info);
    if (!generate || !container) return stop("the enemy's generate container"), std::nullopt;
    void* record = calls::objectResult(calls::kContainerToSaveData, generate, {info, container});
    if (!record) return stop("convContainerToRequestSaveData"), std::nullopt;
    auto json = serializer::toJson(kRecordType, record);
    if (!json) stop("the record's JSON");
    return json;
}

bool create(const std::string& json) {
    void* generate = refw::managedSingleton(kGenerateManager);
    if (!generate || !g_options) return stop("GenerateManager singleton / option literal");
    void* record = serializer::fromJson(kRecordType, json);
    if (!record) return stop("the record from JSON");
    void* container = calls::objectResult(calls::kSaveDataToContainer, generate, {record});
    refw::release(record);
    if (!container) return stop("convRequestSaveDataToContainer");
    void* common = refw::field<void*>(container, kContainerCommon);
    void* request = common ? refw::field<void*>(common, kCommonRequestId) : nullptr;
    void* generatorId = request ? refw::field<void*>(request, kRequestGeneratorId) : nullptr;
    if (!generatorId || !refw::invoke(calls::method(calls::kRegistGeneratorID), generate, {generatorId})) {
        return stop("registGeneratorID");
    }
    void* controller = calls::objectResult(calls::kRequestGenerate, generate,
                                           {container, asArg(*g_options), nullptr, nullptr, nullptr, nullptr});
    return controller != nullptr || stop("requestGenerate");
}

bool destroy(void* character) {
    void* info = nullptr;
    return puppet_builder::destroy(containerOf(character, info));
}

}  // namespace enemy_spawn
