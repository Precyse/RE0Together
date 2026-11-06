// The DD2 plugin's pure parts without the game: hook row verdicts, the world gate and the wire decoders.
#include <cstdio>

#include <cmath>

#include "../src/clock_rule.h"
#include "../src/enemy_rule.h"
#include "../src/game_state_rule.h"
#include "../src/hook_rule.h"
#include "../src/join_warp_rule.h"
#include "../src/load_slot_rule.h"
#include "../src/local_body.h"
#include "../src/profile_rule.h"
#include "../src/save_folders.h"
#include "../src/session_rule.h"
#include "../src/title_menu_rule.h"
#include "../src/wire_dd2.h"
#include "../src/world_gate_rule.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void hookRuleTests() {
    using hook_rule::Verdict;
    const names::MethodRow& row = names::kChangeFlowPhase;
    check(hook_rule::decide(row, row.pinned, names::kSharedBodies) == Verdict::Accept, "the pinned body is accepted");
    check(hook_rule::decide(row, 0, names::kSharedBodies) == Verdict::NotFound, "an unresolved method is refused");
    check(hook_rule::decide(row, row.pinned + 0x10, names::kSharedBodies) == Verdict::Moved, "a moved body is refused");
    names::MethodRow shared = row;
    shared.pinned = names::kSharedBodies[0];
    check(hook_rule::decide(shared, shared.pinned, names::kSharedBodies) == Verdict::Shared,
          "a row pinned to the empty shared body 0x145ab65c0 is refused");
    names::MethodRow counted = row;
    counted.share = 2;
    check(hook_rule::decide(counted, counted.pinned, names::kSharedBodies) == Verdict::Shared,
          "a row whose dump share is not 1 is refused");
    bool allSingle = true;
    for (const names::MethodRow& each : names::kAll) allSingle = allSingle && each.share == 1;
    check(allSingle, "every shipped row has share 1");
}

void gateTests() {
    using world_gate_rule::Cause;
    using world_gate_rule::Gate;
    using world_gate_rule::Place;
    Gate gate;
    check(gate.place() == Place::Title && gate.loadSerial() == 0, "starts at the title with no load");
    gate.phaseChanged("TitleMenu");
    check(gate.place() == Place::Title, "TitleMenu is the title");
    gate.phaseChanged("Loading");
    check(gate.place() == Place::Loading, "Loading is loading");
    gate.sceneChanged("Field");
    check(gate.phaseChanged("InGame") == Cause::EnteredInGame && gate.loadSerial() == 1, "entering InGame is a load");
    check(gate.place() == Place::InGame, "InGame in the field is in game");
    check(gate.phaseChanged("InGame") == Cause::None && gate.loadSerial() == 1, "InGame again is not a load");
    check(gate.sceneChanged("Event") == Cause::SceneChange && gate.place() == Place::Cutscene, "Field to Event is a load");
    check(gate.sceneChanged("Field") == Cause::SceneChange && gate.loadSerial() == 3, "back to the field is a load");
    check(gate.sceneChanged("Title") == Cause::None, "a change to a non-load scene type is not a load");
    gate.sceneChanged("Field");
    check(gate.ferrystoneStarted() == Cause::Ferrystone && gate.place() == Place::Travel, "a ferrystone is travel");
    const uint16_t afterStone = gate.loadSerial();
    gate.phaseChanged("Loading");
    check(gate.phaseChanged("InGame") == Cause::None && gate.loadSerial() == afterStone,
          "the ferrystone's own InGame entry is not counted twice");
    gate.ferrystoneStarted();
    gate.sceneLoadCompleted();
    check(gate.place() == Place::InGame, "a ferrystone that stays InGame ends at the scene load");
    gate.sceneChanged("GameOver");
    check(gate.place() == Place::Down, "GameOver is down");
}

void wireTests() {
    wire_dd2::Place place{};
    place.place = 2;
    place.loadSerial = 9;
    const auto bytes = wire_dd2::encode(place);
    const auto back = wire_dd2::decodePlace(bytes);
    check(back && back->loadSerial == 9, "PLACE round trip");
    place.place = wire_dd2::kPlaces;
    check(!wire_dd2::decodePlace(wire_dd2::encode(place)), "PLACE with an unknown place is refused");
    const wire_dd2::GameHello hello{0x6ae0ce34, 1, 2, wire_dd2::kProto, wire_dd2::kModuleWorldGate};
    const auto helloBack = wire_dd2::decodeHello(wire_dd2::encode(hello));
    check(helloBack && helloBack->peTimestamp == 0x6ae0ce34 && helloBack->moduleMask == 1, "GAME_HELLO round trip");
    check(!wire_dd2::decodePlace(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)), "short PLACE refused");
}

void clockTests() {
    using clock_rule::Verdict;
    const clock_rule::Units units{86400, 3600, 60};
    const clock_rule::Time host{3, 36000.0};  // day 3, 10:00
    check(clock_rule::decide(host, {3, 35950.0}, units, false).verdict == Verdict::Keep, "50 game s off: kept");
    const auto set = clock_rule::decide(host, {3, 30000.0}, units, false);
    check(set.verdict == Verdict::Set && set.day == 3 && set.hour == 10 && set.minute == 0, "100 min behind: set to the host's 10:00");
    check(clock_rule::decide(host, {4, 0.0}, units, false).verdict == Verdict::RefuseBackwards, "a guest ahead is never set back");
    check(clock_rule::decide(host, {4, 0.0}, units, true).verdict == Verdict::Set, "unless the host loaded since");
    const auto minutes = clock_rule::decide({2, 3600.0 * 7 + 60 * 25 + 30}, {1, 0.0}, units, false);
    check(minutes.verdict == Verdict::Set && minutes.day == 2 && minutes.hour == 7 && minutes.minute == 25, "hour and minute from day seconds");
    check(clock_rule::decide(host, {0, 0.0}, {}, false).verdict == Verdict::BadUnits, "unknown units: nothing is set");
}

void yawTests() {
    const float identity[4] = {0, 0, 0, 1};
    check(std::fabs(local_body::yawOf(identity)) < 1e-6f, "identity faces yaw 0");
    const float quarter[4] = {0, std::sin(0.785398f), 0, std::cos(0.785398f)};  // 90 degrees around Y
    check(std::fabs(local_body::yawOf(quarter) - 1.570796f) < 1e-4f, "90 degrees around Y is yaw pi/2");
}

void bodyWireTests() {
    wire_dd2::BodyState body{};
    body.position[0] = 100.5;
    body.actionHash[1] = 0xABCD;
    const auto back = wire_dd2::decodeBodyState(wire_dd2::encode(body));
    check(back && back->position[0] == 100.5 && back->actionHash[1] == 0xABCD, "BODY_STATE round trip");
    body.position[2] = std::nan("");
    check(!wire_dd2::decodeBodyState(wire_dd2::encode(body)), "BODY_STATE with a NaN position is refused");
    body.position[2] = 0;
    body.kind = wire_dd2::kBodyKinds;
    check(!wire_dd2::decodeBodyState(wire_dd2::encode(body)), "BODY_STATE with an unknown body kind is refused");
    wire_dd2::ActionEvent event{};
    event.layer = 2;
    check(!wire_dd2::decodeActionEvent(wire_dd2::encode(event)), "ACTION_EVENT on layer 2 is refused");
    wire_dd2::Clock clock{};
    clock.elapsedDaySeconds = -1;
    check(!wire_dd2::decodeClock(wire_dd2::encode(clock)), "CLOCK with negative day seconds is refused");
    clock.elapsedDaySeconds = 4000;
    check(wire_dd2::decodeClock(wire_dd2::encode(clock)).has_value(), "CLOCK round trip");
}

}  // namespace

void bodyCardTests() {
    const std::vector<std::pair<uint16_t, std::string>> entries{{uint16_t{0}, "{\"hp\":1}"}, {uint16_t{4}, "{\"body\":[1,2]}"}};
    const auto bytes = wire_dd2::encodeCardEntries(entries);
    const auto card = wire_dd2::encodeBodyCard(wire_dd2::kKindArisen, 2, 0x1234, bytes);
    const auto back = wire_dd2::decodeBodyCard(card);
    check(back && back->entries.size() == 2 && back->entries[1].tag == 4 && back->entries[1].bytes.size() == 14 &&
              back->header.hash == 0x1234,
          "BODY_CARD round trip");
    auto shortCard = card;
    shortCard.pop_back();
    check(!wire_dd2::decodeBodyCard(shortCard), "a cut BODY_CARD is refused");
    auto badCount = card;
    badCount[2] = 3;
    check(!wire_dd2::decodeBodyCard(badCount), "a BODY_CARD whose count disagrees with its entries is refused");
    auto badVersion = card;
    badVersion[1] = 9;
    check(!wire_dd2::decodeBodyCard(badVersion), "an unknown BODY_CARD version is refused");
}

void rosterTests() {
    roster_rule::Key a, b, c;
    a.rowId[0] = 1;
    b.rowId[0] = 2;
    c.rowId[0] = 2;
    c.index = 7;
    const auto result = roster_rule::compare({a, b, b}, {b, c});
    check(result.both == std::vector<roster_rule::Key>{b} && result.missing == std::vector<roster_rule::Key>{a} &&
              result.extra == std::vector<roster_rule::Key>{c},
          "roster comparison: both, missing, extra (duplicates once)");
    const std::vector<roster_rule::Key> keys{a, c};
    const auto decoded = wire_dd2::decodeRoster(wire_dd2::encodeRoster(5, keys));
    check(decoded && decoded->header.loadSerial == 5 && decoded->keys.size() == 2 && decoded->keys[1] == c, "ENEMY_ROSTER round trip");
    auto cut = wire_dd2::encodeRoster(5, keys);
    cut.pop_back();
    check(!wire_dd2::decodeRoster(cut), "a cut ENEMY_ROSTER is refused");
}

void enemyTests() {
    roster_rule::Key target;
    target.index = 3;
    wire_dd2::EnemyState state{};
    state.key = target;
    state.position[1] = 12.5;
    state.flags = wire_dd2::kEnemyDead;
    const std::vector<wire_dd2::EnemyState> states{state, state};
    const auto back = wire_dd2::decodeStates(wire_dd2::encodeStates(4, states));
    check(back && back->size() == 2 && (*back)[1].key == target && (*back)[0].position[1] == 12.5 &&
              (*back)[0].flags == wire_dd2::kEnemyDead,
          "ENEMY_STATE round trip");
    auto bad = states;
    bad[0].hp = std::nanf("");
    check(!wire_dd2::decodeStates(wire_dd2::encodeStates(4, bad)), "ENEMY_STATE with a NaN HP is refused");
    wire_dd2::EnemyHit hit{};
    hit.key = target;
    hit.damage = 40.0f;
    check(wire_dd2::decodeEnemyHit(wire_dd2::encode(hit)).has_value(), "ENEMY_HIT round trip");
    hit.attackerKind = wire_dd2::kBodyKinds;
    check(!wire_dd2::decodeEnemyHit(wire_dd2::encode(hit)), "ENEMY_HIT from an unknown body kind is refused");
    const auto spawn = wire_dd2::decodeSpawn(wire_dd2::encodeSpawn(target, "{\"_CharaIDHash\":1}"));
    check(spawn && spawn->key == target && spawn->json == "{\"_CharaIDHash\":1}", "ENEMY_SPAWN round trip");
    check(!wire_dd2::decodeSpawn(wire_dd2::encodeSpawn(target, "")), "an empty ENEMY_SPAWN is refused");

    const enemy_rule::Point guests[] = {{0, 0, 0}};
    check(enemy_rule::streamed({100, 0, 0}, guests) && !enemy_rule::streamed({130, 0, 0}, guests), "streamed within 120 m");
    check(enemy_rule::damageOk(50, 100) && !enemy_rule::damageOk(0, 100) && !enemy_rule::damageOk(150, 100) &&
              !enemy_rule::damageOk(std::nanf(""), 100),
          "hit damage positive, finite, at most the target's HP");
    check(enemy_rule::fresh(1000, 500) && enemy_rule::fresh(1500, 500) && !enemy_rule::fresh(1501, 500),
          "a host enemy state is fresh for 1 s");
    const double here[3] = {0, 0, 0}, near[3] = {20, 0, 0}, far[3] = {30, 40, 0};
    check(!join_warp_rule::needsWarp(join_warp_rule::distanceM(here, near)) && join_warp_rule::needsWarp(join_warp_rule::distanceM(here, far)),
          "the join warp is for bodies over 30 m apart");
    join_warp_rule::OncePerLoad once;
    check(!once.pending(), "no join warp before a load pair is seen");
    once.observe(1, 1);
    once.observe(1, 1);
    check(once.pending(), "a load pair arms the join warp");
    once.settle();
    once.observe(1, 1);
    check(!once.pending(), "the same pair does not arm it again");
    once.observe(2, 1);
    check(once.pending(), "a load of ours arms it again");
    once.settle();
    once.observe(2, 2);
    check(once.pending(), "a load of the host's arms it again");
    enemy_rule::HitBudget budget;
    uint32_t admitted = 0;
    for (int i = 0; i < 30; ++i) admitted += budget.admit(1, target, 1000);
    check(admitted == enemy_rule::kHitsPerTarget, "20 hits per target each second");
    check(budget.admit(1, target, 1000 + enemy_rule::kBudgetWindowMs), "the next second admits again");
}

void saveFolderTests() {
    check(save_folders::validId("chaos") && save_folders::validId("campaign-2"), "launcher campaign ids are valid");
    check(!save_folders::validId("") && !save_folders::validId("..") && !save_folders::validId("a/b") && !save_folders::validId("Chaos") &&
              !save_folders::validId(std::string(save_folders::kMaxIdLength + 1, 'a')),
          "an empty id, a path, upper case and an over-long id are ignored");
}

void profileTests() {
    profile_rule::Profile profile;
    profile.tdbHash = 0xA1B2C3D4;
    profile.level = 42;
    profile.job = 3;
    profile.equipIndex = 1;
    profile.pieces = {{profile_rule::Kind::Context, profile_rule::Owner::Arisen, "app.HumanStatusContext", "{\"_Level\":42}\n{x}"},
                      {profile_rule::Kind::Item, profile_rule::Owner::MainPawn, "app.ItemManager.ItemStorageBackup", ""},
                      {profile_rule::Kind::Money, profile_rule::Owner::Arisen, "app.ItemManager.MoneyBackup", "{\"_Gold\":900}"}};
    const std::string text = profile_rule::encode(profile);
    const auto back = profile_rule::decode(text);
    check(back && back->tdbHash == profile.tdbHash && back->level == 42 && back->job == 3 && back->equipIndex == 1 &&
              back->pieces.size() == 3 && back->pieces[0].json == profile.pieces[0].json &&
              back->pieces[1].owner == profile_rule::Owner::MainPawn && back->pieces[1].json.empty() &&
              back->pieces[2].kind == profile_rule::Kind::Money,
          "profile round trip (JSON with a newline, an empty piece)");
    check(!profile_rule::decode(text.substr(0, text.size() - 4)), "a profile without its end line is refused");
    check(!profile_rule::decode(text + "x"), "a profile with bytes after its end line is refused");
    std::string wrongSize = text;
    wrongSize.replace(wrongSize.find(" 17\n"), 4, " 99\n");
    check(!profile_rule::decode(wrongSize), "a piece whose byte count runs past its block is refused");
    std::string wrongFormat = text;
    wrongFormat[5] = '9';
    check(!profile_rule::decode(wrongFormat), "another profile format is refused");
}

void gameStateTests() {
    using namespace game_state_rule;
    Report report;
    check(format(report) == "place=title;char=off;save=off;slot=-1;peers=", "GAME_STATE of a fresh solo game");
    report.place = Place::World;
    report.character = Char::Mismatch;
    report.save = Save::Ok;
    report.slot = 3;
    report.peers = {{76561198000000001ULL, false}, {76561198000000002ULL, true}};
    check(format(report) == "place=world;char=mismatch;save=ok;slot=3;peers=76561198000000001:0,76561198000000002:1",
          "GAME_STATE with a character problem, a slot and a live and a stale peer");
    check(placeOf(world_gate_rule::Place::Travel) == Place::Loading && placeOf(world_gate_rule::Place::Cutscene) == Place::World &&
              placeOf(world_gate_rule::Place::Title) == Place::Title,
          "the world gate's places map onto title, loading and world");
    check(problems(Report{}, kSlotUnknown).empty(), "a solo game has no problems");
    Report bad;
    bad.place = Place::World;
    bad.character = Char::Refused;
    bad.save = Save::Refused;
    bad.slot = 2;
    const auto lines = problems(bad, 5);
    check(lines.size() == 3 && lines[0].rfind("Your character could not be loaded: ", 0) == 0 &&
              lines[1] == "Save not protected: quit to the title" && lines[2] == "Load slot 5 like the host",
          "problems: character, save and the host's slot, in order");
    bad.slot = 5;
    check(problems(bad, 5).size() == 2, "the host's slot loaded: no slot line");
    Report title;
    check(problems(title, 4).size() == 1 && problems(title, kSlotUnknown).empty(), "at the title before a load the guest is told the host's slot");
    Report world;
    world.place = Place::World;
    check(problems(world, 4).empty(), "in the world with an unknown slot nothing is claimed");
    check(partnerLine("Ann", true, Place::World, 12.0) == "Ann: connection lost", "a stale partner is shown as lost");
    check(partnerLine("Ann", false, Place::World, 12.4) == "Ann: in world, 12 m", "a partner in the world shows the distance");
    check(partnerLine("Ann", false, Place::World, std::nullopt) == "Ann: in world", "no distance when unknown");
    check(partnerLine("Ann", false, Place::Loading, 12.0) == "Ann: loading" && partnerLine("Ann", false, Place::Title, 1.0) == "Ann: title",
          "loading and title partners show no distance");
    const std::string all = partnerLine("Ann", false, Place::World, 3.0) + partnerLine("Ann", true, Place::Title, 3.0);
    check(all.find("Â·") == std::string::npos && all.find("â¢") == std::string::npos, "no dot separators in the lines");
}

void sessionRuleTests() {
    using session_rule::kPeerStaleMs;
    check(!session_rule::stale(10000, 10000), "a peer heard this tick is live");
    check(!session_rule::stale(10000 + kPeerStaleMs, 10000), "a peer silent exactly the limit is still live");
    check(session_rule::stale(10001 + kPeerStaleMs, 10000), "a peer silent past the limit is stale");
    check(!session_rule::stale(5000, 6000), "a frame stamped after the tick does not make a peer stale");
}

void loadSlotRuleTests() {
    using namespace load_slot_rule;
    check(parse("4") == 4 && parse("0") == 0 && parse("12") == 12, "the forced slot is a plain number");
    check(parse("") == kNoSlot && parse(nullptr) == kNoSlot && parse("x") == kNoSlot && parse("-1") == kNoSlot && parse("4a") == kNoSlot,
          "an empty, signed or non-numeric slot forces nothing");
    check(slotFor(true, 4, 0) == 4 && slotFor(true, 4, 1) == 4 && slotFor(true, 4, 4) == 4, "a guest loads the host's slot whichever row it asked");
    check(slotFor(true, 0, 3) == 0, "slot 0 (the autosave) can be the forced one");
    check(slotFor(true, 4, -1) == -1, "the system data load is never changed");
    check(slotFor(false, 4, 0) == 0, "a host never rewrites a load");
    check(slotFor(true, kNoSlot, 3) == 3, "a guest with no forced slot loads what it asked");
}

void titleMenuRuleTests() {
    using namespace title_menu_rule;
    const Menu ready{true, true, true};
    check(waiting(ready) && !waiting({false, true, true}) && !waiting({true, false, true}) && !waiting({true, true, false}),
          "the menu waits only in SelectMenu with the SaveService idle and the redirect safe");
    check(!settled(0, 99999) && !settled(1000, 1000 + kSettleMs - 1) && settled(1000, 1000 + kSettleMs), "Continue is pressed after the settle time");
    const int32_t rows[] = {13, 0, 2, 8};
    check(rowOf(rows, 13) == 0 && rowOf(rows, 2) == 2 && rowOf(rows, 7) == -1 && rowOf({}, 13) == -1 && rowOf(rows, kUnknownLiteral) == -1,
          "the first row of an item type, or none");
    check(blocksNewGame(true, 0, 0, 1) && blocksNewGame(true, 1, 0, 1), "a guest may not choose the New Game rows");
    check(!blocksNewGame(false, 0, 0, 1), "a host may");
    check(!blocksNewGame(true, 13, 0, 1) && !blocksNewGame(true, 2, 0, 1), "a guest's Continue and Load rows run");
    check(!blocksNewGame(true, kUnknownLiteral, kUnknownLiteral, kUnknownLiteral) && !blocksNewGame(true, 0, kUnknownLiteral, kUnknownLiteral),
          "nothing is refused when a literal or the row is unknown");
}

int main() {
    loadSlotRuleTests();
    titleMenuRuleTests();
    gameStateTests();
    sessionRuleTests();
    rosterTests();
    enemyTests();
    saveFolderTests();
    profileTests();
    bodyCardTests();
    clockTests();
    yawTests();
    bodyWireTests();
    hookRuleTests();
    gateTests();
    wireTests();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
