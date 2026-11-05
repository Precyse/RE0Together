#include "body_card.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "calls.h"
#include "fnv1a32.h"
#include "local_body.h"
#include "log.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kRecheckMs = 10000;
constexpr size_t kMaxEntryBytes = UINT16_MAX;

// What this machine last sent for one of its bodies.
struct Own {
    uint32_t sentHash = 0;
    std::vector<uint8_t> servedPeers;  // the matching peers present at the last successful send
    std::vector<uint8_t> lastCard;
};

uint64_t g_lastCheckMs = 0;
Own g_own[wire_dd2::kBodyKinds];
std::map<std::pair<uint8_t, uint8_t>, body_card::Card> g_peers;  // (slot, kind)

// The own card of a body now, or empty when its look cannot be read.
std::vector<uint8_t> ownCard(uint8_t kind, uint32_t& hash) {
    const auto contexts = look::of(local_body::character(kind));
    look::Json json;
    if (!contexts || !look::toJson(*contexts, json)) return {};
    std::vector<std::pair<uint16_t, std::string>> entries;
    for (size_t i = 0; i < look::kCount; ++i) {
        if (json[i].empty()) continue;
        if (json[i].size() > kMaxEntryBytes) {
            logger::write("body_card: %s JSON is %zu bytes, over the entry limit", look::kSlots[i].type, json[i].size());
            return {};
        }
        entries.emplace_back(static_cast<uint16_t>(i), std::move(json[i]));
    }
    const std::vector<uint8_t> bytes = wire_dd2::encodeCardEntries(entries);
    hash = fnv1a32::hash(bytes);
    return wire_dd2::encodeBodyCard(kind, static_cast<uint16_t>(entries.size()), hash, bytes);
}

void sendOwn(uint8_t kind, const std::vector<uint8_t>& present, bool recheck) {
    Own& own = g_own[kind];
    // A peer counts as new once its plugin matched (after its hello), until a send to it succeeded.
    const bool joined = std::any_of(present.begin(), present.end(), [&](uint8_t slot) {
        return std::find(own.servedPeers.begin(), own.servedPeers.end(), slot) == own.servedPeers.end();
    });
    if (!joined && !recheck) return;
    uint32_t hash = 0;
    std::vector<uint8_t> card = ownCard(kind, hash);
    if (card.empty()) {
        if (!joined || own.lastCard.empty()) return;
        card = own.lastCard;  // a joiner gets the last good card
        hash = own.sentHash;
    }
    if (hash == own.sentHash && !joined) return;
    if (card.size() > wire_dd2::kMaxCardBytes + sizeof(wire_dd2::BodyCardHeader)) {
        logger::write("body_card: card of %zu bytes is over the limit", card.size());
        return;
    }
    if (session::send(wire_dd2::kBodyCard, true, card)) {
        if (hash != own.sentHash) logger::write("body_card: kind %u sent (%zu bytes, hash 0x%08X)", kind, card.size(), hash);
        own.sentHash = hash;
        own.lastCard = std::move(card);
        own.servedPeers = present;
    }
}

}  // namespace

namespace body_card {

void tick(uint64_t nowMs) {
    const std::vector<uint8_t> present = session::matchingPeers();
    std::erase_if(g_peers, [&](const auto& entry) {
        return std::find(present.begin(), present.end(), entry.first.first) == present.end();
    });
    if (present.empty() || !calls::enabled(calls::Feature::Puppet) || world_gate::place() != world_gate_rule::Place::InGame) return;
    const bool recheck = nowMs - g_lastCheckMs >= kRecheckMs;
    if (recheck) g_lastCheckMs = nowMs;
    for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) sendOwn(kind, present, recheck);
}

bool onFrame(uint8_t slot, std::span<const uint8_t> payload) {
    const auto card = wire_dd2::decodeBodyCard(payload);
    if (!card) return false;
    Card kept;
    kept.hash = card->header.hash;
    for (const wire_dd2::CardEntry& entry : card->entries) {
        if (entry.tag >= look::kCount) return false;
        kept.json[entry.tag].assign(entry.bytes.begin(), entry.bytes.end());
    }
    Card& current = g_peers[{slot, card->header.kind}];
    if (current.hash != kept.hash) {
        logger::write("body_card: slot %u kind %u card 0x%08X (%zu bytes)", slot, card->header.kind, kept.hash, payload.size());
    }
    current = std::move(kept);
    return true;
}

const Card* peerCard(uint8_t slot, uint8_t kind) {
    const auto it = g_peers.find({slot, kind});
    return it == g_peers.end() ? nullptr : &it->second;
}

}  // namespace body_card
