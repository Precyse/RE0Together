#include "peer_puppet.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "body_card.h"
#include "calls.h"
#include "log.h"
#include "look.h"
#include "peer_bodies.h"
#include "puppet_builder.h"
#include "puppet_driver.h"
#include "puppet_registry.h"
#include "refw.h"
#include "session.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kRetryMs = 5000;  // after a refused build, before the next try for that body
constexpr const char* kPawnManager = "app.PawnManager";
constexpr uintptr_t kHireSkipEquip = 1;  // hirePawn's Boolean: skip setEquipItem (the body already wears its card)

using world_gate_rule::Place;
using BodyKey = std::pair<uint8_t, uint8_t>;  // (slot, kind)

struct Shown {
    std::optional<puppet_builder::Puppet> puppet;  // the body's id and product, kept for the session
    uint32_t productHash = 0;                      // the card the product's writer was made from
    bool generated = false;
    bool seated = false;                           // a main pawn hired into our party
    bool seatTried = false;
    puppet_driver::State driver;
    uint64_t lastTryMs = 0;
    bool tried = false;
};

bool g_enabled = false;
std::map<BodyKey, Shown> g_shown;

// A puppet is built only while both sides are in plain gameplay, and kept through menus, cutscenes and downs; a load on
// either side (title, loading, ferrystone travel, rift) removes it.
bool builds(Place place) { return place == Place::InGame; }
bool keeps(Place place) { return place == Place::InGame || place == Place::Menu || place == Place::Cutscene || place == Place::Down; }

std::optional<Place> peerPlace(uint8_t slot) {
    const auto place = session::peerPlace(slot);
    if (!place) return std::nullopt;
    return static_cast<Place>(place->place);
}

// A remote main pawn takes a pawn seat in our party (no server call; refused when the party is full).
void seat(const BodyKey& key, Shown& shown) {
    if (key.second != wire_dd2::kKindMainPawn || shown.seated || shown.seatTried) return;
    const auto body = puppet_registry::body(shown.puppet->characterId);
    void* pawns = refw::managedSingleton(kPawnManager);
    if (!body || !pawns) return;
    shown.seatTried = true;
    const refw::InvokeResult* hired = refw::invoke(calls::method(calls::kHirePawn), pawns,
                                                   {body->character, reinterpret_cast<void*>(kHireSkipEquip)});
    shown.seated = hired && hired->byte != 0;
    logger::write("peer_puppet: slot %u main pawn %s", key.first, shown.seated ? "seated in the party" : "not seated (party full or refused)");
}

// By CharacterID, so it works after the game removed the body too.
void unseat(Shown& shown) {
    if (!shown.seated) return;
    void* pawns = refw::managedSingleton(kPawnManager);
    const auto id = reinterpret_cast<void*>(uintptr_t{shown.puppet->characterId});
    if (pawns) refw::invoke(calls::method(calls::kDismissPawn), pawns, {id});
    shown.seated = false;
}

void remove(const BodyKey& key, Shown& shown, const char* why) {
    if (!shown.generated) return;
    logger::write("peer_puppet: slot %u kind %u removed (%s)", key.first, key.second, why);
    unseat(shown);
    puppet_registry::forget(shown.puppet->characterId);  // first: the cleanup vetoes must not refuse our own removal
    puppet_builder::despawn(*shown.puppet);
    shown.generated = false;
    shown.seatTried = false;
    shown.driver = {};
}

void* writerFrom(const body_card::Card& card) {
    look::Objects contexts{};
    if (!look::fromJson(card.json, contexts)) return nullptr;
    void* writer = look::writer(contexts);
    look::release(contexts);
    return writer;
}

void build(const BodyKey& key, Shown& shown, const body_card::Card& card, const wire_dd2::BodyState& body, uint64_t nowMs) {
    shown.tried = true;
    shown.lastTryMs = nowMs;
    if (!shown.puppet || shown.productHash != card.hash) {
        void* writer = writerFrom(card);
        if (!writer) return;
        if (!shown.puppet) {
            shown.puppet = puppet_builder::create(writer);
        } else {
            puppet_builder::rewrite(*shown.puppet, writer);
        }
        refw::release(writer);
        if (!shown.puppet) return;
        shown.productHash = card.hash;
    }
    puppet_registry::expect(shown.puppet->characterId);
    shown.generated = puppet_builder::generate(*shown.puppet, body.position, body.yaw);
    if (!shown.generated) {
        puppet_registry::forget(shown.puppet->characterId);
        return;
    }
    logger::write("peer_puppet: slot %u kind %u built from card 0x%08X as 0x%08X", key.first, key.second, card.hash,
                  shown.puppet->characterId);
}

void tickBody(const BodyKey& key, Shown& shown, uint64_t nowMs, Place own) {
    const auto [slot, kind] = key;
    const body_card::Card* card = body_card::peerCard(slot, kind);
    const auto body = peer_bodies::latest(slot, kind);
    const std::vector<wire_dd2::ActionEvent> edges = peer_bodies::takeActionEdges(slot, kind);  // drained every step
    const auto place = peerPlace(slot);
    if (shown.generated) {
        if (!keeps(own) || !place || !keeps(*place)) {
            remove(key, shown, "a load on either side");
        } else if (puppet_registry::destroyed(shown.puppet->characterId)) {
            logger::write("peer_puppet: slot %u kind %u body removed by the game, rebuilding", slot, kind);
            puppet_registry::forget(shown.puppet->characterId);
            puppet_builder::bodyGone(*shown.puppet);
            unseat(shown);
            shown.generated = shown.seatTried = false;
            shown.driver = {};
        } else if (card && card->hash != shown.productHash) {
            remove(key, shown, "new card");
        } else {
            const auto bound = puppet_registry::body(shown.puppet->characterId);
            if (bound && body && *place == Place::InGame) {
                seat(key, shown);
                puppet_driver::drive(*bound, *body, edges, shown.driver);
            }
            return;
        }
    }
    const bool wanted = builds(own) && place && builds(*place) && card && body;
    if (!wanted) {
        shown.tried = false;
        return;
    }
    if (shown.tried && nowMs - shown.lastTryMs < kRetryMs) return;
    build(key, shown, *card, *body, nowMs);
}

}  // namespace

namespace peer_puppet {

void setEnabled(bool on) { g_enabled = on; }

std::optional<puppet_registry::Body> body(uint8_t slot, uint8_t kind) {
    const auto it = g_shown.find({slot, kind});
    if (it == g_shown.end() || !it->second.generated) return std::nullopt;
    return puppet_registry::body(it->second.puppet->characterId);
}

void tick(uint64_t nowMs) {
    if (!g_enabled || !calls::enabled(calls::Feature::Puppet)) return;
    const std::vector<uint8_t> present = session::matchingPeers();
    for (auto it = g_shown.begin(); it != g_shown.end();) {
        if (std::find(present.begin(), present.end(), it->first.first) == present.end()) {
            remove(it->first, it->second, "the peer left");
            if (it->second.puppet) puppet_builder::release(*it->second.puppet);
            it = g_shown.erase(it);
            continue;
        }
        ++it;
    }
    const Place own = world_gate::place();
    for (uint8_t slot : present) {
        for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) {
            const BodyKey key{slot, kind};
            tickBody(key, g_shown[key], nowMs, own);
        }
    }
}

}  // namespace peer_puppet
