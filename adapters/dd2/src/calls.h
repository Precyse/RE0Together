#pragma once
// Every RE Engine method the plugin calls (as names.h lists the hooked ones), from the type database dump of the supported
// build with its rebased body. At start each row is resolved by type, name and parameters; a row whose body is not the
// pinned one turns its feature off (logged once), so a game update never makes the plugin call the wrong method.
#include <array>
#include <cstdint>
#include <initializer_list>

#include "reframework/API.h"

namespace calls {

enum class Feature : uint8_t { Body, Clock, Offline, Serializer, Puppet, Saves, Profile, Enemy, Count };

enum Id : uint8_t {
    kGetManualPlayer,
    kGetTransform,
    kGetUniversalPosition,
    kGetRotation,
    kGetActionManager,
    kGetCurrentActionHash,
    kGetHp,
    kGetIsDead,
    kGetInGameDay,
    kGetInGameElapsedDaySeconds,
    kGetTimeScale,
    kSetTime,
    kRequestForceOfflinePlay,
    kGetContext,
    kContextsTryGetValue,
    kProductOnlineCtor,
    kGetUnusedOnlineCharacterID,
    kAddProduct,
    kGetRequestIDfromCharacterID,
    kGetPoolGenerateInfoContainer,
    kSetRequestID,
    kGetIdWrapper,
    kSetTransform,
    kDeleteRecord,
    kRegistGeneratorID,
    kRequestGenerate,
    kRequestDestroy,
    kSerializeJson,
    kDeserializeJson,
    kGetCharaID,
    kGetNavigationController,
    kGetHit,
    kGetHumanActionSelector,
    kGetTargetAngleCtrl,
    kWarp,
    kRequestActionByHash,
    kRequestLocomotion,
    kRequestIdle,
    kSetTurnDestination,
    kGetDiscreteSpeedType,
    kGetSaveMountPath,
    kSetSaveMountPath,
    kGetSaveDirectoryPath,
    kGetForceWindows,
    kSetForceWindows,
    kSaveCleanData,
    kSaveAddDataClass,
    kSaveLoadSingle,
    kGetSaveState,
    kGetLastSaveResult,
    kSaveDataListCount,
    kSaveDataListItem,
    kIsTitleMenuPhase,
    kContextListCount,
    kContextListItem,
    kContextListSetItem,
    kContextListAdd,
    kRecordListCount,
    kRecordListItem,
    kItemListCount,
    kItemListItem,
    kItemListAdd,
    kItemListRemoveAt,
    kGetMainPawn,
    kGetCachedCharacter,
    kHirePawn,
    kDismissPawn,
    kGetGenerateInfo,
    kGetGenerateContainer,
    kContainerToSaveData,
    kSaveDataToContainer,
    kDirectDamage,
    kKillAndSetDieLoop,
    kSetHp,
    kGetGameObject,
    kGetEnemyNavigation,
    kGetMonsterSelector,
    kGetOriginalMaxHp,
    kGetEnemyChara,
    kGetMonsterChara,
    kGetPartyPawn,
    kCount
};

struct Row {
    Id id;
    const char* type;
    const char* method;
    const char* params;
    uintptr_t pinned;
    Feature feature;
};

constexpr std::array<Row, kCount> kRows{{
    {kGetManualPlayer, "app.CharacterManager", "get_ManualPlayer", "", 0x1463c7310, Feature::Body},
    {kGetTransform, "app.Character", "get_Transform", "", 0x148fdb9e0, Feature::Body},
    {kGetUniversalPosition, "via.Transform", "get_UniversalPosition", "", 0x146863840, Feature::Body},
    {kGetRotation, "via.Transform", "get_Rotation", "", 0x146863920, Feature::Body},
    {kGetActionManager, "app.Character", "get_ActionManager", "", 0x148ec94c0, Feature::Body},
    {kGetCurrentActionHash, "app.ActionManager", "getCurrentActionHash", "System.UInt32", 0x1452a28b0, Feature::Body},
    {kGetHp, "app.Character", "get_Hp", "", 0x1452aeb50, Feature::Body},
    {kGetIsDead, "app.Character", "get_IsDead", "", 0x145248550, Feature::Body},
    {kGetInGameDay, "app.TimeManager", "get_InGameDay", "", 0x14734b3d0, Feature::Clock},
    {kGetInGameElapsedDaySeconds, "app.TimeManager", "get_InGameElapsedDaySeconds", "", 0x147358280, Feature::Clock},
    {kGetTimeScale, "app.TimeManager", "get_TimeScale", "", 0x1473582f0, Feature::Clock},
    {kSetTime, "app.TimeManager", "setTime", "System.Int32,System.Int32,System.Int32,System.Boolean", 0x145a7fdc0,
     Feature::Clock},
    {kRequestForceOfflinePlay, "app.NetworkManager", "requestForceOfflinePlay",
     "System.Action,System.Action`1<app.network.NetworkError.ErrorCause>", 0x1467970d0, Feature::Offline},
    // The stray-pawn route a puppet (echo or peer) is built and removed through (coop-scratch\dd2\design\static_checks.md).
    {kGetContext, "app.Character", "get_Context", "", 0x148ec9580, Feature::Body},
    {kContextsTryGetValue, "System.Collections.Generic.Dictionary`2<System.Type,app.ContextHolder.ContextInfoBase>", "TryGetValue",
     "System.Type,app.ContextHolder.ContextInfoBase", 0x140025140, Feature::Puppet},
    {kProductOnlineCtor, "app.StrayPawnFactoryProductOnline", ".ctor", "app.StrayPawnFactoryOrder,app.ContextWriterOnline",
     0x1469cafb0, Feature::Puppet},
    {kGetUnusedOnlineCharacterID, "app.StrayPawnManager", "getUnusedOnlineCharacterID",
     "System.Collections.Generic.List`1<app.CharacterID>", 0x145a70a30, Feature::Puppet},
    {kAddProduct, "app.StrayPawnFactory", "addProduct", "app.CharacterID,app.StrayPawnFactoryProduct", 0x1480b79b0,
     Feature::Puppet},
    {kGetRequestIDfromCharacterID, "app.GenerateManager", "getRequestIDfromCharacterID", "app.CharacterID", 0x1402f53d0,
     Feature::Puppet},
    {kGetPoolGenerateInfoContainer, "app.GenerateManager", "getPoolGenerateInfoContainer", "", 0x140311810, Feature::Puppet},
    {kSetRequestID, "app.GenerateInfo.CommonInfoData", "setRequestID", "app.RequestID", 0x14038c700, Feature::Puppet},
    {kGetIdWrapper, "app.GenerateManager", "getIdWrapper", "app.CharacterID", 0x1402ff490, Feature::Puppet},
    {kSetTransform, "app.GenerateInfo.GenerateInfoContainer", "setTransform", "via.Position,via.Quaternion,via.vec3", 0x140154740,
     Feature::Puppet},
    {kDeleteRecord, "app.ContextDBMS", "deleteRecord", "app.UniqueID", 0x1409991e0, Feature::Puppet},
    {kRegistGeneratorID, "app.GenerateManager", "registGeneratorID", "app.GeneratorID", 0x1401547c0, Feature::Puppet},
    {kRequestGenerate, "app.GenerateManager", "requestGenerate",
     "app.GenerateInfo.GenerateInfoContainer,app.GenerateManager.GenerateOprionFlags,System.Boolean,System.Boolean,"
     "System.Action`2<app.PrefabInstantiateResults,app.DummyArg_2_0_0_13>,"
     "System.Action`2<app.PrefabInstantiateResults,app.DummyArg_2_0_0_14>",
     0x1401551b0, Feature::Puppet},
    // requestDestroy(container, reGenerate, disableDissolve, externalTiming, disableContextSave, removeContext): the
    // order of the flags read from the decompile (they fill DestroyRequestInfo +0x2D, +0x2F, +0x31, +0x2E, +0x30).
    {kRequestDestroy, "app.GenerateManager", "requestDestroy",
     "app.GenerateInfo.GenerateInfoContainer,System.Boolean,System.Boolean,System.Boolean,System.Boolean,System.Boolean",
     0x1401f51a0, Feature::Puppet},
    // The engine's reflection serializer (statics): (System.Type, value, options) and (System.Type, json, options).
    {kSerializeJson, "via.Serializer", "serializeJsonInternal", "System.Object,System.Object,via.Serializer.Options",
     0x146481780, Feature::Serializer},
    {kDeserializeJson, "via.Serializer", "deserializeJsonInternal", "System.Object,System.String,via.Serializer.Options",
     0x146481700, Feature::Serializer},
    // The puppet driver (DD2 design players.md 4-5): the body's controllers, warp, actions, locomotion, facing.
    {kGetCharaID, "app.Character", "get_CharaID", "", 0x148fda020, Feature::Puppet},
    {kGetNavigationController, "app.Character", "get_NavigationController", "", 0x1464a74b0, Feature::Puppet},
    {kGetHit, "app.Character", "get_Hit", "", 0x146627e60, Feature::Puppet},
    {kGetHumanActionSelector, "app.Character", "get_HumanActionSelector", "", 0x148fed600, Feature::Puppet},
    {kGetTargetAngleCtrl, "app.Character", "get_TargetAngleCtrl", "", 0x146376d30, Feature::Puppet},
    {kWarp, "app.Character", "warp", "via.Position,via.Quaternion,app.CharacterWarpOption", 0x1408f8570, Feature::Puppet},
    {kRequestActionByHash, "app.ActionManager", "requestActionByHash", "System.UInt32,System.UInt32,System.Boolean",
     0x145a851b0, Feature::Puppet},
    {kRequestLocomotion, "app.HumanActionSelector", "requestLocomotionImpl", "app.LocomotionSpeedTypeEnum,System.Boolean",
     0x1402b5540, Feature::Puppet},
    {kRequestIdle, "app.HumanActionSelector", "requestIdleImpl", "System.Boolean", 0x1402b5dc0, Feature::Puppet},
    {kSetTurnDestination, "app.TargetAngleController", "setDestination", "via.Position", 0x1406c32f0, Feature::Puppet},
    {kGetDiscreteSpeedType, "app.Character", "get_DiscreteLocomotionSpeedType", "", 0x148feea10, Feature::Body},
    // The save folder (native SaveService; set_SaveDirectoryPath is a retail stub, static_checks.md).
    {kGetSaveMountPath, "via.storage.saveService.SaveService", "get_SaveMountPath", "", 0x1463367f0, Feature::Saves},
    {kSetSaveMountPath, "via.storage.saveService.SaveService", "set_SaveMountPath", "System.String", 0x146336880, Feature::Saves},
    {kGetSaveDirectoryPath, "via.storage.saveService.SaveService", "get_SaveDirectoryPath", "", 0x146336940, Feature::Saves},
    // On Steam the SaveService stores through the Steam cloud and the mount is a name there; ForceWindows makes it the file system,
    // so a mount is a folder path.
    {kGetForceWindows, "via.storage.saveService.SaveService", "get_ForceWindows", "", 0x1463372c0, Feature::Saves},
    {kSetForceWindows, "via.storage.saveService.SaveService", "set_ForceWindows", "System.Boolean", 0x1463372e0, Feature::Saves},
    // A save loaded outside the game's own flow into a scratch GameSaveData (the capture launch, src/offline_capture.h).
    {kSaveCleanData, "via.storage.saveService.SaveService", "cleanData", "", 0x146336530, Feature::Saves},
    {kSaveAddDataClass, "via.storage.saveService.SaveService", "addDataClass", "System.String,System.Object", 0x1463364c0, Feature::Saves},
    {kSaveLoadSingle, "via.storage.saveService.SaveService", "loadSingle", "System.Int32", 0x146336570, Feature::Saves},
    {kGetSaveState, "via.storage.saveService.SaveService", "get_SaveState", "", 0x146336630, Feature::Saves},
    {kGetLastSaveResult, "via.storage.saveService.SaveService", "get_LastSaveResult", "", 0x146336640, Feature::Saves},
    {kSaveDataListCount, "System.Collections.Generic.List`1<app.SaveDataBase>", "get_Count", "", 0x1452a9120, Feature::Saves},
    {kSaveDataListItem, "System.Collections.Generic.List`1<app.SaveDataBase>", "get_Item", "System.Int32", 0x14549f780, Feature::Saves},
    {kIsTitleMenuPhase, "app.MainFlowManager", "get_IsTitleMenuPhase", "", 0x147019850, Feature::Saves},
    // The save sections the profile is read from and spliced into (DD2 design progression.md 2-3); the List bodies are
    // generic and shared by every instantiation.
    {kContextListCount, "System.Collections.Generic.List`1<app.Context>", "get_Count", "", 0x1452a9120, Feature::Profile},
    {kContextListItem, "System.Collections.Generic.List`1<app.Context>", "get_Item", "System.Int32", 0x14549f780, Feature::Profile},
    {kContextListSetItem, "System.Collections.Generic.List`1<app.Context>", "set_Item", "System.Int32,app.Context", 0x14027af70,
     Feature::Profile},
    {kContextListAdd, "System.Collections.Generic.List`1<app.Context>", "Add", "app.Context", 0x14761e3a0, Feature::Profile},
    {kRecordListCount, "System.Collections.Generic.List`1<app.ContextDatabaseSaveDataRecord>", "get_Count", "", 0x1452a9120,
     Feature::Profile},
    {kRecordListItem, "System.Collections.Generic.List`1<app.ContextDatabaseSaveDataRecord>", "get_Item", "System.Int32",
     0x14549f780, Feature::Profile},
    {kItemListCount, "System.Collections.Generic.List`1<app.ItemManager.ItemStorageBackup>", "get_Count", "", 0x1452a9120,
     Feature::Profile},
    {kItemListItem, "System.Collections.Generic.List`1<app.ItemManager.ItemStorageBackup>", "get_Item", "System.Int32",
     0x14549f780, Feature::Profile},
    {kItemListAdd, "System.Collections.Generic.List`1<app.ItemManager.ItemStorageBackup>", "Add",
     "app.ItemManager.ItemStorageBackup", 0x14734b7b0, Feature::Profile},
    {kItemListRemoveAt, "System.Collections.Generic.List`1<app.ItemManager.ItemStorageBackup>", "RemoveAt", "System.Int32",
     0x1400658f0, Feature::Profile},
    // Main pawns (DD2 design players.md 7, static_checks.md phase 5): the local one, and seating a remote one in the party
    // (hirePawn and dismissPawn make no server call; a character without server pawn data is accepted).
    {kGetMainPawn, "app.PawnManager", "get_MainPawn", "", 0x140615ec0, Feature::Body},
    {kGetCachedCharacter, "app.Pawn", "get_CachedCharacter", "", 0x146339a60, Feature::Body},
    {kHirePawn, "app.PawnManager", "hirePawn", "app.Character,System.Boolean", 0x146658af0, Feature::Puppet},
    {kDismissPawn, "app.PawnManager", "dismissPawn", "app.CharacterID", 0x146657cb0, Feature::Puppet},
    // Enemy sync (DD2 design world.md 1-2, static_checks.md "Enemy identity and hit injection"): an enemy's generate
    // record both ways, the engine's own direct-damage entry, the native death, HP, and a mirror's controllers.
    {kGetGenerateInfo, "app.Character", "get_GenerateInfo", "", 0x146339a70, Feature::Enemy},
    {kGetGenerateContainer, "app.GenerateInfo", "get_Container", "", 0x14633a8d0, Feature::Enemy},
    {kContainerToSaveData, "app.GenerateManager", "convContainerToRequestSaveData",
     "app.GenerateInfo,app.GenerateInfo.GenerateInfoContainer", 0x14092bec0, Feature::Enemy},
    {kSaveDataToContainer, "app.GenerateManager", "convRequestSaveDataToContainer",
     "app.GenerateManager.GenerateRequestCharacterSaveData", 0x140e050e0, Feature::Enemy},
    // directDamage(info, target, attacker GameObject, Boolean, attacker HitController): queues the info on
    // HitManager.DirectDamageList, drained by HitManager.damageProc like a collision hit.
    {kDirectDamage, "app.HitManager", "directDamage",
     "app.HitController.DamageInfo,app.HitController,via.GameObject,System.Boolean,app.HitController", 0x140d6eb20,
     Feature::Enemy},
    {kKillAndSetDieLoop, "app.Character", "killAndSetDieLoop", "app.Character.KillAndSetDieLoopOption", 0x148ff0180,
     Feature::Enemy},
    // setHp(hp, even when the region is already down, region index 0 = the whole body).
    {kSetHp, "app.HitController", "setHp", "System.Single,System.Boolean,System.Int32", 0x1401d5d80, Feature::Enemy},
    {kGetGameObject, "via.Component", "get_GameObject", "", 0x1464b7d30, Feature::Enemy},
    {kGetEnemyNavigation, "app.Ch200000", "get_NavigationController", "", 0x14633ad20, Feature::Enemy},
    {kGetMonsterSelector, "app.Monster", "get_MonsterActionSelector", "", 0x1464a8ac0, Feature::Enemy},
    {kGetOriginalMaxHp, "app.Character", "get_OriginalMaxHp", "", 0x1452aeb70, Feature::Enemy},
    // The roster's character of an enemy or monster component, through the engine's getters: Ch200000.get_Chara reads +0x88
    // where the dump's field table says 0x80 (both bodies are shared accessors, fine to call, never hooked).
    {kGetEnemyChara, "app.Ch200000", "get_Chara", "", 0x14633ab50, Feature::Body},
    {kGetMonsterChara, "app.Monster", "get_Chara", "", 0x1464a74b0, Feature::Body},
    // The Pawn in a party seat (PawnID SubPawn01 or SubPawn02: the hired support pawns), null when the seat is empty.
    {kGetPartyPawn, "app.PawnManager", "getPartyPawn", "app.PawnManager.PawnID,System.Boolean", 0x1408d7f70, Feature::Puppet},
}};

// kRows is indexed by Id.
constexpr bool rowsInIdOrder() {
    for (size_t i = 0; i < kRows.size(); ++i) {
        if (kRows[i].id != i) return false;
    }
    return true;
}
static_assert(rowsInIdOrder());

// Resolves every row; logs each refused row and each feature left off.
void resolve();

bool enabled(Feature feature);

// The resolved method of a row (null when its feature is off).
REFrameworkMethodHandle method(Id id);

// Invokes a row and returns the object it gave back, or null (feature off, refused, threw or null result).
void* objectResult(Id id, void* self, std::initializer_list<void*> args = {});

}  // namespace calls
