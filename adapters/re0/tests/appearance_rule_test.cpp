// Checks the outfit rules and the APPEARANCE codec (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/appearance_rule.h"

namespace {

using appearance_rule::kNoOutfit;
using appearance_rule::Role;
using character_owner::Character;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void checkOfferedSet() {
    for (const uint8_t model : std::array<uint8_t, 4>{0x02, 0x7d, 0x81, 0x91}) check(appearance_rule::find(Character::Rebecca, model), "Rebecca base outfit");
    for (const uint8_t model : std::array<uint8_t, 3>{0x05, 0x95, 0xa5}) check(appearance_rule::find(Character::Billy, model), "Billy base outfit");
    check(appearance_rule::find(Character::Rebecca, 0x71)->dlcAppId == 381713, "DLC outfit names its pack");
    check(appearance_rule::find(Character::Billy, 0x99)->dlcAppId == 381710, "Billy DLC outfit names its pack");
    check(appearance_rule::find(Character::Billy, 0x95)->dlcAppId == appearance_rule::kNoDlc, "base outfit has no pack");
}

void checkRefusals() {
    check(!appearance_rule::find(Character::Rebecca, 0xa5), "Rebecca never wears Wesker");
    check(!appearance_rule::find(Character::Rebecca, 0x95), "Rebecca never wears a Billy outfit");
    check(!appearance_rule::find(Character::Billy, 0x7d), "Billy never wears a Rebecca outfit");
    check(!appearance_rule::find(Character::Billy, 0xff), "an unknown id is refused");
    check(!appearance_rule::find(Character::Billy, kNoOutfit), "no model is not an outfit");
    check(!appearance_rule::find(Character::Unknown, 0x02), "an unknown character has no outfits");
}

void checkParse() {
    check(appearance_rule::parseModel("7d") == 0x7d, "lowercase hex");
    check(appearance_rule::parseModel("A5") == 0xa5, "uppercase hex");
    check(appearance_rule::parseModel("5") == 0x05, "one digit");
    check(!appearance_rule::parseModel(""), "empty is not a value");
    check(!appearance_rule::parseModel("zz"), "non-hex is rejected");
    check(!appearance_rule::parseModel("123"), "three digits are rejected");
}

void checkWanted() {
    using appearance_rule::wantedModel;
    check(wantedModel(Role::Local, false, 0x7d, 0x95) == 0x7d, "an unapplied ini choice is wanted on the local character");
    check(wantedModel(Role::Local, true, 0x7d, 0x95) == kNoOutfit, "an applied ini choice leaves the suitcase in charge");
    check(wantedModel(Role::Local, false, kNoOutfit, 0x95) == kNoOutfit, "no ini choice, nothing wanted");
    check(wantedModel(Role::Remote, false, 0x7d, 0x95) == 0x95, "the remote character wears what its owner reported");
    check(wantedModel(Role::Remote, true, 0x7d, kNoOutfit) == kNoOutfit, "nothing reported, nothing wanted");
    check(wantedModel(Role::Unowned, false, 0x7d, 0x95) == kNoOutfit, "an unowned character is left alone");
    check(appearance_rule::needsChange(0x05, 0x95), "a different outfit needs a change");
    check(!appearance_rule::needsChange(0x95, 0x95), "the same outfit does not");
    check(!appearance_rule::needsChange(0x95, kNoOutfit), "no wish does not");
}

void checkSenderJudgement() {
    using appearance_rule::judgeSender;
    using appearance_rule::Verdict;
    constexpr int kUndecided = control_rule::kNoOwner;
    check(judgeSender(kUndecided, 1) == Verdict::Hold, "before ownership a message is held");
    check(judgeSender(1, 1) == Verdict::Apply, "ownership decided for the sender: the held outfit is applied");
    check(judgeSender(0, 1) == Verdict::Drop, "ownership decided for another slot: the held outfit is dropped");
    check(judgeSender(1, 1) == Verdict::Apply, "after ownership the owner's message applies");
    check(judgeSender(0, 1) == Verdict::Drop, "after ownership a non-owner's message is dropped");
}

void checkCodec() {
    const appearance_rule::Appearance sent = appearance_rule::encode(Character::Billy, 0xa5);
    check(sizeof(sent) == 4, "payload is 4 bytes");
    const auto back = appearance_rule::decode({reinterpret_cast<const uint8_t*>(&sent), sizeof(sent)});
    check(back && back->character == static_cast<uint8_t>(Character::Billy) && back->model == 0xa5, "round trip");
    const uint8_t shortPayload[3] = {0, 0xa5, 0};
    check(!appearance_rule::decode(shortPayload), "a short payload is rejected");
    const uint8_t badCharacter[4] = {7, 0xa5, 0, 0};
    check(!appearance_rule::decode(badCharacter), "an unknown character is rejected");
    check(appearance_rule::kMsgAppearance == 0x0116, "message id");
}

}  // namespace

int main() {
    checkOfferedSet();
    checkRefusals();
    checkParse();
    checkWanted();
    checkSenderJudgement();
    checkCodec();
    if (g_failures == 0) std::printf("appearance_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
