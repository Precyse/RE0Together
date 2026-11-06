#pragma once
// The one table of every RE Engine method the plugin hooks (DD2 design rule: names, not addresses, are the source;
// the pinned address catches a moved or shared body). Each row comes from the type database dump of the supported
// build (coop-scratch\dd2\tdb\methods.tsv, PE 0x6ae0ce34), rebased to 0x140000000; `share` is how many methods the dump
// gives that native body (rule H2 refuses anything but 1).
#include <array>
#include <cstdint>

namespace names {

struct MethodRow {
    const char* id;
    const char* type;
    const char* method;
    const char* params;  // parameter type names joined by ',' (empty: none), to tell overloads apart
    uintptr_t pinned;
    uint32_t share;
    const char* set;     // rows of one set install all or nothing
};

constexpr const char* kWorldGate = "world_gate";

constexpr MethodRow kChangeFlowPhase{"change_flow_phase", "app.MainFlowManager", "changeFlowPhase",
                                     "app.MainFlowManager.Phase", 0x141050050, 1, kWorldGate};
constexpr MethodRow kChangeSceneType{"change_scene_type", "app.MainFlowManager", "changeSceneType",
                                     "app.MainFlowManager.SceneType,app.GuiDefine.GuiType", 0x14704aaa0, 1, kWorldGate};
constexpr MethodRow kCompleteSceneLoad{"complete_scene_load", "app.MainFlowManager", "onCompleteSceneLoad", "",
                                       0x140fdeca0, 1, kWorldGate};
constexpr MethodRow kFerrystoneLoad{"ferrystone_load", "app.FerrystoneFlowController", "startLoad", "", 0x145aaefc0, 1,
                                    kWorldGate};

// The puppet set (DD2 design players.md 3-4): binding a built body, the brain-off gates, keeping the body alive.
constexpr const char* kPuppet = "puppet";

constexpr MethodRow kReadyCharacter{"ready_character", "app.CharacterManager", "onReadyCharacter", "app.Character",
                                    0x140a5c820, 1, kPuppet};
constexpr MethodRow kCharacterDestroy{"character_destroy", "app.Character", "onDestroy", "", 0x145072650, 1, kPuppet};
constexpr MethodRow kNavDestination{"nav_destination", "app.CharacterNavigationController", "setDestination", "via.Position",
                                    0x1404b3470, 1, kPuppet};
constexpr MethodRow kPawnNavDestination{"pawn_nav_destination", "app.PawnNavigationController", "setDestination",
                                        "via.Position", 0x144c908f0, 1, kPuppet};
constexpr MethodRow kActionCore{"action_core", "app.ActionManager", "requestActionCore",
                                "app.ActionManager.Priority,System.String,System.UInt32", 0x140347570, 1, kPuppet};
constexpr MethodRow kLocomotion{"locomotion", "app.HumanActionSelector", "requestLocomotionImpl",
                                "app.LocomotionSpeedTypeEnum,System.Boolean", 0x1402b5540, 1, kPuppet};
constexpr MethodRow kIdle{"idle", "app.HumanActionSelector", "requestIdleImpl", "System.Boolean", 0x1402b5dc0, 1, kPuppet};
constexpr MethodRow kDamageHp{"damage_hp", "app.HitController", "updateDamageHp",
                              "app.HitController.DamageInfo,System.Single,System.Boolean", 0x14090cd60, 1, kPuppet};
constexpr MethodRow kSetHp{"set_hp", "app.HitController", "setHp", "System.Single,System.Boolean,System.Int32", 0x1401d5d80, 1,
                           kPuppet};
constexpr MethodRow kLeaveStrayPawn{"leave_stray_pawn", "app.NPCManager", "leaveRimSearchedStrayPawn", "app.CharacterID",
                                    0x1484e7670, 1, kPuppet};
constexpr MethodRow kDestroyStrayPawn{"destroy_stray_pawn", "app.NPCManager", "destroyStrayPawn", "app.NPCHolder,System.Boolean",
                                      0x145fae090, 1, kPuppet};
constexpr MethodRow kPawnLost{"pawn_lost", "app.PawnManager", "lost", "app.CharacterID,System.Boolean,System.Boolean",
                              0x1466599d0, 1, kPuppet};

// The profile set (DD2 design progression.md 2-3): the save sections the game itself makes (read in solo play) and the
// loaders a co-op load runs (where the guest's character is spliced in).
constexpr const char* kProfile = "profile";

constexpr MethodRow kSaveContexts{"save_contexts", "app.ContextDBMS", "saveGameSaveData", "", 0x140c2ef00, 1, kProfile};
constexpr MethodRow kLoadContexts{"load_contexts", "app.ContextDBMS", "loadGameSaveData", "app.SaveDataBase", 0x145a3ec50, 1,
                                  kProfile};
constexpr MethodRow kSaveItems{"save_items", "app.ItemManager", "saveGameSaveData", "", 0x140c5eb00, 1, kProfile};
constexpr MethodRow kLoadItems{"load_items", "app.ItemManager", "loadGameSaveData", "app.SaveDataBase", 0x144cedac0, 1, kProfile};

// The plague set (DD2 design progression.md 10): dragonsplague off on every machine while coop=1.
constexpr const char* kPlague = "plague";

constexpr MethodRow kAddPossessionPoint{"add_possession_point", "app.PawnDataContext", "addPossessionProgressPoint", "System.Int32",
                                        0x146c3cff0, 1, kPlague};
constexpr MethodRow kSetPossessionLv{"set_possession_lv", "app.PawnDataContext", "setPossessionLv", "System.Int32", 0x140c7a550, 1,
                                     kPlague};
constexpr MethodRow kPawnPossessionLv{"pawn_possession_lv", "app.PawnManager", "setPossessionLV", "app.CharacterID,System.Boolean",
                                      0x146662190, 1, kPlague};
constexpr MethodRow kMainPawnPossessionLv{"main_pawn_possession_lv", "app.PawnManager", "setPossessionLVMainPawn",
                                          "app.PawnDataContext,System.Boolean", 0x146662300, 1, kPlague};
constexpr MethodRow kJudgePossession{"judge_possession", "app.FacilityManager", "judgPossession", "", 0x1463b4760, 1, kPlague};
constexpr MethodRow kKillPossessed{"kill_possessed", "app.NPCManager", "killNPC_Possession", "", 0x1484de210, 1, kPlague};

// The roster set (DD2 design world.md, phase 7 probe P7-1): the engine's own enemy and monster registration.
constexpr const char* kRoster = "roster";

constexpr MethodRow kRegisterEnemy{"register_enemy", "app.EnemyManager", "registerEnemy", "app.Ch200000", 0x1402e8e60, 1, kRoster};
constexpr MethodRow kRemoveEnemy{"remove_enemy", "app.EnemyManager", "removeEnemy", "app.Ch200000", 0x140a67b00, 1, kRoster};
constexpr MethodRow kRegisterMonster{"register_monster", "app.MonsterManager", "registMonster", "app.Monster", 0x140b26bf0, 1, kRoster};
constexpr MethodRow kRemoveMonster{"remove_monster", "app.MonsterManager", "unregistMonster", "app.Monster", 0x140a0fc90, 1, kRoster};

// The mirror set (DD2 design world.md 1 "Observer copies"): the monster overrides of the puppet set's brain-off gates, so
// a guest's copy of a host enemy follows the host's stream instead of its own brain.
constexpr const char* kMirror = "mirror";

constexpr MethodRow kMonsterNavDestination{"monster_nav_destination", "app.MonsterNavigationController", "setDestination",
                                           "via.Position", 0x1404b2fc0, 1, kMirror};
constexpr MethodRow kMonsterLocomotion{"monster_locomotion", "app.MonsterActionSelector", "requestLocomotionImpl",
                                       "app.LocomotionSpeedTypeEnum,System.Boolean", 0x140346f60, 1, kMirror};
constexpr MethodRow kMonsterIdle{"monster_idle", "app.MonsterActionSelector", "requestIdleImpl", "System.Boolean", 0x1449fc510, 1,
                                 kMirror};

// The load-slot set: the one funnel every game load of the SaveDataManager goes through (requestLoadAuto passes slot 0,
// requestLoadManual and requestLoadSlot their slot, requestLoadSystemData -1); the slot is the file number data00<slot>.bin.
constexpr const char* kLoadSlot = "load_slot";

constexpr MethodRow kRequestLoad{"request_load", "app.SaveDataManager", "requestLoad",
                                 "System.Int32,app.SaveDataManager.SaveLoadRequestArgs,System.UInt32[]", 0x145c92980, 1, kLoadSlot};

// The title-menu set (src/title_menu.h): the title menu GUI (app.ui030201) every frame it is up, and its list selection (the one
// place a row of the menu is chosen: Continue, Load Game, New Game ...).
constexpr const char* kTitleMenu = "title_menu";

constexpr MethodRow kTitleUpdate{"title_update", "app.ui030201", "update", "", 0x145050fe0, 1, kTitleMenu};
constexpr MethodRow kTitleSelectList{"title_select_list", "app.ui030201", "onSelectList", "", 0x146089cd0, 1, kTitleMenu};

constexpr std::array<MethodRow, 36> kAll{kChangeFlowPhase, kChangeSceneType, kCompleteSceneLoad, kFerrystoneLoad, kReadyCharacter,
                                         kCharacterDestroy, kNavDestination, kPawnNavDestination, kActionCore, kLocomotion, kIdle,
                                         kDamageHp, kSetHp, kLeaveStrayPawn, kDestroyStrayPawn, kPawnLost, kSaveContexts,
                                         kLoadContexts, kSaveItems, kLoadItems, kAddPossessionPoint, kSetPossessionLv,
                                         kPawnPossessionLv, kMainPawnPossessionLv, kJudgePossession, kKillPossessed,
                                         kRegisterEnemy, kRemoveEnemy, kRegisterMonster, kRemoveMonster, kMonsterNavDestination,
                                         kMonsterLocomotion, kMonsterIdle, kRequestLoad, kTitleUpdate, kTitleSelectList};

// Native bodies shared by many methods (an empty method body, a trivial setter): never hooked, whatever a row says.
constexpr std::array<uintptr_t, 1> kSharedBodies{0x145ab65c0};  // shared by 48,893 methods, among them every empty .ctor

// Enum types read at start through the TDB (their literal values are not in the dump).
constexpr const char* kPhaseEnum = "app.MainFlowManager.Phase";
constexpr const char* kSceneTypeEnum = "app.MainFlowManager.SceneType";
constexpr const char* kSpeedTypeEnum = "app.LocomotionSpeedTypeEnum";

}  // namespace names
