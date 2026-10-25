#include "appearance.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "appearance_rule.h"
#include "character_owner.h"
#include "debug_stats.h"
#include "equip_rule.h"
#include "equip_steps.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"
#include "settled_copy.h"

namespace {

using appearance_rule::kNoOutfit;
using appearance_rule::Role;
using character_owner::Character;
using character_owner::kCharacters;
using equip_steps::guarded;
using StartFunction = void(__fastcall*)(void* self, void* edx);
using SetCostumeFunction = bool(__fastcall*)(void* self, void* edx, int kind, uint32_t model);

constexpr size_t kCount = character_owner::kCharacterCount;
constexpr uint64_t kResendMs = 2000;
constexpr uint64_t kLoadTimeoutMs = 10'000;
constexpr wchar_t kDlcFolder[] = L"\\DLC";
constexpr wchar_t kDlcPackPrefix[] = L"costume_";
constexpr wchar_t kDlcCostumeFolder[] = L"\\nativePC\\costume\\";

// One outfit swap in progress on a character: sets released and requested, waiting for the load.
struct Job {
    bool active = false;
    bool fellBack = false;  // the requested outfit failed to load and the default was requested instead
    uint8_t to = kNoOutfit;
    int32_t weaponType = equip_rule::kNoWeapon;
    uint64_t startMs = 0;
};

NetClient* g_net = nullptr;
std::array<std::atomic<uint8_t>, kCount> g_ini{};     // the local player's choice per character: config, then the Co-op page
std::array<bool, kCount> g_iniApplied{};              // game thread: the ini choice was put on its character this game
std::array<std::atomic<uint8_t>, kCount> g_reported;  // the owner's latest APPEARANCE per character (net thread writes)
std::mutex g_pendingMutex;
std::array<appearance_rule::Pending, kCount> g_pending;  // guarded by g_pendingMutex: received before its owner was known
std::array<int, kCount> g_ownerSeen;                  // game thread: the owner slot last seen per character
std::array<Job, kCount> g_jobs;                       // game thread
std::array<uint8_t, kCount> g_failed{};               // game thread: the outfit a swap gave up on, not retried
std::array<uint8_t, kCount> g_lastSent{};             // game thread
std::array<uint64_t, kCount> g_lastSentMs{};          // game thread
std::map<uint8_t, bool> g_dlcCache;                   // game thread: model -> a DLC pack carries it
StartFunction g_originalStart = nullptr;
StartFunction g_originalSound = nullptr;
SetCostumeFunction g_originalSetCostume = nullptr;

size_t indexOf(Character character) { return static_cast<size_t>(character); }

int kindOf(Character character) {
    return character == Character::Rebecca ? game::kRebeccaCostumeKind : game::kBillyCostumeKind;
}

int playerIndexOf(Character character) {
    return character == Character::Rebecca ? game::kRebeccaPlayerIndex : game::kBillyPlayerIndex;
}

void* costumes() { return reinterpret_cast<void*>(game::readPointer(game::kPlayerCostumeGlobal)); }

// The character's current model byte, or kNoOutfit when the game is not ready.
uint8_t currentModel(Character character) {
    void* object = costumes();
    if (!object) return kNoOutfit;
    uint8_t model = kNoOutfit;
    guarded([&] { model = game::callThiscall<uint8_t>(game::kCurrentCostumeFunction, object, kindOf(character)); });
    return model;
}

// True when the character's outfit list holds the model (the family list setCostume searches).
bool listed(Character character, uint8_t model) {
    void* object = costumes();
    uint8_t found = kNoOutfit;
    if (!object) return false;
    guarded([&] {
        const uintptr_t entry = game::callThiscall<uintptr_t>(game::kFindCostumeFunction, object, kindOf(character), model);
        game::readMemory(entry, found);
    });
    return found == model;
}

// The game's setCostume past the suitcase guard.
bool setCostume(Character character, uint8_t model) {
    void* object = costumes();
    bool ok = false;
    if (object) guarded([&] { ok = g_originalSetCostume(object, nullptr, kindOf(character), model); });
    return ok;
}

// A DLC pack under <game>\DLC\<app id>\costume_*\nativePC\costume\ carries the model's .pcos (read-only lookup).
bool dlcCarries(uint8_t model) {
    const auto cached = g_dlcCache.find(model);
    if (cached != g_dlcCache.end()) return cached->second;
    const std::wstring root = gameDirectory() + kDlcFolder;
    wchar_t file[16];
    swprintf_s(file, L"em%02x.pcos", model);
    bool found = false;
    WIN32_FIND_DATAW app;
    const HANDLE apps = FindFirstFileW((root + L"\\*").c_str(), &app);
    for (BOOL more = apps != INVALID_HANDLE_VALUE; more && !found; more = FindNextFileW(apps, &app)) {
        if (app.cFileName[0] == L'.') continue;
        const std::wstring appFolder = root + L"\\" + app.cFileName;
        WIN32_FIND_DATAW pack;
        const HANDLE packs = FindFirstFileW((appFolder + L"\\" + kDlcPackPrefix + L"*").c_str(), &pack);
        for (BOOL next = packs != INVALID_HANDLE_VALUE; next && !found; next = FindNextFileW(packs, &pack)) {
            const std::wstring path = appFolder + L"\\" + pack.cFileName + kDlcCostumeFolder + file;
            found = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        }
        if (packs != INVALID_HANDLE_VALUE) FindClose(packs);
    }
    if (apps != INVALID_HANDLE_VALUE) FindClose(apps);
    g_dlcCache[model] = found;
    return found;
}

// The model if this machine can put it on the character (offered for it, and its DLC installed), else kNoOutfit.
uint8_t usable(Character character, uint8_t model) {
    if (model == kNoOutfit) return kNoOutfit;
    const appearance_rule::Outfit* outfit = appearance_rule::find(character, model);
    if (!outfit) return kNoOutfit;
    if (outfit->dlcAppId != appearance_rule::kNoDlc && !dlcCarries(model)) {
        logger::writeUnlessRepeated("appearance: %s %02x needs DLC %u, not installed here", character_owner::name(character),
                                    model, outfit->dlcAppId);
        return kNoOutfit;
    }
    return model;
}

Role roleOf(Character character) {
    if (character_owner::isRemoteOwned(character)) return Role::Remote;
    return character_owner::plannedLocalCharacter() == character ? Role::Local : Role::Unowned;
}

uint8_t wanted(Character character) {
    const size_t i = indexOf(character);
    return usable(character, appearance_rule::wantedModel(roleOf(character), g_iniApplied[i], g_ini[i], g_reported[i]));
}

// Game start: the ini choice and the reported outfits go into the save record and the current slot before any player
// exists; the room load then builds the players with them.
void applyAtStart() {
    g_iniApplied.fill(false);
    for (const Character character : kCharacters) {
        const size_t i = indexOf(character);
        const uint8_t before = currentModel(character);
        const uint8_t model = wanted(character);
        if (roleOf(character) == Role::Local) g_iniApplied[i] = true;
        if (!appearance_rule::needsChange(before, model)) continue;
        const bool ok = listed(character, model) && setCostume(character, model);
        logger::write("appearance: start %s %02x -> %02x%s", character_owner::name(character), before, model,
                      ok ? "" : " refused by the game");
    }
}

void __fastcall startDetour(void* self, void* edx) {
    g_originalStart(self, edx);
    guarded(applyAtStart);
}

// The voice bank follows Wesker mode only at game start; a Wesker outfit gets Wesker's voice (voice_2) there too.
void __fastcall soundDetour(void* self, void* edx) {
    g_originalSound(self, edx);
    guarded([] {
        const uint8_t pending = wanted(Character::Billy);
        const uint8_t model = pending != kNoOutfit ? pending : currentModel(Character::Billy);
        const uintptr_t sound = game::readPointer(game::kSoundGlobal);
        const uintptr_t language = game::readPointer(game::kLanguageGlobal);
        if (model != appearance_rule::kBillyWesker || !sound || !language) return;
        const int spoken = game::callThiscall<int>(game::kLanguageFunction, reinterpret_cast<void*>(language));
        const int variant = game::kVoiceWeskerVariant + (spoken != game::kLanguageEnglish ? 1 : 0);
        game::callThiscall<void>(game::kVoiceVariantFunction, reinterpret_cast<void*>(sound), variant);
        logger::write("appearance: voice variant %d", variant);
    });
}

// The suitcase offers both characters' outfits, but the partner's character wears what its owner chose: the change is
// answered as done and dropped. The adapter's own changes go through g_originalSetCostume.
bool __fastcall setCostumeDetour(void* self, void* edx, int kind, uint32_t model) {
    const Character character = kind >= game::kFirstRebeccaCostumeKind && kind <= game::kLastRebeccaCostumeKind
                                    ? Character::Rebecca
                                    : Character::Billy;
    if (game_state::menuOpen() && character_owner::isRemoteOwned(character)) {
        logger::write("appearance: suitcase change of %s to %02x refused, the partner chooses it",
                      character_owner::name(character), model);
        return true;
    }
    return g_originalSetCostume(self, edx, kind, model);
}

void giveUp(Character character, Job& job, const char* why) {
    logger::write("appearance: %s %02x abandoned: %s", character_owner::name(character), job.to, why);
    g_failed[indexOf(character)] = job.to;
    job = Job{};
}

// The menu close's outfit step (0x5d8040, state 0xb): release the old set while the old model is still current, set
// the new model, request its set.
bool begin(Character character, uintptr_t player, uint8_t to, Job& job) {
    void* chara = equip_steps::gameChara();
    uint32_t record = 0;
    if (!chara || !game::readMemory(player + game::kPlayerSceneRecordOffset, record) || !listed(character, to)) return false;
    job = Job{true, false, to, equip_steps::heldType(player), monotonicMs()};
    equip_steps::releaseSet(chara, kindOf(character), job.weaponType);
    if (!setCostume(character, to)) return false;
    equip_steps::requestSet(chara, kindOf(character), job.weaponType, record);
    return true;
}

// Once every request loaded: a failed resource falls back to the default outfit (once), else the player's resource is
// rebound and its weapon equipped again. False while the default is still loading.
bool finish(Character character, uintptr_t player, Job& job) {
    void* chara = equip_steps::gameChara();
    int kind = kindOf(character);
    if (game::callThiscall<bool>(game::kResourceFailedFunction, chara, &kind) && !job.fellBack) {
        uint32_t record = 0;
        game::readMemory(player + game::kPlayerSceneRecordOffset, record);
        equip_steps::releaseSet(chara, kindOf(character), job.weaponType);
        setCostume(character, appearance_rule::defaultModel(character));
        equip_steps::requestSet(chara, kindOf(character), job.weaponType, record);
        job.fellBack = true;
        logger::write("appearance: %s %02x failed to load, default requested", character_owner::name(character), job.to);
        return false;
    }
    void* sPlayer = reinterpret_cast<void*>(game::readPointer(game::kPlayerGlobal));
    game::callThiscall<void>(game::kPlayerRebuildFunction, sPlayer, playerIndexOf(character));
    equip_steps::equip(player);
    return true;
}

void advance(Character character, uintptr_t player, Job& job) {
    if (monotonicMs() - job.startMs > kLoadTimeoutMs) return giveUp(character, job, "load timed out");
    bool loaded = false;
    if (!guarded([&] { loaded = equip_steps::requestsLoaded(); })) return giveUp(character, job, "load check faulted");
    if (!loaded) return;
    bool done = false;
    if (!guarded([&] { done = finish(character, player, job); })) return giveUp(character, job, "rebuild faulted");
    if (!done) return;
    logger::write("appearance: %s -> %02x loaded after %llu ms (%s)", character_owner::name(character), job.to,
                  monotonicMs() - job.startMs, roleOf(character) == Role::Local ? "local" : "remote");
    debug_stats::count(debug_stats::Counter::AppearanceSwaps);
    g_iniApplied[indexOf(character)] = true;
    job = Job{};
}

// The owner tells the peer what its character wears: on a change and periodically, so a late peer catches up.
void report(Character character, uint8_t current) {
    const size_t i = indexOf(character);
    if (current == g_lastSent[i] && monotonicMs() - g_lastSentMs[i] < kResendMs) return;
    const appearance_rule::Appearance payload = appearance_rule::encode(character, current);
    if (!g_net->send(appearance_rule::kMsgAppearance, true, proto::kSlotAll, proto::bytesOf(payload))) return;
    if (current != g_lastSent[i]) logger::write("appearance: %s %02x reported", character_owner::name(character), current);
    g_lastSent[i] = current;
    g_lastSentMs[i] = monotonicMs();
}

void reconcile(Character character) {
    const size_t i = indexOf(character);
    Job& job = g_jobs[i];
    const uint8_t current = currentModel(character);
    const uintptr_t player = character_owner::find(character);
    if (current == kNoOutfit) return;
    if (job.active) return advance(character, player, job);
    const Role role = roleOf(character);
    if (role == Role::Local) report(character, current);
    const uint8_t model = wanted(character);
    if (!appearance_rule::needsChange(current, model)) {
        if (role == Role::Local) g_iniApplied[i] = true;
        return;
    }
    if (model == g_failed[i] || !player || !equip_steps::inPlainGameplay(player)) return;
    bool started = false;
    if (!guarded([&] { started = begin(character, player, model, job); }) || !started) {
        job.to = model;
        giveUp(character, job, "the game refused or faulted");
    }
}

void acceptReported(Character character, uint8_t model) {
    if (g_reported[indexOf(character)].exchange(model) != model) {
        logger::write("appearance: %s %02x received", character_owner::name(character), model);
    }
}

void logDropped(Character character, uint8_t slot) {
    logger::write("appearance: %s from slot %u dropped, not its owner", character_owner::name(character), slot);
}

// A held APPEARANCE is applied or dropped once its character's owner is decided.
void resolvePending(Character character) {
    appearance_rule::Pending pending;
    {
        const std::lock_guard<std::mutex> lock(g_pendingMutex);
        appearance_rule::Pending& held = g_pending[indexOf(character)];
        const appearance_rule::Verdict verdict = appearance_rule::judgeSender(character_owner::ownerSlot(character), held.senderSlot);
        if (!held.held || verdict == appearance_rule::Verdict::Hold) return;
        pending = held;
        held = appearance_rule::Pending{};
    }
    if (character_owner::isOwnedBy(character, pending.senderSlot)) acceptReported(character, pending.model);
    else logDropped(character, pending.senderSlot);
}

// A newly decided owner makes this machine announce its own outfit again, so a partner that joined earlier gets it.
void announceOnOwnership(Character character) {
    const size_t i = indexOf(character);
    const int owner = character_owner::ownerSlot(character);
    if (owner == g_ownerSeen[i]) return;
    g_ownerSeen[i] = owner;
    if (owner != control_rule::kNoOwner) g_lastSentMs[i] = 0;
}

void onTick() {
    for (const Character character : kCharacters) {
        resolvePending(character);
        announceOnOwnership(character);
        reconcile(character);
    }
}

}  // namespace

namespace appearance {

bool enable(NetClient& net, const Config& config) {
    g_net = &net;
    g_ownerSeen.fill(control_rule::kNoOwner);
    for (size_t i = 0; i < kCount; ++i) g_ini[i] = config.outfit[i];
    game_tick::addCallback("appearance", onTick);
    return hooks::install("game start", game::kGameStartFunction, reinterpret_cast<void*>(&startDetour),
                          reinterpret_cast<void**>(&g_originalStart)) &&
           hooks::install("game start sound", game::kGameStartSoundFunction, reinterpret_cast<void*>(&soundDetour),
                          reinterpret_cast<void**>(&g_originalSound)) &&
           hooks::install("sPlayerCostume::setCostume", game::kSetCostumeFunction, reinterpret_cast<void*>(&setCostumeDetour),
                          reinterpret_cast<void**>(&g_originalSetCostume));
}

uint8_t outfitOf(Character character) { return currentModel(character); }

void setChoice(Character character, uint8_t model) {
    const size_t i = indexOf(character);
    if (i >= kCount || g_ini[i].exchange(model) == model) return;
    logger::write("appearance: %s chose outfit 0x%02x for the next game start", character_owner::name(character), model);
}

void onFrame(const GameFrame& frame) {
    const std::optional<appearance_rule::Appearance> received = appearance_rule::decode(frame.payload);
    if (!received) return;
    const auto character = static_cast<Character>(received->character);
    switch (appearance_rule::judgeSender(character_owner::ownerSlot(character), frame.slot)) {
        case appearance_rule::Verdict::Hold: {
            const std::lock_guard<std::mutex> lock(g_pendingMutex);
            g_pending[indexOf(character)] = {true, received->model, frame.slot};
            return;
        }
        case appearance_rule::Verdict::Apply: return acceptReported(character, received->model);
        case appearance_rule::Verdict::Drop: return logDropped(character, frame.slot);
    }
}

void uninstall() {
    hooks::remove(game::kGameStartFunction);
    hooks::remove(game::kGameStartSoundFunction);
    hooks::remove(game::kSetCostumeFunction);
}

}  // namespace appearance
