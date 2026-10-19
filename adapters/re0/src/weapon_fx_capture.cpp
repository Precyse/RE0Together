#include "weapon_fx_capture.h"

#include <intrin.h>

#include <array>
#include <atomic>
#include <utility>

#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "hooks.h"
#include "join_sync.h"
#include "log.h"
#include "pose_wire.h"
#include "state_net.h"
#include "stream_clock.h"
#include "weapon_fx_game.h"
#include "weapon_fx_matrix.h"
#include "weapon_fx_sites.h"
#include "weapon_fx_wire.h"

namespace {

using debug_stats::Counter;
using weapon_fx_sites::Primitive;
using weapon_fx_wire::Entry;
using weapon_fx_wire::Op;

constexpr uint32_t kLogIntervalMs = 1000;
constexpr uint32_t kNoExtra = 0;  // sEffect::create's last argument at every listed site

weapon_fx_game::PlaySeFunction g_originalPlaySe = nullptr;
weapon_fx_game::GenericSeFunction g_originalGenericSe = nullptr;
weapon_fx_game::ShootFunction g_originalShoot = nullptr;
weapon_fx_game::MuzzleFlashFunction g_originalMuzzleFlash = nullptr;
weapon_fx_game::EffectCreateFunction g_originalEffectCreate = nullptr;
weapon_fx_game::PlayerFunction g_originalStow = nullptr;
weapon_fx_game::PlayerFunction g_originalWick = nullptr;
weapon_fx_game::RippleFunction g_originalRipple = nullptr;

bool g_installed = false;

std::atomic<uint32_t> g_windowUnlisted{0};  // sEffect::create is also called off the game thread

// Game thread only: listed sites are handler code.
weapon_fx_wire::Batch g_batch;
uint32_t g_seq = 0;
uint32_t g_windowSent = 0;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;

// Primitives that run inside a recorded one (a shoot makes its own effects, a muzzle flash makes an effect) are part
// of it: only the outermost call is looked at.
thread_local int t_depth = 0;

class Scope {
public:
    Scope() : outermost_(t_depth++ == 0) {}
    ~Scope() { --t_depth; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    bool outermost() const { return outermost_; }

private:
    bool outermost_;
};

void flush() {
    if (g_batch.entries.empty()) return;
    const uint32_t count = static_cast<uint32_t>(g_batch.entries.size());
    if (join_sync::caughtUp()) {
        g_batch.senderMs = pose_wire::clockMs();
        g_batch.seq = ++g_seq;
        g_batch.character = static_cast<uint8_t>(character_owner::localCharacter());
        const auto payload = weapon_fx_wire::encode(g_batch);
        if (payload && state_net::send(weapon_fx_wire::kMsgWeaponFx, true, *payload)) {
            debug_stats::count(Counter::WeaponFxOut, count);
            g_windowSent += count;
        }
    }
    g_batch.entries.clear();
}

void noteUnlisted() {
    debug_stats::count(Counter::WeaponFxUnlisted);
    ++g_windowUnlisted;
}

// Keeps `entry` for this frame's message, or counts it when the receiver's own rules would refuse it.
void add(const Entry& entry) {
    if (!weapon_fx_wire::acceptable(entry)) {
        noteUnlisted();
        return;
    }
    if (g_batch.entries.size() >= weapon_fx_wire::kMaxEntries) flush();
    g_batch.entries.push_back(entry);
}

// The site id of a call to `primitive` returning to `caller`; an owned call from anywhere else is counted.
uint8_t admit(Primitive primitive, uintptr_t caller) {
    const uint8_t site = weapon_fx_sites::find(primitive, caller);
    if (site == weapon_fx_sites::kNoSite) noteUnlisted();
    return site;
}

bool owned(uintptr_t player) { return player && player == character_owner::localPlayer(); }

bool readVector(const float* address, float (&out)[3]) { return game::readMemory(reinterpret_cast<uintptr_t>(address), out); }

void recordSound(Primitive primitive, uintptr_t caller, uintptr_t chara, uint32_t soundId, float volume) {
    if (!owned(chara)) return;
    const uint8_t site = admit(primitive, caller);
    if (site == weapon_fx_sites::kNoSite || !std::in_range<uint16_t>(soundId)) return;
    Entry entry;
    entry.op = Op::Se;
    entry.site = site;
    entry.number = static_cast<uint16_t>(soundId);
    entry.level = volume;
    add(entry);
}

void recordFlash(uintptr_t caller, uintptr_t player, uint32_t index, float scale) {
    if (!owned(player)) return;
    const uint8_t site = admit(Primitive::MuzzleFlash, caller);
    if (site == weapon_fx_sites::kNoSite || !std::in_range<uint8_t>(index)) return;
    Entry entry;
    entry.op = Op::Flash;
    entry.site = site;
    entry.kind = static_cast<uint8_t>(index);
    entry.level = scale;
    add(entry);
}

// A projectile also carries where it leaves and the aim pitches its launch reads from the shooter.
bool fillProjectile(uintptr_t player, const float* matrix, Entry& entry) {
    float m[weapon_fx_matrix::kFloats];
    if (!game::readMemory(reinterpret_cast<uintptr_t>(matrix), m)) return false;
    weapon_fx_matrix::split(m, entry.pos, entry.quat);
    game::readMemory(player + game::kPlayerAutoPitchOffset, entry.autoPitch);
    game::readMemory(player + game::kPlayerManualPitchOffset, entry.manualPitch);
    entry.weapon = static_cast<uint32_t>(weapon_fx_game::weaponTypeOf(player));
    return true;
}

void recordShoot(uintptr_t caller, uintptr_t player, const float* matrix, uint32_t kind) {
    if (!owned(player)) return;
    const uint8_t site = admit(Primitive::Shoot, caller);
    if (site == weapon_fx_sites::kNoSite || !std::in_range<uint8_t>(kind)) return;
    Entry entry;
    entry.op = Op::Shoot;
    entry.site = site;
    entry.kind = static_cast<uint8_t>(kind);
    entry.muzzle = weapon_fx_sites::kSites[site].muzzle;
    if (weapon_fx_wire::isProjectile(entry) && !fillProjectile(player, matrix, entry)) {
        noteUnlisted();
        return;
    }
    add(entry);
}

// What the attach pointer of sEffect::create is, relative to the owned character: nothing, it, or its weapon unit.
bool attachKindOf(uintptr_t attach, uintptr_t player, uintptr_t unit, uint8_t& out) {
    if (attach == 0) {
        out = weapon_fx_wire::kAttachNone;
    } else if (attach == player) {
        out = weapon_fx_wire::kAttachPlayer;
    } else if (unit && attach == unit) {
        out = weapon_fx_wire::kAttachWeaponUnit;
    } else {
        return false;
    }
    return true;
}

void recordEffect(uintptr_t caller, uintptr_t owner, uint32_t index, uintptr_t attach, int32_t joint, const float* pos,
                  const float* rot, uint32_t extra) {
    const uintptr_t player = character_owner::localPlayer();
    const uintptr_t unit = player ? weapon_fx_game::weaponUnitOf(player) : 0;
    if (!player || (owner != player && !(unit && owner == unit))) return;
    const uint8_t site = admit(Primitive::Effect, caller);
    if (site == weapon_fx_sites::kNoSite) return;
    Entry entry;
    entry.op = Op::Effect;
    entry.site = site;
    entry.owner = owner == player ? weapon_fx_wire::kOwnerPlayer : weapon_fx_wire::kOwnerWeaponUnit;
    const bool fits = std::in_range<uint16_t>(index) && std::in_range<int8_t>(joint) && extra == kNoExtra;
    if (!fits || !attachKindOf(attach, player, unit, entry.attach) || !readVector(pos, entry.pos) || !readVector(rot, entry.rot)) {
        noteUnlisted();
        return;
    }
    entry.number = static_cast<uint16_t>(index);
    entry.joint = static_cast<int8_t>(joint);
    add(entry);
}

void recordThrowable(Primitive primitive, uintptr_t caller, uintptr_t player) {
    if (!owned(player)) return;
    const uint8_t site = admit(primitive, caller);
    if (site == weapon_fx_sites::kNoSite) return;
    Entry entry;
    entry.op = weapon_fx_wire::opOf(primitive);
    entry.site = site;
    add(entry);
}

void recordRipple(uintptr_t caller, uintptr_t chara, uint32_t sub, const float* pos) {
    if (!owned(chara)) return;
    const uint8_t site = admit(Primitive::Ripple, caller);
    if (site == weapon_fx_sites::kNoSite) return;
    Entry entry;
    entry.op = Op::Ripple;
    entry.site = site;
    if (!std::in_range<uint8_t>(sub) || !readVector(pos, entry.pos)) {
        noteUnlisted();
        return;
    }
    entry.kind = static_cast<uint8_t>(sub);
    add(entry);
}

int __fastcall playSeDetour(void* weapon, void* edx, uintptr_t chara, uint32_t soundId, float volume) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    const int result = g_originalPlaySe(weapon, edx, chara, soundId, volume);
    if (scope.outermost()) recordSound(Primitive::PlaySe, caller, chara, soundId, volume);
    return result;
}

int __stdcall genericSeDetour(uintptr_t chara, uint32_t soundId, float volume) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    const int result = g_originalGenericSe(chara, soundId, volume);
    if (scope.outermost()) recordSound(Primitive::GenericSe, caller, chara, soundId, volume);
    return result;
}

void __fastcall shootDetour(void* weapon, void* edx, uintptr_t player, const float* matrix, uint32_t kind) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    g_originalShoot(weapon, edx, player, matrix, kind);
    if (scope.outermost()) recordShoot(caller, player, matrix, kind);
}

void __fastcall muzzleFlashDetour(void* player, void* edx, uint32_t index, float scale) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    g_originalMuzzleFlash(player, edx, index, scale);
    if (scope.outermost()) recordFlash(caller, reinterpret_cast<uintptr_t>(player), index, scale);
}

uint32_t* __fastcall effectCreateDetour(void* effects, void* edx, uint32_t* out, uintptr_t owner, uintptr_t set, uint32_t index,
                                        uintptr_t attach, int32_t joint, const float* pos, const float* rot,
                                        const uint32_t* flags, uint32_t extra) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    uint32_t* const result = g_originalEffectCreate(effects, edx, out, owner, set, index, attach, joint, pos, rot, flags, extra);
    if (scope.outermost()) recordEffect(caller, owner, index, attach, joint, pos, rot, extra);
    return result;
}

void __fastcall stowDetour(void* player, void* edx) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    g_originalStow(player, edx);
    if (scope.outermost()) recordThrowable(Primitive::Stow, caller, reinterpret_cast<uintptr_t>(player));
}

void __fastcall wickDetour(void* player, void* edx) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    g_originalWick(player, edx);
    if (scope.outermost()) recordThrowable(Primitive::Wick, caller, reinterpret_cast<uintptr_t>(player));
}

int __stdcall rippleDetour(uintptr_t chara, uint32_t sub, const float* pos) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Scope scope;
    const int result = g_originalRipple(chara, sub, pos);
    if (scope.outermost()) recordRipple(caller, chara, sub, pos);
    return result;
}

struct Hook {
    const char* name;
    uintptr_t address;
    void* detour;
    void** original;
};

const std::array kHooks = {
    Hook{"sWeapon::playSe", game::kWeaponPlaySeFunction, reinterpret_cast<void*>(&playSeDetour), reinterpret_cast<void**>(&g_originalPlaySe)},
    Hook{"weapon generic sound", game::kWeaponGenericSeFunction, reinterpret_cast<void*>(&genericSeDetour),
         reinterpret_cast<void**>(&g_originalGenericSe)},
    Hook{"sWeapon::shoot", game::kWeaponShootFunction, reinterpret_cast<void*>(&shootDetour), reinterpret_cast<void**>(&g_originalShoot)},
    Hook{"player::muzzleFlash", game::kMuzzleFlashFunction, reinterpret_cast<void*>(&muzzleFlashDetour),
         reinterpret_cast<void**>(&g_originalMuzzleFlash)},
    Hook{"sEffect::create", game::kEffectCreateFunction, reinterpret_cast<void*>(&effectCreateDetour),
         reinterpret_cast<void**>(&g_originalEffectCreate)},
    Hook{"player::stowThrowable", game::kStowThrowableFunction, reinterpret_cast<void*>(&stowDetour),
         reinterpret_cast<void**>(&g_originalStow)},
    Hook{"player::lightWick", game::kLightWickFunction, reinterpret_cast<void*>(&wickDetour), reinterpret_cast<void**>(&g_originalWick)},
    Hook{"water ripple sound", game::kWaterRippleFunction, reinterpret_cast<void*>(&rippleDetour),
         reinterpret_cast<void**>(&g_originalRipple)},
};

// Once a second while anything was sent or skipped.
void logWindow() {
    const uint32_t now = pose_wire::clockMs();
    if (g_logStarted && stream_clock::after(g_nextLogMs, now)) return;
    g_logStarted = true;
    g_nextLogMs = now + kLogIntervalMs;
    const uint32_t unlisted = g_windowUnlisted.exchange(0);
    if (g_windowSent == 0 && unlisted == 0) return;
    logger::write("weapon_fx: sent %u entries (%u unlisted calls skipped)", g_windowSent, unlisted);
    g_windowSent = 0;
}

void afterMove(uintptr_t player) {
    if (!owned(player)) return;
    flush();
    logWindow();
}

}  // namespace

namespace weapon_fx_capture {

bool enable() {
    for (const Hook& hook : kHooks) {
        if (!hooks::install(hook.name, hook.address, hook.detour, hook.original)) return false;
    }
    g_installed = true;
    game_tick::addPostMove("weapon_fx_capture", afterMove);
    return true;
}

void uninstall() {
    if (!g_installed) return;
    for (const Hook& hook : kHooks) hooks::remove(hook.address);
    g_installed = false;
}

}  // namespace weapon_fx_capture
