// The CD adapter's pure parts without the game: the world gate, actor key classes, packet headers, the Troc table and
// the CD wire decoders.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <array>
#include <map>

#include "../src/actor_key.h"
#include "../src/equip_rule.h"
#include "../src/game_state_rule.h"
#include "../src/field_time_rule.h"
#include "../src/hit_rule.h"
#include "../src/kill_rule.h"
#include "../src/packet.h"
#include "../src/pin_rule.h"
#include "../src/presence_rule.h"
#include "../src/save_hold_rule.h"
#include "../src/stage_packets.h"
#include "../src/story_rule.h"
#include "../src/template_rule.h"
#include "../src/stream_clock_rule.h"
#include "../src/troc_table.h"
#include "../src/wire_cd.h"
#include "../src/world_gate_rule.h"
#include "../src/zone_rule.h"
#include "../../common/peer_roster.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

using world_gate_rule::Cause;
using world_gate_rule::Gate;
using world_gate_rule::State;

void gateTests() {
    Gate gate;
    check(gate.state() == State::Title && gate.loadSerial() == 0, "gate starts at Title with no load");
    check(gate.loadStarted(100).cause == Cause::LoadStart && gate.loadSerial() == 1, "a load start bumps the serial");
    check(!gate.loadStarted(200).changed() && gate.loadSerial() == 1, "a second start inside a load changes nothing");
    check(gate.loadCompleted(300).to == State::AwaitLocalReady, "completion waits for the local player");
    check(!gate.tick(300 + world_gate_rule::kSettleMs).changed(), "no own move: still waiting after the settle time");
    gate.ownPlayerMoved();
    check(!gate.tick(301).changed(), "own move but too soon: still waiting");
    check(gate.tick(300 + world_gate_rule::kSettleMs).to == State::Ready, "own move and settled: Ready");
    check(gate.loadStarted(5000).to == State::Loading && gate.loadSerial() == 2, "fast travel from Ready is a new load");
    check(gate.tick(5000 + world_gate_rule::kLoadingCapMs).cause == Cause::LoadTimeout, "a load with no completion times out");
    check(gate.loggedOut(1).cause == Cause::None, "logout at Title changes nothing");

    Gate late;
    check(late.loadCompleted(10).cause == Cause::LoadCompleteUnseenStart && late.loadSerial() == 1,
          "a completion with no start seen still counts as a load");
    late.ownPlayerMoved();
    late.tick(10 + world_gate_rule::kSettleMs);
    check(late.loggedOut(9999).to == State::Title, "logout returns to Title");

    Gate stale;
    stale.loadStarted(0);
    stale.ownPlayerMoved();  // a move inside the load does not count
    stale.loadCompleted(10);
    check(!stale.tick(10 + world_gate_rule::kSettleMs).changed(), "moves before the completion do not make Ready");

    Gate wrap;
    for (int i = 0; i < 0x10000; ++i) {
        wrap.loadStarted(0);
        wrap.loggedOut(0);
    }
    check(wrap.loadSerial() != 0, "the load serial skips 0 when it wraps");
}

void keyTests() {
    check(actor_key::classOf(0x9010F001) == actor_key::Class::User, "0x9... is a user key");
    check(actor_key::classOf(0xA010F001) == actor_key::Class::Player, "0xA... is a player key");
    check(actor_key::classOf(0xB02F0011) == actor_key::Class::World, "0xB02... is still a world key");
    check(actor_key::classOf(0x2010F001) == actor_key::Class::None, "no valid bit: no class");
    check(actor_key::indexOf(0xB01F1234) == 0xF1234, "index is the low 20 bits");
}

void packetTests() {
    const uint8_t move[8] = {0xFC, 0x09, 0x00, 0x03, 0x00, 1, 2, 3};
    packet::Header header;
    check(packet::parse(move, sizeof(move), header) && header.opcode == 0x09FC && header.payloadSize == 3,
          "header fields read little-endian");
    check(!packet::parse(move, sizeof(move) - 1, header), "a length that disagrees with the size is refused");
    check(!packet::parse(move, 4, header), "shorter than a header is refused");
}

void trocTests() {
    check(troc_table::messageName(".?AVTrocTrMoveActorReq@pa@@") == "MoveActorReq", "RTTI name gives the message name");
    check(troc_table::messageName(".?AVTroc@pa@@").empty(), "the base class is not a message");
    check(troc_table::messageName(".?AVTrocTrMoveActorReq@@").empty(), "a class outside pa is not a message");
    troc_table::Table table;
    troc_table::Info move;
    move.name = "MoveActorReq";
    move.opcode = 2556;
    check(table.add(move), "first add stored");
    troc_table::Info dup = move;
    dup.name = "Other";
    check(!table.add(dup), "a taken opcode is refused");
    check(table.opcodeOf("MoveActorReq") == 2556 && table.find(2556)->name == "MoveActorReq", "lookups both ways");
    constexpr std::array<std::string_view, 2> required{"MoveActorReq", "MoveActorAck"};
    const auto missing = table.missing(required);
    check(missing.size() == 1 && missing[0] == "MoveActorAck", "missing lists the absent required names");
}

void wireTests() {
    wire_cd::World world{};
    world.loadSerial = 7;
    world.gateState = 3;
    world.position[1] = 12.5f;
    const auto bytes = wire_cd::encode(world);
    const auto back = wire_cd::decodeWorld(bytes);
    check(back && back->loadSerial == 7 && back->position[1] == 12.5f, "CD_WORLD round trip");
    world.gateState = wire_cd::kGateStates;
    check(!wire_cd::decodeWorld(wire_cd::encode(world)), "CD_WORLD with an unknown gate state is refused");
    world.gateState = 0;
    world.position[0] = std::numeric_limits<float>::quiet_NaN();
    check(!wire_cd::decodeWorld(wire_cd::encode(world)), "CD_WORLD with a NaN position is refused");
    check(!wire_cd::decodeWorld(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)), "short CD_WORLD refused");

    wire_cd::Hello hello{};
    hello.role = 2;
    check(!wire_cd::decodeHello(wire_cd::encode(hello)), "CD_HELLO with an unknown role is refused");

    const wire_cd::Counter counters[] = {{1, 10}, {2, 20}};
    const auto packed = wire_cd::encodeCounters(counters);
    const auto unpacked = wire_cd::decodeCounters(packed);
    check(unpacked && unpacked->size() == 2 && (*unpacked)[1].value == 20, "CD_COUNTERS round trip");
    auto bad = packed;
    bad.push_back(0);
    check(!wire_cd::decodeCounters(bad), "CD_COUNTERS with trailing bytes refused");
}

void rosterTests() {
    PeerRoster<int> roster;
    SessionSnapshot session;
    check(!roster.update(session), "no link: nothing to announce");
    session.linked = true;
    session.localSlot = 0;
    session.peers = {{0, 1, "me", 0, 1}};
    check(!roster.update(session) && roster.empty(), "linked alone: no peers, nothing to announce");
    session.peers.push_back({1, 2, "guest", 0, 2});
    check(roster.update(session) && roster.find(1), "a peer joining is announced");
    *roster.find(1) = 7;
    check(!roster.update(session) && *roster.find(1) == 7, "the same peer keeps its state");
    session.peers[1].joinSerial = 3;
    check(roster.update(session) && *roster.find(1) == 0, "a rejoin starts a fresh state");
    session.peers.pop_back();
    check(!roster.update(session) && !roster.find(1), "a peer that left is gone");
}

void zoneTests() {
    zone_rule::Zone zone;
    zone_rule::Inputs in;
    in.bothReady = in.sameField = true;
    in.distanceM = 100.0f;
    check(!zone.update(in).changed, "near but not yet held: off");
    in.nowMs = zone_rule::kOnHoldMs;
    const auto on = zone.update(in);
    check(on.changed && on.on && zone.epoch() == 1, "near for 2 s: on, epoch 1");
    in.distanceM = 300.0f;
    in.nowMs += 1000;
    check(!zone.update(in).changed, "far for a moment: still on");
    in.guestFighting = true;
    in.nowMs += zone_rule::kOffHoldMs;
    check(!zone.update(in).changed && zone.on(), "far but the guest fights: waits");
    in.nowMs += zone_rule::kFightWaitMs;
    const auto off = zone.update(in);
    check(off.changed && !off.on && off.reason == zone_rule::OffReason::Distance, "fight wait over: off for distance");

    zone_rule::Zone dead;
    zone_rule::Inputs d;
    d.bothReady = d.sameField = true;
    d.distanceM = 10.0f;
    dead.update(d);
    d.nowMs = zone_rule::kOnHoldMs;
    dead.update(d);
    d.hostDead = true;
    d.bothReady = false;
    d.distanceM = 900.0f;  // the respawn jump
    d.nowMs += zone_rule::kOffHoldMs * 2;
    dead.update(d);
    d.nowMs += zone_rule::kOffHoldMs * 2;
    check(!dead.update(d).changed && dead.on(), "the host's death and respawn keep the zone on");
    d.nowMs += zone_rule::kHostDeathGraceMs;
    check(dead.update(d).changed && !dead.on(), "after the grace a lost host turns it off");

    zone_rule::Zone field;
    zone_rule::Inputs f;
    f.bothReady = f.sameField = true;
    field.update(f);
    f.nowMs = zone_rule::kOnHoldMs;
    field.update(f);
    f.sameField = false;
    f.guestFighting = true;
    check(field.update(f).reason == zone_rule::OffReason::FieldChanged, "a field change is off at once, even in a fight");

    zone_rule::Candidate c{zone_rule::kRecordTypeNpc, 0, true};
    check(zone_rule::shared(c), "a plain hostile NPC is shared");
    c.variant = zone_rule::VariantKind::Boss;
    check(!zone_rule::shared(c), "a boss is not shared");
    c.variant = zone_rule::VariantKind::Normal;
    c.parentType = zone_rule::kParentMount;
    check(!zone_rule::shared(c), "a mounted actor is not shared");
    c.parentType = 0;
    c.owner = 5;
    check(!zone_rule::shared(c), "an owned actor is not shared");
}

void hitTests() {
    using hit_rule::Refusal;
    hit_rule::Gate gate;
    hit_rule::Intent in{1, 3, 7, 0, 5.0f, 2000};
    const hit_rule::Enemy enemy{true, false, true, 100000};
    const hit_rule::Context ctx{3, 7, false};
    check(gate.check(in, enemy, ctx, 0) == Refusal::None, "a fresh melee blow in range is accepted");
    check(gate.check(in, enemy, ctx, 1) == Refusal::Duplicate, "the same hit id twice is refused");
    in.hitId = 2;
    in.zoneEpoch = 2;
    check(gate.check(in, enemy, ctx, 2) == Refusal::StaleZone, "an old zone epoch is refused");
    in.hitId = 6;
    in.zoneEpoch = 3;
    in.distanceM = 40.0f;
    check(gate.check(in, enemy, ctx, 3) == Refusal::TooFar, "melee at 40 m is too far");
    in.hitId = 3;
    in.accessor = 1;
    check(gate.check(in, enemy, ctx, 4) == Refusal::None, "a projectile at 40 m is accepted");
    in.hitId = 4;
    in.deltaX1000 = 200000;
    check(gate.check(in, enemy, ctx, 5) == Refusal::BadDelta, "more than the max HP is refused");
    in.deltaX1000 = 1000;
    const hit_rule::Enemy lever{true, false, false, 0};
    in.hitId = 5;
    check(gate.check(in, lever, ctx, 6) == Refusal::NotDamageable, "an actor without HP is refused");
    uint32_t accepted = 2;
    for (uint32_t id = 100; id < 120; ++id) {
        in.hitId = id;
        if (gate.check(in, enemy, ctx, 10) == Refusal::None) ++accepted;
    }
    check(accepted == hit_rule::kMaxIntentsPerSecond, "at most 15 accepted per second");
    in.hitId = 200;
    check(gate.check(in, enemy, ctx, 1010) == Refusal::None, "the window moves on");
}

void killTests() {
    kill_rule::Ledger ledger;
    const auto first = ledger.commit(0xB0001234, 4, 1, kill_rule::Killer::Guest, true);
    check(first && first->killSerial == 1 && first->mask == (kill_rule::kCreditHost | kill_rule::kCreditGuest),
          "a guest kill with share on credits both");
    check(!ledger.commit(0xB0001234, 4, 0, kill_rule::Killer::Host, true), "a death commits once");
    check(ledger.commit(0xB0001234, 5, 0, kill_rule::Killer::Host, false).has_value(), "the same key at a new serial is another enemy");
    const auto off = kill_rule::credit(kill_rule::Killer::Guest, false);
    check(off.host == kill_rule::HostAction::FinishNoCredit && off.mask == kill_rule::kCreditGuest,
          "share off: the host finishes without credit, the guest keeps its own");
    const auto host = kill_rule::credit(kill_rule::Killer::Host, true);
    check(host.guest == kill_rule::GuestAction::FinishGuestCredit, "a host kill with share on finishes the guest's shadow with credit");
    check(kill_rule::credit(kill_rule::Killer::Other, true).mask == 0, "an NPC kill credits nobody");
}

void clockTests() {
    namespace sc = stream_clock_rule;
    check(sc::restamp(10000, 10000, std::nullopt) == 10000, "a fresh stamp is kept");
    check(sc::restamp(8000, 10000, std::nullopt) == 10000 - sc::kRestampPastMs, "an old stamp is clamped");
    check(sc::restamp(20000, 10000, std::nullopt) == 10000 + sc::kRestampFutureMs, "a future stamp is clamped");
    check(sc::restamp(10000, 10000, 10200) == 10201, "stamps per actor strictly increase");

    sc::PauseDetector pause;
    pause.sample(0, 0);
    check(!pause.sample(600, 600) && !pause.paused(), "a running clock is not paused");
    check(pause.sample(650, 1200) && pause.paused(), "a clock that barely moved for 600 ms is paused");
    check(!pause.sample(660, 1300), "still paused before the resume window");
    check(pause.sample(760, 1400) && !pause.paused(), "moving at half speed for 150 ms is running again");

    sc::MoveThinner thinner;
    check(thinner.keep(0, 1, 0), "the first move is sent");
    check(!thinner.keep(10, 1, 10), "a move 10 ms later is thinned");
    check(thinner.keep(25, 1, 25), "a move 25 ms after the last sent one is sent");
    check(thinner.keep(30, 2, 30), "a parent change is always sent");
    check(thinner.keep(35, 2, 5), "an out-of-order stamp is always sent");
}

std::vector<uint8_t> numbered(size_t size) {
    std::vector<uint8_t> out(size);
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<uint8_t>(i * 7 + 3);
    return out;
}

void recipeTests() {
    namespace pr = presence_rule;
    auto move = numbered(pr::kMoveReqSize);
    const auto compact = pr::compactMove(move);
    check(compact && compact->size() == pr::kMoveCompactSize, "a Move Req compacts to body + tail");
    check(std::equal(move.end() - pr::kMoveTail, move.end(), compact->end() - pr::kMoveTail), "the tail is kept");
    const auto ack = pr::moveAck(*compact, 0xA010F011, 123456);
    check(ack && ack->size() == pr::kMoveAckSize, "the Move Ack is 235 bytes");
    check(pr::read<uint32_t>(*ack, pr::kKey) == 0xA010F011 && pr::read<uint64_t>(*ack, pr::kMoveTimestamp) == 123456 &&
              pr::read<uint64_t>(*ack, pr::kMoveParent) == 0 && (*ack)[pr::kMoveBody] == 0,
          "the Move Ack has the copy key, our stamp, no parent and the recipient byte");
    check(!pr::compactMove(numbered(pr::kMoveReqSize - 1)), "a Move of another size is refused");

    auto action = numbered(pr::kActionSize);
    const auto actionAck = pr::actionAck(action, 7, 99);
    check(actionAck && actionAck->size() == pr::kActionSize && (*actionAck)[pr::kActionHead] == action.back(),
          "the action's force flag moves to +0x8E");
    check((*actionAck)[pr::kActionHead + 1] == action[pr::kActionHead] && pr::read<uint64_t>(*actionAck, pr::kActionAckTimestamp) == 99,
          "block B shifts up one byte and carries our stamp");
    check(!pr::actionIsSelfContained(action), "an action naming a parent or target is held back");
    std::fill(action.begin() + pr::kActionReqParent, action.begin() + pr::kActionReqParent + 4, uint8_t{0});
    std::fill(action.begin() + pr::kActionReqTarget, action.begin() + pr::kActionReqTarget + 4, uint8_t{0});
    check(pr::actionIsSelfContained(action), "an action naming nobody is relayed");

    const auto rotate = numbered(pr::kRotateReqSize);
    const auto rotateAck = pr::rotateAck(rotate, 9, 5);
    check(rotateAck && rotateAck->size() == pr::kRotateAckSize && (*rotateAck)[pr::kRotateInsertAt] == 0 &&
              (*rotateAck)[pr::kRotateInsertAt + 1] == rotate[pr::kRotateInsertAt],
          "the Rotate Ack has one byte inserted at +0xA4");

    const auto remove = pr::removeAck(0xB010F001);
    check(remove.size() == 10 && remove[0] == 4 && remove[5] == pr::kRemoveFade && pr::read<uint32_t>(remove, 6) == 0xB010F001,
          "RemoveActorAck: marker, reason, key at +6");
    const auto packet = pr::packetOf(2516, remove);
    check(packet.size() == 15 && pr::read<uint16_t>(packet, 0) == 2516 && pr::read<uint16_t>(packet, 3) == 10, "a packet has the header");
}

void templateTests() {
    namespace tr = template_rule;
    constexpr uint32_t kUser = 0x90100000, kPlayer = 0xA0100001, kBody = 0xB0100050;
    std::vector<uint8_t> user(600, 0x11);
    auto put32 = [](std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); };
    put32(user, tr::kUserKey, kUser);
    put32(user, tr::kOwnedKey, kPlayer);
    put32(user, 0x100, kPlayer);
    const uint64_t number = 0x0123456789ABCDEFull;
    for (size_t at : tr::kCharacterNumber) std::memcpy(user.data() + at, &number, 8);
    std::vector<uint8_t> body(300, 0x22);
    put32(body, tr::kBodyKey, kBody);
    put32(body, 0x40, kPlayer);
    body.insert(body.end(), user.begin(), user.end());

    const tr::Keys keys = tr::senderKeys(user, body);
    check(keys.user == kUser && keys.player == kPlayer && keys.body == kBody, "the template names its keys");
    const auto cut = tr::truncateBody(body, user);
    check(cut && cut->size() == 300, "the body is cut where its copy of the user packet starts");

    tr::Placement at;
    at.position[0] = 10.0f;
    std::string why;
    const auto copy = tr::makeCopy(user, body, 1, at, {0x90100000 + 5, 0xA0100009}, why);
    check(copy.has_value(), ("a good template makes a copy " + why).c_str());
    const tr::Keys target = tr::copyKeys(1);
    check(tr::countKey(copy->user, kPlayer) == 0 && tr::countKey(copy->user, target.player) == 2 &&
              tr::countKey(copy->body, target.body) == 1 && tr::countKey(copy->body, target.player) == 1,
          "every sender key becomes the slot's copy key");
    check(tr::read<uint64_t>(copy->user, tr::kCharacterNumber[1]) == tr::copyCharacterNumber(1), "the character number is the copy's");
    check(tr::read<float>(copy->user, copy->user.size() - tr::kPositionFromEnd) == 10.0f, "the position is written");
    check(!tr::makeCopy(user, body, 1, at, {target.player}, why) && why == "names a key of this world",
          "a copy naming a key of this world is refused");
    auto badNumber = user;
    badNumber[tr::kCharacterNumber[1]] ^= 1;
    check(!tr::makeCopy(badNumber, body, 1, at, {}, why) && why == "character number", "mismatched character numbers are refused");
    check(!tr::makeCopy(user, std::vector<uint8_t>(body.begin(), body.begin() + 300), 1, at, {}, why),
          "a body without the user copy is refused");
}

void equipTests() {
    namespace er = equip_rule;
    er::Loadout loadout;
    const er::Item swap[] = {{0xF42E4, 4}, {0, 4}};
    check(loadout.apply(10, swap), "a swap is a plausible change");
    const er::Item put[] = {{0x1B23D, 3}};
    loadout.apply(11, put);
    const er::Item badSlot[] = {{1, er::kMaxSlot + 1}};
    check(!loadout.apply(12, badSlot) && loadout.serial() == 11, "a slot past the limit is refused");
    const auto slots = loadout.slots();
    check(slots.size() == 2 && slots[0].slot == 3 && !slots[0].hadAtLoad && slots[1].slot == 4 && slots[1].hadAtLoad &&
              slots[1].item == 0xF42E4,
          "the loadout keeps the item and whether the template filled the slot");

    std::map<uint16_t, er::CopySlot> copy;
    const auto first = er::plan(slots, copy);
    check(first.unequips == std::vector<uint16_t>{4} && first.puts.size() == 2,
          "a fresh copy: the template-filled slot is emptied first, then every put in one message");
    check(er::plan(slots, copy).empty(), "the same loadout again changes nothing");
    std::vector<er::SlotState> off = slots;
    off[0].item = 0;
    const auto removal = er::plan(off, copy);
    check(removal.unequips == std::vector<uint16_t>{3} && removal.puts.empty(), "taking an item off is one unequip");

    check(er::nextSerial(std::nullopt, 7) == 7 && er::nextSerial(7u, 7) == 8 && er::nextSerial(8u, 20) == 20,
          "serials rise strictly and never fall below the sender's");
    const auto equip = er::otherEquip(0xA010F001, 9, first.puts);
    check(equip.size() == er::kEquipHeader + 2 * er::kEquipEntry && equip[0] == er::kFragmentMarker, "OtherEquipItemAck is 9 + 6n bytes");
    check(er::otherUnequip(0xA010F001, 4, 9).size() == er::kUnequipSize, "OtherUnequipItemAck is 10 bytes");

    std::vector<uint8_t> ack(er::kUnequipItemAckSize, 0);
    const uint32_t key = 0xA0100001, serial = 33;
    const uint16_t slot = 5;
    std::memcpy(ack.data(), &key, 4);
    std::memcpy(ack.data() + 4, &slot, 2);
    std::memcpy(ack.data() + 8, &serial, 4);
    const auto unequip = er::parseUnequipItemAck(ack);
    check(unequip && unequip->key == key && unequip->item.slot == 5 && unequip->item.item == 0 && unequip->serial == 33,
          "UnequipItemAck becomes an empty slot");

    std::vector<wire_cd::EquipEntry> entries;
    for (const auto& s2 : slots) entries.push_back({s2.slot, s2.item, static_cast<uint8_t>(s2.hadAtLoad)});
    const auto bytes = wire_cd::encodeEquip(3, 11, entries);
    const auto back = wire_cd::decodeEquip(bytes);
    check(back && back->header.generation == 3 && back->header.serial == 11 && back->entries.size() == 2 &&
              back->entries[1].hadAtLoad == 1,
          "CHAR_EQUIP round trip");
    auto badFlag = bytes;
    badFlag.back() = 2;
    check(!wire_cd::decodeEquip(badFlag), "CHAR_EQUIP with a bad flag is refused");
    check(!wire_cd::decodeEquip(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)), "short CHAR_EQUIP refused");
}

void pinTests() {
    namespace pr = pin_rule;
    pr::Pin pin;
    const std::array<float, 3> here{100.0f, 5.0f, 200.0f};
    auto step = pr::decide(pin, here, 1000);
    check(step.create && !step.remove && pin.shown, "a far peer gets a pin");
    step = pr::decide(pin, std::array<float, 3>{101.0f, 5.0f, 200.0f}, 3000);
    check(!step.create && !step.remove, "a small move keeps the pin");
    step = pr::decide(pin, std::array<float, 3>{110.0f, 5.0f, 200.0f}, 3500);
    check(step.remove && step.create && pin.at[0] == 110.0f, "a 3 m move is Remove + Create");
    step = pr::decide(pin, std::array<float, 3>{120.0f, 5.0f, 200.0f}, 3800);
    check(!step.create, "moves are at most once a second");
    step = pr::decide(pin, std::array<float, 3>{110.0f, 5.0f, 200.0f}, 3500 + pr::kRefreshMs);
    check(step.remove && step.create, "a still pin is refreshed");
    step = pr::decide(pin, std::nullopt, 20000);
    check(step.remove && !step.create && !pin.shown, "the copy is here: the pin goes");
    check(!pr::decide(pin, std::nullopt, 21000).remove, "no pin, nothing to remove");
    check(pr::createPayload(0xA0100001, pr::pinId(1), here.data()).size() == pr::kCreateHeader + pr::kCreateEntry,
          "CreateOrChangePinMarkerAck is 6 + 22 bytes");
    check(pr::removePayload(0xA0100001, pr::pinId(1)).size() == pr::kRemoveSize, "RemovePinMarkerAck is 13 bytes");

    namespace tr = template_rule;
    check(tr::copySlotOfPlayer(tr::copyKeys(3).player) == 3, "a copy's player key gives its slot");
    check(tr::isCopyKey(tr::copyKeys(2).body + 1) && tr::isCopyKey(tr::copyKeys(0).user) && !tr::isCopyKey(0xA0100001) &&
              !tr::isCopyKey(0xB02FFFFF & ~0xFFFFFu),
          "copy keys sit above the server's index pool");
    check(!tr::copySlotOfPlayer(tr::copyKeys(3).body) && !tr::copySlotOfPlayer(0xA0100001) && !tr::copySlotOfPlayer(tr::kCopyPlayerBase + 1),
          "any other key is not a copy player");
}

void saveHoldTests() {
    namespace sh = save_hold_rule;
    sh::Hold hold;
    check(!hold.tick(0, 0).holdAutosaves, "nothing foreign: autosaves pass");
    check(hold.tick(sh::kShadowAlive, 1000).holdAutosaves, "a live shadow holds autosaves");
    const auto bound = hold.tick(sh::kShadowAlive, 1000 + sh::kBoundMs);
    check(!bound.holdAutosaves && bound.endShadows && bound.abandonReplays, "at 30 s the shadows end and the hold lets go");
    check(!hold.tick(sh::kShadowAlive, 40000).holdAutosaves, "a spent hold stays released while its reasons last");
    hold.tick(0, 41000);
    check(hold.tick(sh::kLoading, 42000).holdAutosaves, "after the reasons cleared once, a new hold starts");
    sh::Hold manual;
    check(!manual.onManualSave(0, 0), "a manual save with nothing foreign passes at once");
    check(manual.onManualSave(sh::kShadowAlive, 100), "a manual save waits while a shadow lives");
    const auto ending = manual.tick(sh::kShadowAlive, 200);
    check(ending.endShadows && !ending.passManualSave, "the shadows are ended for it");
    check(manual.tick(0, 300).passManualSave, "it passes once nothing foreign is left");
    manual.onManualSave(sh::kReplayWaiting, 1000);
    check(manual.tick(sh::kReplayWaiting, 1000 + sh::kManualMaxMs).passManualSave, "a manual save waits 5 s at most");
}

void stagePacketTests() {
    namespace sp = stage_packets;
    std::vector<uint8_t> req(sp::kReqSize, 0);
    const uint64_t instance = 0x1122334455667788ULL;
    const uint32_t branch = 3;
    const uint16_t flags = 0x0102;
    std::memcpy(req.data(), &instance, 8);
    std::memcpy(req.data() + 8, &branch, 4);
    std::memcpy(req.data() + 12, &flags, 2);
    const auto parsed = sp::parseReq(req);
    check(parsed && parsed->instance == instance && parsed->branch == 3 && parsed->flags == 0x0102, "StageBranchStateReq parse");
    check(sp::encodeReq(*parsed) == req, "a replayed Req is the same 14 bytes the game sends");
    std::vector<uint8_t> ack(sp::kAckMinSize + sp::kAckPairSize, 0);
    ack[0] = sp::kModeApply;
    std::memcpy(ack.data() + 5, &instance, 8);
    std::memcpy(ack.data() + 13, &branch, 4);
    const auto answer = sp::parseAck(ack);
    check(answer && answer->mode == sp::kModeApply && answer->instance == instance && answer->branch == 3, "StageBranchStateAck parse (one pair)");
    ack.push_back(0);
    check(!sp::parseAck(ack), "an Ack whose tail is not whole pairs is refused");
    std::vector<uint8_t> del(sp::kDeleteSize, 0);
    std::memcpy(del.data() + 4, &instance, 8);
    check(sp::parseDelete(del) == instance, "DeleteSequencerAck parse");
    const wire_cd::StageBranchMessage message{7, 1, 0xF4CB1, story_rule::kUnknownBranch, 2, 0};
    check(wire_cd::decodeStageBranch(wire_cd::encode(message)).has_value(), "STAGE_BRANCH round trip");
    const wire_cd::StageBranchMessage noLog{0, 1, 0xF4CB1, 0, 2, 0};
    check(!wire_cd::decodeStageBranch(wire_cd::encode(noLog)), "a STAGE_BRANCH without a log id is refused");
}

void storyTests() {
    namespace st = story_rule;
    constexpr uint32_t kUnknown = st::kUnknownBranch;
    st::HostLog host(0xABCD);
    const auto a = host.append(7, 0, 1, 0), b = host.append(7, 1, 2, 0), c = host.append(9, kUnknown, 4, 0);
    check(a && b && c && a->seq == 1 && c->seq == 3, "host entries are numbered densely");

    st::GuestLog guest;
    guest.begin(host.logId());
    for (const auto& e : host.entries()) guest.offer(e);
    check(!guest.offer(*a), "an entry is applied once (a resent log adds nothing)");
    st::Local missing;
    check(!guest.next(7, missing, 0), "a guest behind waits until its world has the stage");
    check(!guest.next(7, missing, st::kExpireMs * 2), "waiting for the stage never expires the entry");
    st::Local here{true, false, 0, 4};
    const auto first = guest.next(7, here, 1000);
    check(first && first->branch == 1, "the first branch goes when the instance sits where the host's started");
    check(!guest.next(7, here, 2000), "a tried entry waits 5 s before a retry");
    guest.applied(7, first->seq);
    here.sittingBranch = 0;
    check(!guest.next(7, here, 3000), "the next branch waits for the instance to sit on its starting branch");
    here.sittingBranch = 1;
    const auto second = guest.next(7, here, 3000);
    check(second && second->branch == 2, "sitting on the starting branch, the next one goes");
    check(!guest.next(9, st::Local{true, true, 0, 8}, 4000), "a busy stage is never touched");

    st::GuestLog ahead;
    ahead.begin(1);
    ahead.offer({1, 5, 0, 3, 0});
    ahead.offer({2, 5, 3, 6, 0});
    ahead.offer({3, 5, 6, 8, 0});
    const auto resumed = ahead.next(5, st::Local{true, false, 6, 10}, 0);
    check(resumed && resumed->branch == 8 && ahead.pending() == 1, "a guest ahead skips through the branch it sits on");
    check(!ahead.next(5, st::Local{true, false, 6, 8}, st::kRetryMs * 2), "a branch out of the table's range is not tried");

    st::GuestLog rollback;
    rollback.begin(2);
    rollback.offer({1, 5, 0, 1, 0});  // the host reloaded an older save and replays branch 1 from 0
    check(!rollback.next(5, st::Local{true, false, 4, 10}, 0), "a host replaying an older save never rolls the guest back");
    rollback.next(5, st::Local{true, false, 4, 10}, st::kExpireMs);
    check(rollback.pending() == 0, "an entry that cannot apply expires 120 s after the instance existed");

    st::GuestLog loop;
    loop.begin(3);
    loop.offer({1, 5, 2, 3, 0});
    loop.offer({2, 5, 3, 2, 0});
    loop.offer({3, 5, 2, 5, 0});
    const auto out = loop.next(5, st::Local{true, false, 2, 10}, 0);
    check(out && out->branch == 5, "a loop back to the sitting branch is skipped through its last visit");
    loop.begin(4);
    check(loop.offer({1, 5, 0, 3, 0}), "a new log id starts over");
}

void fieldTimeTests() {
    namespace ft = field_time_rule;
    ft::FieldTime noon;
    noon.day = 3;
    noon.hour = 12;
    noon.scale = 12;
    ft::FieldTime back;
    back.setRaw(noon.toRaw());
    check(back.day == 3 && back.hour == 12 && back.minute == 0 && back.weekday == 3, "raw round trip at noon");
    ft::FieldTime night;
    night.hour = 23;
    night.minute = 30;
    back.setRaw(night.toRaw());
    check(back.hour == 23 && back.minute == 30, "raw round trip at night (half speed)");
    ft::FieldTime early;
    early.hour = 1;
    check(early.toRaw() == 1800000, "the small hours count half");
    check(ft::nearestWithTimeOf(5 * ft::kRawDay + 100, 2 * ft::kRawDay + 50) == 5 * ft::kRawDay + 50,
          "the host's hour lands on our own day");
    check(ft::nearestWithTimeOf(5 * ft::kRawDay + 100, ft::kRawDay - 10) == 5 * ft::kRawDay - 10,
          "late in the host's day lands on our previous day when nearer");
    ft::CatchUp limiter;
    const uint64_t ours = 10 * ft::kRawDay;
    check(!limiter.decide(ours, ours + ft::kCatchUpRaw, 0), "within two game minutes nothing is posted");
    check(limiter.decide(ours, ours + 3600000, 0).has_value(), "an hour off is caught up");
    check(!limiter.decide(ours, ours + 3600000, 1000), "at most once per 15 s");
    int sent = 1;
    for (uint64_t t = ft::kMinGapMs; t < ft::kBurstWindowMs; t += ft::kMinGapMs) sent += limiter.decide(ours, ours + 3600000, t) ? 1 : 0;
    check(sent == static_cast<int>(ft::kBurstMax), "at most 8 per 5 minutes");
    check(ft::setReq(noon).size() == ft::kSetReqSize && ft::kSetReqSize == 36, "SetFieldTimeReq is 36 bytes");
    const wire_cd::FieldTimeMessage message{12345, 12, 0};
    check(wire_cd::decodeFieldTime(wire_cd::encode(message)).has_value(), "FIELD_TIME round trip");
    check(!wire_cd::decodeFieldTime(wire_cd::encode(wire_cd::FieldTimeMessage{1, 0, 0})), "FIELD_TIME with scale 0 is refused");
}

}  // namespace

void gameStateTests() {
    using namespace game_state_rule;
    using Gate = world_gate_rule::State;
    check(placeOf(Gate::Ready) == Place::World && placeOf(Gate::Loading) == Place::Loading &&
              placeOf(Gate::AwaitLocalReady) == Place::Loading && placeOf(Gate::Title) == Place::Title,
          "game state: the gate maps to world, loading (also while awaiting the local player) and title");
    check(copyOf({}).refusal == nullptr && !copyOf({}).off, "game state: nothing wrong is copy ok");
    check(copyOf({false, false, true, kCodeSilent}).off, "game state: presence=0 is copy off whatever else is wrong");
    check(std::string(copyOf({true, false, true, kCodeSilent}).refusal) == kCodeNames, "game state: missing names come first");
    check(std::string(copyOf({true, true, true, kCodeSilent}).refusal) == kCodeRecipe, "game state: a recipe mismatch comes before a peer problem");
    check(std::string(copyOf({true, true, false, kCodeTemplate}).refusal) == kCodeTemplate, "game state: a peer problem is reported");
    check(linkOf(false, false, false) == Link::Live && linkOf(true, true, false) == Link::Live, "game state: a peer before and after its hello is live");
    check(linkOf(true, false, true) == Link::Mismatch && linkOf(true, true, true) == Link::Stale, "game state: a different adapter is a mismatch before stale");
    Report report;
    check(format(report) == "place=title;char=off;save=off;slot=-1;copy=ok;peers=", "game state: the idle line");
    report.place = Place::World;
    report.copy = copyOf({true, true, false, kCodeSilent});
    report.peers = {{76561198000000001ull, Link::Mismatch}, {5, Link::Live}};
    check(format(report) == "place=world;char=off;save=off;slot=-1;copy=refused:silent;peers=76561198000000001:2,5:0", "game state: refused copy and peers");
    report.copy = copyOf({false});
    check(format(report).find("copy=off;") != std::string::npos, "game state: copy off");
}

int main() {
    gameStateTests();
    fieldTimeTests();
    saveHoldTests();
    storyTests();
    stagePacketTests();
    pinTests();
    equipTests();
    recipeTests();
    templateTests();
    clockTests();
    rosterTests();
    zoneTests();
    hitTests();
    killTests();
    gateTests();
    keyTests();
    packetTests();
    trocTests();
    wireTests();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
