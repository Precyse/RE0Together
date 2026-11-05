#pragma once
// The two save sections the co-op profile is made of, as the game's save and load methods pass them (DD2 design
// progression.md 1; field offsets from the type database dump): ContextDBMSSaveData (one record of contexts per
// character) and ItemManager.GameSaveData (every item record, money, the Arisen's equipment set). Game objects only;
// callers hold no lock of the game's.
#include <cstdint>
#include <optional>
#include <string>

#include "calls.h"
#include "profile_rule.h"

namespace save_sections {

constexpr const char* kContextSection = "app.ContextDBMSSaveData";
constexpr const char* kItemSection = "app.ItemManager.GameSaveData";
constexpr const char* kMoneyType = "app.ItemManager.MoneyBackup";

constexpr uint32_t kSectionDatabase = 0x18;  // ContextDBMSSaveData.DBSaveData
constexpr uint32_t kDatabaseRecords = 0x10;  // ContextDatabaseSaveData.Records
constexpr uint32_t kRecordContexts = 0x10;   // ContextDatabaseSaveDataRecord.Contexts
constexpr uint32_t kItemSaves = 0x18;        // GameSaveData.ItemStorageSaves
constexpr uint32_t kMoneySave = 0x28;        // GameSaveData.MoneySave
constexpr uint32_t kEquipIndex = 0xE4;       // GameSaveData.ArisenEquipIndex
constexpr uint32_t kItemCharacter = 0x30;    // ItemStorageBackup._CharaId
constexpr uint32_t kItemStorage = 0x28;      // ItemStorageBackup._StorageId
constexpr uint32_t kStatusLevel = 0x2C;      // HumanStatusContext._Level
constexpr uint32_t kJobCurrent = 0x28;       // JobContext.CurrentJob

// Reads the Arisen's and the main pawn's CharacterID values; false when the enum lacks them.
bool init();

// Whose a CharacterID value is, among the two the profile carries.
std::optional<profile_rule::Owner> ownerOf(uint32_t characterId);

// A managed List through its rows (count, item); 0 / null on failure.
size_t count(calls::Id countRow, void* list);
void* item(calls::Id itemRow, void* list, size_t index);

// The section's object is of this runtime type.
bool isType(void* object, const char* type);

// ContextDBMSSaveData -> its List of records; null when the shape is not the expected one.
void* records(void* contextSection);

// The CharacterID a record of contexts belongs to (its CharacterIDContext), or null.
std::optional<uint32_t> recordCharacter(void* record);

// These context types stay the world's: who the record is and where the character stands.
bool worldOwned(const std::string& contextType);

}  // namespace save_sections
