#include "profile.h"

#include <windows.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "calls.h"
#include "game_state_rule.h"
#include "hook_registry.h"
#include "log.h"
#include "local_body.h"
#include "look.h"
#include "names.h"
#include "profile_rule.h"
#include "refw.h"
#include "save_notice.h"
#include "save_redirect.h"
#include "save_sections.h"
#include "serializer.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

using profile_rule::Kind;
using profile_rule::Owner;
using profile_rule::Piece;

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kFirstParam = 2;
constexpr uint32_t kMoneyGold = 0x10;  // MoneyBackup._Gold
constexpr const char* kStatusType = "app.HumanStatusContext";
constexpr const char* kJobType = "app.JobContext";

// What the last save wrote, per section (the save hooks may run off the game thread).
struct Captured {
    std::vector<Piece> contexts, items;
    int32_t level = 0, job = 0, equipIndex = 0, gold = 0;
    bool haveContexts = false, haveItems = false, dirty = false;
};

std::mutex g_mutex;
Captured g_captured;                                // under g_mutex
std::shared_ptr<const profile_rule::Profile> g_profile;  // splice mode: read at install, replaced by each write-back; under g_mutex
std::atomic<bool> g_spliced{false}, g_checked{false};  // set by the loaders, read by the tick

// The state of the guest's character (game_state_rule::Char). `g_base` is what install found: Off (not a splice guest),
// None (no readable profile), Refused (hooks or rows missing), Ok (hooks in). On top of Ok, the loads: each loader notes
// whether it refused, the check after the load whether the Arisen is the profile's. `g_writeBack`: this load was spliced
// and checked ok, so the guest's saves may be written back to the character file.
std::atomic<game_state_rule::Char> g_base{game_state_rule::Char::Off};
std::atomic<bool> g_contextsRefused{false}, g_itemsRefused{false}, g_mismatch{false}, g_writeBack{false};

std::wstring g_path;  // the character file (src/save_folders.h), set at install

std::shared_ptr<const profile_rule::Profile> currentProfile() {
    std::lock_guard lock(g_mutex);
    return g_profile;
}

const std::wstring& profilePath() { return g_path; }

void* returned(void** retVal) { return retVal ? *retVal : nullptr; }

// Capture

// The Arisen's and the main pawn's contexts from a ContextDBMSSaveData section, as the game saves or loads it.
void captureContexts(void* section) {
    void* records = save_sections::records(section);
    if (!records) return;
    Captured part;
    const size_t n = save_sections::count(calls::kRecordListCount, records);
    for (size_t r = 0; r < n; ++r) {
        void* record = save_sections::item(calls::kRecordListItem, records, r);
        const auto character = save_sections::recordCharacter(record);
        const auto owner = character ? save_sections::ownerOf(*character) : std::nullopt;
        if (!owner) continue;
        void* contexts = refw::field<void*>(record, save_sections::kRecordContexts);
        const size_t count = save_sections::count(calls::kContextListCount, contexts);
        for (size_t i = 0; i < count; ++i) {
            void* context = save_sections::item(calls::kContextListItem, contexts, i);
            const std::string type = refw::typeNameOf(context);
            if (type.empty() || save_sections::worldOwned(type)) continue;
            const auto json = serializer::toJson(type, context);
            if (!json) {
                logger::write("profile: %s of the %s refused by the serializer, capture dropped", type.c_str(), profile_rule::ownerName(*owner));
                return;
            }
            if (*owner == Owner::Arisen && type == kStatusType) part.level = refw::field<int32_t>(context, save_sections::kStatusLevel);
            if (*owner == Owner::Arisen && type == kJobType) part.job = refw::field<int32_t>(context, save_sections::kJobCurrent);
            part.contexts.push_back({Kind::Context, *owner, type, *json});
        }
    }
    std::lock_guard lock(g_mutex);
    g_captured.contexts = std::move(part.contexts);
    g_captured.level = part.level;
    g_captured.job = part.job;
    g_captured.haveContexts = g_captured.dirty = true;
}

// Their item records, money and equipment set from an ItemManager.GameSaveData section, as the game saves or loads it.
void captureItems(void* section) {
    if (!save_sections::isType(section, save_sections::kItemSection)) return;
    Captured part;
    void* items = refw::field<void*>(section, save_sections::kItemSaves);
    const size_t n = save_sections::count(calls::kItemListCount, items);
    for (size_t i = 0; i < n; ++i) {
        void* item = save_sections::item(calls::kItemListItem, items, i);
        const auto owner = item ? save_sections::ownerOf(refw::field<uint32_t>(item, save_sections::kItemCharacter)) : std::nullopt;
        if (!owner) continue;
        const std::string type = refw::typeNameOf(item);
        const auto json = serializer::toJson(type, item);
        if (!json) {
            logger::write("profile: an item record (%s) refused by the serializer, capture dropped", type.c_str());
            return;
        }
        part.items.push_back({Kind::Item, *owner, type, *json});
    }
    void* money = refw::field<void*>(section, save_sections::kMoneySave);
    const auto moneyJson = serializer::toJson(save_sections::kMoneyType, money);
    if (!moneyJson) {
        logger::write("profile: money refused by the serializer, capture dropped");
        return;
    }
    part.items.push_back({Kind::Money, Owner::Arisen, save_sections::kMoneyType, *moneyJson});
    std::lock_guard lock(g_mutex);
    g_captured.items = std::move(part.items);
    g_captured.gold = refw::field<int32_t>(money, kMoneyGold);
    g_captured.equipIndex = refw::field<int32_t>(section, save_sections::kEquipIndex);
    g_captured.haveItems = g_captured.dirty = true;
}

void postSaveContexts(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    captureContexts(returned(retVal));
    save_notice::onSaved();
}
void postSaveItems(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    captureItems(returned(retVal));
    save_notice::onSaved();
}

// A guest's saves go back to its character file, but only from a load that was spliced and checked ok.
void postGuestSaveContexts(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (g_writeBack) captureContexts(returned(retVal));
}
void postGuestSaveItems(void** retVal, REFrameworkTypeDefinitionHandle, unsigned long long) {
    if (g_writeBack) captureItems(returned(retVal));
}

// Loading a save captures it too, so a character is carried once its save was loaded with the mod, saved or not.
int preCaptureLoadContexts(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc > kFirstParam) captureContexts(argv[kFirstParam]);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}
int preCaptureLoadItems(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc > kFirstParam) captureItems(argv[kFirstParam]);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

// Writes the character file whole: a temp file moved over it, the file it replaces kept once as `.bak`.
bool writeFile(const std::string& text) {
    const std::wstring path = profilePath(), part = path + L".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!(out << text)) return false;
    }
    CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);  // fails when there is no file yet, which is fine
    return MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool writeCapture() {
    profile_rule::Profile out;
    int32_t gold = 0;
    {
        std::lock_guard lock(g_mutex);
        if (!g_captured.dirty || !g_captured.haveContexts || !g_captured.haveItems) return false;
        g_captured.dirty = false;
        out.level = g_captured.level;
        out.job = g_captured.job;
        out.equipIndex = g_captured.equipIndex;
        out.pieces = g_captured.contexts;
        out.pieces.insert(out.pieces.end(), g_captured.items.begin(), g_captured.items.end());
        gold = g_captured.gold;
    }
    out.tdbHash = refw::tdbHash();
    const bool written = writeFile(profile_rule::encode(out));
    logger::write("profile: %s level=%d job=%d pieces=%zu gold=%d", written ? "captured" : "NOT written", out.level, out.job,
                  out.pieces.size(), gold);
    if (written) {
        std::lock_guard lock(g_mutex);
        if (g_profile) g_profile = std::make_shared<const profile_rule::Profile>(std::move(out));  // the next load splices the progress
    }
    return written;
}

// Splice

// A load starts over what the last one decided: no write-back until the check after this load says ok, and nothing
// half captured from before is written.
void beginLoad() {
    g_writeBack = false;
    g_mismatch = false;
    std::lock_guard lock(g_mutex);
    g_captured = Captured{};
}

// A splice only into a save folder that is not the real one (rule: a refused redirect never splices).
bool spliceAllowed(std::atomic<bool>& refused, const char* section) {
    refused = !save_redirect::safeToLoad();
    if (refused) logger::write("splice: %s not spliced, the save redirect is not verified", section);
    return !refused;
}

int preLoadContexts(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    beginLoad();
    g_spliced = false;
    g_checked = false;
    const auto profile = currentProfile();
    void* records = argc > kFirstParam ? save_sections::records(argv[kFirstParam]) : nullptr;
    if (!profile || !records || !spliceAllowed(g_contextsRefused, "contexts")) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    size_t replaced = 0, added = 0, refused = 0, owners = 0;
    const size_t n = save_sections::count(calls::kRecordListCount, records);
    for (size_t r = 0; r < n; ++r) {
        void* record = save_sections::item(calls::kRecordListItem, records, r);
        const auto character = save_sections::recordCharacter(record);
        const auto owner = character ? save_sections::ownerOf(*character) : std::nullopt;
        if (!owner) continue;
        ++owners;
        void* contexts = refw::field<void*>(record, save_sections::kRecordContexts);
        for (const Piece& piece : profile->pieces) {
            if (piece.kind != Kind::Context || piece.owner != *owner) continue;
            void* context = serializer::fromJson(piece.type, piece.json);
            if (!context) {
                ++refused;
                continue;
            }
            const size_t count = save_sections::count(calls::kContextListCount, contexts);
            size_t at = count;
            for (size_t i = 0; i < count && at == count; ++i) {
                if (refw::typeNameOf(save_sections::item(calls::kContextListItem, contexts, i)) == piece.type) at = i;
            }
            if (at < count) {
                refw::invoke(calls::method(calls::kContextListSetItem), contexts, {reinterpret_cast<void*>(at), context});
                ++replaced;
            } else {
                refw::invoke(calls::method(calls::kContextListAdd), contexts, {context});
                ++added;
            }
            refw::release(context);
        }
    }
    g_contextsRefused = refused > 0;
    g_spliced = owners > 0;
    logger::write("splice: contexts of %zu characters, %zu replaced, %zu added, %zu refused", owners, replaced, added, refused);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preLoadItems(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    const auto profile = currentProfile();
    void* section = argc > kFirstParam ? argv[kFirstParam] : nullptr;
    if (!profile || !save_sections::isType(section, save_sections::kItemSection) || !spliceAllowed(g_itemsRefused, "items")) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
    void* items = refw::field<void*>(section, save_sections::kItemSaves);
    size_t removed = 0, added = 0, refused = 0;
    for (size_t i = save_sections::count(calls::kItemListCount, items); i-- > 0;) {
        void* item = save_sections::item(calls::kItemListItem, items, i);
        if (!item || !save_sections::ownerOf(refw::field<uint32_t>(item, save_sections::kItemCharacter))) continue;
        refw::invoke(calls::method(calls::kItemListRemoveAt), items, {reinterpret_cast<void*>(i)});
        ++removed;
    }
    for (const Piece& piece : profile->pieces) {
        if (piece.kind == Kind::Context) continue;
        void* object = serializer::fromJson(piece.type, piece.json);
        if (!object) {
            ++refused;
            continue;
        }
        if (piece.kind == Kind::Money) {
            refw::writeRef(section, save_sections::kMoneySave, object);
        } else {
            refw::invoke(calls::method(calls::kItemListAdd), items, {object});
            ++added;
        }
        refw::release(object);
    }
    refw::field<int32_t>(section, save_sections::kEquipIndex) = profile->equipIndex;
    g_itemsRefused = refused > 0;
    logger::write("splice: items %zu removed, %zu added, %zu refused", removed, added, refused);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

// After a splice, once in gameplay: the Arisen the game built carries the profile's level. Only an ok check (and no piece
// refused) lets this load's saves go back to the character file.
void checkSplice() {
    if (!g_spliced || g_checked || world_gate::place() != world_gate_rule::Place::InGame) return;
    const auto contexts = look::of(local_body::character(wire_dd2::kKindArisen));
    const auto profile = currentProfile();
    if (!contexts || !(*contexts)[0] || !profile) return;
    g_checked = true;
    const int32_t level = refw::field<int32_t>((*contexts)[0], save_sections::kStatusLevel);
    if (level == profile->level) {
        g_writeBack = !g_contextsRefused && !g_itemsRefused;
        logger::write("splice: ok level=%d job=%d%s", level, profile->job, g_writeBack ? "" : " (a piece was refused, progress not written back)");
    } else {
        g_mismatch = true;
        logger::write("splice: MISMATCH level %d, profile %d", level, profile->level);
    }
}

std::optional<profile_rule::Profile> readProfile() {
    std::ifstream in(profilePath(), std::ios::binary);
    if (!in) {
        logger::write("profile: none yet (load your own save once with the mod installed)");
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto out = profile_rule::decode(text.str());
    if (!out) {
        logger::write("profile: unreadable, not spliced");
        return std::nullopt;
    }
    if (out->tdbHash != refw::tdbHash()) {
        logger::write("profile: from another game build (tdb %08X, running %08X), not spliced; load your own save once", out->tdbHash,
                      refw::tdbHash());
        return std::nullopt;
    }
    return out;
}

}  // namespace

namespace profile {

bool install(Mode mode, const std::wstring& path) {
    g_path = path;
    const bool capture = mode == Mode::Capture;
    if (!capture) g_base = game_state_rule::Char::None;
    if (!calls::enabled(calls::Feature::Profile) || !save_sections::init()) {
        logger::write("%s: not hooked (profile calls or CharacterID literals missing)", names::kProfile);
        if (!capture) g_base = game_state_rule::Char::Refused;
        return false;
    }
    if (!capture) {
        auto read = readProfile();
        if (!read) return false;
        logger::write("profile: level=%d job=%d pieces=%zu read for the splice", read->level, read->job, read->pieces.size());
        g_profile = std::make_shared<const profile_rule::Profile>(std::move(*read));
    }
    const hook_registry::Handler captureHandlers[] = {
        hook_registry::make<nullptr, postSaveContexts>(&names::kSaveContexts),
        hook_registry::make<nullptr, postSaveItems>(&names::kSaveItems),
        hook_registry::make<preCaptureLoadContexts>(&names::kLoadContexts),
        hook_registry::make<preCaptureLoadItems>(&names::kLoadItems),
    };
    const hook_registry::Handler spliceHandlers[] = {
        hook_registry::make<preLoadContexts>(&names::kLoadContexts),
        hook_registry::make<preLoadItems>(&names::kLoadItems),
        hook_registry::make<nullptr, postGuestSaveContexts>(&names::kSaveContexts),
        hook_registry::make<nullptr, postGuestSaveItems>(&names::kSaveItems),
    };
    const bool hooked = capture ? hook_registry::installSet(names::kProfile, captureHandlers)
                                : hook_registry::installSet(names::kProfile, spliceHandlers);
    if (!capture) g_base = hooked ? game_state_rule::Char::Ok : game_state_rule::Char::Refused;
    return hooked;
}

game_state_rule::Char spliceState() {
    const game_state_rule::Char base = g_base;
    if (base != game_state_rule::Char::Ok) return base;
    if (g_contextsRefused || g_itemsRefused) return game_state_rule::Char::Refused;
    return g_mismatch ? game_state_rule::Char::Mismatch : game_state_rule::Char::Ok;
}

bool captureFrom(void* contextSection, void* itemSection) {
    captureContexts(contextSection);
    captureItems(itemSection);
    return writeCapture();
}

void tick(uint64_t) {
    checkSplice();
    writeCapture();
}

}  // namespace profile
