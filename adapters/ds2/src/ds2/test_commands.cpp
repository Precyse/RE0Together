// DEATH STRANDING 2: test commands for live checks on a session copy (adapter.ini test_commands=1, off by default).
// Each command is a small text file dropped in <game>\coop; the simulation tick reads it, runs it and deletes it:
//   tp.txt         "x y z"    teleport the local player there ("0 0 0" only logs the position)
//   area.txt       "id"       RequestChangeArea to that area (0 = the current one)
//   addweapon.txt  "id"       give the weapon as a cargo piece, then AddWeapon refreshes it (-1 lists the weapon config ids)
//   bt.txt         "r on"     SetBtActiveRegion(region, on)
//   travel.txt     "x y z"    the game's own fast travel (FastTravelPlayerToWorldTransform) after taking the remote body down
// A vectored exception handler also logs the address of every access violation inside the game's image, which names the
// code behind a crash the adapter's own guards swallow.
#include "ds2/test_commands.h"

#include <windows.h>

#include <cstdio>
#include <string>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/place.h"
#include "ds2/remote_player.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "paths.h"

namespace {

constexpr uintptr_t kChangeArea = 0x140709cc0;        // RequestChangeArea(unused, u16 area, bool, transform*, i32, bool)
constexpr uintptr_t kAddWeapon = 0x140d9bbc0;         // DSPlayerSystem_sExportedAddWeapon(u16 weapon id)
constexpr uintptr_t kFastTravel = 0x140703280;        // (FastTravelSystem*, const WorldTransform*, const GGUUID*, bool)
constexpr uintptr_t kNullUuid = 0x142d91fd0;
constexpr uintptr_t kGameModuleGlobal = 0x14623E338, kFastTravelSystem = 0x5A0;
constexpr uintptr_t kSetBtRegion = 0x141f09920;       // SetBtActiveRegion(u8 region, u8 active)
constexpr uintptr_t kWeaponConfigList = 0x14623FA50;  // +0x30 count, +0x38 entry pointers, entry +0x20 u16 id
constexpr uintptr_t kWeaponCount = 0x30, kWeaponEntries = 0x38, kWeaponId = 0x20, kWeaponListItem = 0x28;
constexpr uintptr_t kBaggageCatalogue = 0x14623E540;  // +0x18 count, +0x20 the DSGameBaggageListItem pointers
constexpr uintptr_t kCatalogueCount = 0x18, kCatalogueItems = 0x20, kItemContents = 0x50, kItemKind = 0x44;
constexpr uintptr_t kAreaOffset = 0x60;               // baggage owner +0x60: the player's current area
constexpr int kMaxListedConfigs = 40;
constexpr size_t kMaxFaultsLogged = 30;
constexpr size_t kStackWordsScanned = 96;
constexpr int kReturnsLogged = 10;
constexpr ULONGLONG kPollMs = 500;
constexpr uintptr_t kImageSpan = 0x20000000;
constexpr DWORD kFaultCodes[] = {EXCEPTION_ACCESS_VIOLATION, EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_STACK_OVERFLOW};

// The text of a command file, deleted on the way; empty when there is none.
std::string takeCommand(const wchar_t* name) {
    const std::wstring path = coopDirectory() + L"\\" + name;
    FILE* file = _wfopen(path.c_str(), L"r");
    if (!file) return {};
    char text[128] = {};
    fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    DeleteFileW(path.c_str());
    return text;
}

bool guardedCall(void (*call)(const int*), const int* args) {
    __try {
        call(args);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void teleport(const std::string& text) {
    double x = 0, y = 0, z = 0;
    decima::WorldTransform where{};
    const uintptr_t sam = remote_player::samEntity();
    if (sscanf(text.c_str(), "%lf %lf %lf", &x, &y, &z) != 3 || !ds2::entityTransform(sam, where)) return;
    logger::write("test_commands: the player is at %.1f %.1f %.1f", where.position.x, where.position.y, where.position.z);
    if (x == 0 && y == 0) return;
    where.position = {x, y, z};
    logger::write("test_commands: teleport to %.1f %.1f %.1f %s", x, y, z, ds2::teleportEntity(sam, where) ? "done" : "faulted");
}

void changeArea(const std::string& text) {
    int area = 0;
    if (sscanf(text.c_str(), "%d", &area) != 1) return;
    decima::WorldTransform where{};
    if (!ds2::entityTransform(remote_player::samEntity(), where)) return;
    bool ok = false;
    __try {
        ok = reinterpret_cast<bool (*)(uintptr_t, uint16_t, bool, const void*, int32_t, bool)>(ds2::at(kChangeArea))(
            0, static_cast<uint16_t>(area), true, &where, -1, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logger::write("test_commands: RequestChangeArea faulted");
    }
    logger::write("test_commands: RequestChangeArea(%d) -> %d", area, ok);
}

void addWeapon(const std::string& text) {
    int id = 0;
    if (sscanf(text.c_str(), "%d", &id) != 1) return;
    if (id < 0) {
        const uintptr_t list = decima::readPointer(ds2::at(kWeaponConfigList));
        const int32_t count = list ? ds2::field<int32_t>(list, kWeaponCount) : 0;
        const uintptr_t entries = list ? decima::readPointer(list + kWeaponEntries) : 0;
        logger::write("test_commands: %d weapon configs", count);
        for (int32_t i = 0; entries && i < count && i < kMaxListedConfigs; ++i) {
            const uintptr_t entry = decima::readPointer(entries + i * sizeof(uintptr_t));
            logger::write("test_commands: config %d id %u", i, entry ? ds2::field<uint16_t>(entry, kWeaponId) : 0);
        }
        return;
    }
    // A weapon is a cargo piece: its baggage kind is the catalogue item whose contents are the weapon config's list item;
    // creating that piece makes the player's weapon table (and the wheel) list the weapon. AddWeapon itself only refreshes a
    // weapon the player already has.
    const uintptr_t list = decima::readPointer(ds2::at(kWeaponConfigList));
    const int32_t configs = list ? ds2::field<int32_t>(list, kWeaponCount) : 0;
    const uintptr_t entries = list ? decima::readPointer(list + kWeaponEntries) : 0;
    uintptr_t listItem = 0;
    for (int32_t i = 0; entries && i < configs && !listItem; ++i) {
        const uintptr_t entry = decima::readPointer(entries + i * sizeof(uintptr_t));
        if (entry && ds2::field<uint16_t>(entry, kWeaponId) == id) listItem = decima::readPointer(entry + kWeaponListItem);
    }
    const uintptr_t catalogue = decima::readPointer(ds2::at(kBaggageCatalogue));
    const int32_t items = catalogue ? ds2::field<int32_t>(catalogue, kCatalogueCount) : 0;
    const uintptr_t itemArray = catalogue ? decima::readPointer(catalogue + kCatalogueItems) : 0;
    uint32_t kind = 0;
    for (int32_t i = 0; listItem && itemArray && i < items && !kind; ++i) {
        const uintptr_t item = decima::readPointer(itemArray + i * sizeof(uintptr_t));
        if (item && decima::readPointer(item + kItemContents) == listItem) kind = ds2::field<uint32_t>(item, kItemKind);
    }
    logger::write("test_commands: weapon %d: list item %p, baggage kind %u", id, reinterpret_cast<void*>(listItem), kind);
    if (!kind) return;
    game::Cargo piece;
    piece.type = kind;
    logger::write("test_commands: AddWeapon(%d) as cargo %s", id, game::addCargo(piece) == game::AddResult::Done ? "requested" : "not done");
    guardedCall([](const int* a) { reinterpret_cast<void (*)(uint16_t)>(ds2::at(kAddWeapon))(static_cast<uint16_t>(*a)); }, &id);
}

void fastTravel(const std::string& text) {
    double x = 0, y = 0, z = 0;
    decima::WorldTransform where{};
    if (sscanf(text.c_str(), "%lf %lf %lf", &x, &y, &z) != 3 || !ds2::entityTransform(remote_player::samEntity(), where)) return;
    where.position = {x, y, z};
    const uintptr_t module = decima::readPointer(ds2::at(kGameModuleGlobal));
    const uintptr_t pointed = module ? decima::readPointer(module + kFastTravelSystem) : 0;
    const uintptr_t system = pointed ? pointed : module + kFastTravelSystem;  // the system is a pointer field of the game module
    logger::write("test_commands: fast travel system %p (field value %p)", reinterpret_cast<void*>(system), reinterpret_cast<void*>(pointed));
    remote_player::leave("fast travel");
    bool ok = false;
    __try {
        reinterpret_cast<void (*)(uintptr_t, const void*, const void*, bool)>(ds2::at(kFastTravel))(system, &where, reinterpret_cast<const void*>(ds2::at(kNullUuid)), true);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    logger::write("test_commands: fast travel to %.1f %.1f %.1f %s", x, y, z, ok ? "requested" : "faulted");
}

void setBtRegion(const std::string& text) {
    int args[2] = {0, 0};
    if (sscanf(text.c_str(), "%d %d", &args[0], &args[1]) != 2) return;
    const bool ok = guardedCall([](const int* a) { reinterpret_cast<void (*)(uint8_t, uint8_t)>(ds2::at(kSetBtRegion))(static_cast<uint8_t>(a[0]), static_cast<uint8_t>(a[1])); }, args);
    logger::write("test_commands: SetBtActiveRegion(%d, %d) %s", args[0], args[1], ok ? "done" : "faulted");
}

void tick() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < kPollMs) return;
    last = now;
    if (const std::string text = takeCommand(L"tp.txt"); !text.empty()) teleport(text);
    if (const std::string text = takeCommand(L"area.txt"); !text.empty()) changeArea(text);
    if (const std::string text = takeCommand(L"addweapon.txt"); !text.empty()) addWeapon(text);
    if (const std::string text = takeCommand(L"bt.txt"); !text.empty()) setBtRegion(text);
    if (const std::string text = takeCommand(L"travel.txt"); !text.empty()) fastTravel(text);
}

// The values on the stack that point into the game's image: the likely return addresses, nearest first.
void logStackReturns(uintptr_t stack, uintptr_t base) {
    std::string line;
    int found = 0;
    for (size_t i = 0; i < kStackWordsScanned && found < kReturnsLogged; ++i) {
        uintptr_t value = 0;
        if (!decima::safeRead(stack + i * sizeof(uintptr_t), value)) break;
        if (value < base || value - base > kImageSpan) continue;
        char text[32];
        snprintf(text, sizeof(text), " %llx", static_cast<unsigned long long>(value - base + ds2::kImageBase));
        line += text;
        ++found;
    }
    logger::write("test_commands:   stack returns:%s", line.c_str());
}

LONG CALLBACK faultLogger(EXCEPTION_POINTERS* info) {
    static size_t logged = 0;
    const uintptr_t at = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = ds2::at(ds2::kImageBase);
    if (at < base || at - base > kImageSpan || logged >= kMaxFaultsLogged) return EXCEPTION_CONTINUE_SEARCH;
    for (const DWORD code : kFaultCodes) {
        if (info->ExceptionRecord->ExceptionCode != code) continue;
        ++logged;
        const ULONG_PTR target = info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
        logger::write("test_commands: fault %08lx in the game at file va %p, address %p", code,
                      reinterpret_cast<void*>(at - base + ds2::kImageBase), reinterpret_cast<void*>(target));
        logStackReturns(info->ContextRecord->Rsp, base);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace test_commands {

void installEarly() {
    AddVectoredExceptionHandler(1, faultLogger);
    sim_tick::add(&tick, "test commands");
}

}  // namespace test_commands
