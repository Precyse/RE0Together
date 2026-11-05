#include "save_sections.h"

#include "log.h"
#include "refw.h"

namespace {

constexpr const char* kCharacterIdEnum = "app.CharacterID";
constexpr const char* kArisenLiteral = "ch000000_00";
constexpr const char* kMainPawnLiteral = "ch100000_00";
constexpr const char* kCharacterIdContext = "app.CharacterIDContext";
constexpr uint32_t kContextCharacterId = 0x18;  // CharacterIDContext.CharacterID

constexpr const char* kWorldOwned[] = {kCharacterIdContext, "app.UniqueIDContext", "app.CharacterPosRotContext"};

std::optional<uint32_t> g_arisen, g_mainPawn;

}  // namespace

namespace save_sections {

bool init() {
    for (const auto& [name, value] : refw::enumLiterals(kCharacterIdEnum)) {
        if (name == kArisenLiteral) g_arisen = static_cast<uint32_t>(value);
        if (name == kMainPawnLiteral) g_mainPawn = static_cast<uint32_t>(value);
    }
    if (!g_arisen || !g_mainPawn) logger::write("save_sections: %s or %s missing from %s", kArisenLiteral, kMainPawnLiteral, kCharacterIdEnum);
    return g_arisen && g_mainPawn;
}

std::optional<profile_rule::Owner> ownerOf(uint32_t characterId) {
    if (characterId == g_arisen) return profile_rule::Owner::Arisen;
    if (characterId == g_mainPawn) return profile_rule::Owner::MainPawn;
    return std::nullopt;
}

size_t count(calls::Id countRow, void* list) {
    const refw::InvokeResult* result = list ? refw::invoke(calls::method(countRow), list) : nullptr;
    return result ? static_cast<size_t>(result->dword) : 0;
}

void* item(calls::Id itemRow, void* list, size_t index) {
    return calls::objectResult(itemRow, list, {reinterpret_cast<void*>(index)});
}

bool isType(void* object, const char* type) { return object && refw::typeNameOf(object) == type; }

void* records(void* contextSection) {
    if (!isType(contextSection, kContextSection)) return nullptr;
    void* database = refw::field<void*>(contextSection, kSectionDatabase);
    return database ? refw::field<void*>(database, kDatabaseRecords) : nullptr;
}

std::optional<uint32_t> recordCharacter(void* record) {
    void* contexts = record ? refw::field<void*>(record, kRecordContexts) : nullptr;
    const size_t n = count(calls::kContextListCount, contexts);
    for (size_t i = 0; i < n; ++i) {
        void* context = item(calls::kContextListItem, contexts, i);
        if (isType(context, kCharacterIdContext)) return refw::field<uint32_t>(context, kContextCharacterId);
    }
    return std::nullopt;
}

bool worldOwned(const std::string& contextType) {
    for (const char* type : kWorldOwned) {
        if (contextType == type) return true;
    }
    return false;
}

}  // namespace save_sections
