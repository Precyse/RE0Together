#include "event_body.h"

#include "character_owner.h"
#include "crash_dump.h"
#include "debug_stats.h"
#include "game.h"
#include "log.h"
#include "puppet.h"

namespace {

using character_owner::Character;
namespace rule = event_rule;

bool isPuppet(uintptr_t character) { return character != 0 && puppet::active(character); }

const char* opName(uint16_t opcode) {
    const auto name = reinterpret_cast<const char*>(
        game::readPointer(game::kOpcodeTable + opcode * game::kOpcodeRowSize + game::kOpcodeNameOffset));
    return name ? name : "?";
}

// The unit the op's handler finds for a subject word that is neither character: the unit registry's n-th unit of the
// script's room (0x40ff00), then its object (0x51fed0). 0 when there is none.
uintptr_t unitByOrdinal(uint32_t scriptKey, uint32_t ordinal) {
    const uintptr_t registry = game::readPointer(game::kUnitRoomRegistryGlobal);
    const uintptr_t characters = game::readPointer(game::kGameCharaGlobal);
    if (!registry || !characters) return 0;
    __try {
        const auto index = game::callThiscall<uint32_t>(game::kUnitOrdinalFunction, reinterpret_cast<void*>(registry),
                                                        scriptKey, ordinal);
        if (index == game::kNoUnit) return 0;
        return game::callThiscall<uintptr_t>(game::kUnitObjectFunction, reinterpret_cast<void*>(characters), index);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "event_body: unit lookup")) {
        return 0;
    }
}

// The puppet an op was skipped for, to name it in the log.
uintptr_t puppetOf(rule::Target target, event_body::Seen seen, uintptr_t unit) {
    switch (target) {
        case rule::Target::Controlled: return seen.controlled;
        case rule::Target::Partner: return seen.partner;
        case rule::Target::Unit: return unit;
        case rule::Target::Both: return isPuppet(seen.controlled) ? seen.controlled : seen.partner;
        case rule::Target::Nobody: break;
    }
    return 0;
}

}  // namespace

namespace event_body {

Plan plan(const Op& op, Seen seen, bool ownerRunsThread) {
    rule::Puppets puppets{isPuppet(seen.controlled), isPuppet(seen.partner), false};
    const rule::Target target = rule::targetOf(op.code, op.subjectWord);
    uintptr_t unit = 0;
    if (target == rule::Target::Unit) {
        unit = unitByOrdinal(op.scriptKey, op.subjectWord);
        puppets.unit = isPuppet(unit);
    }
    Plan decision;
    decision.run = rule::runFor(op.code, op.subjectWord, puppets, ownerRunsThread);
    if (decision.run == rule::BodyRun::RunLocalHalf) {
        decision.hideControlled = puppets.controlled;
        decision.hidePartner = puppets.partner;
    }
    const bool skipped = decision.run == rule::BodyRun::Skip;
    const bool absentOwner = decision.run == rule::BodyRun::Run && rule::movesAbsentOwner(op.code, ownerRunsThread) &&
                             puppets.partner;
    if (skipped || absentOwner) {
        const Character who = character_owner::identify(puppetOf(target, seen, unit));
        if (skipped) {
            debug_stats::count(debug_stats::Counter::EventBodyOpsSkipped);
            logger::write("event_sync: op %s skipped as Body (%s is a puppet)", opName(op.code), character_owner::name(who));
        } else {
            logger::write("event_sync: op %s runs on the puppet of %s, its owner does not run the thread",
                          opName(op.code), character_owner::name(who));
        }
    }
    return decision;
}

std::optional<event_place::Mark> markPlacement(const Op& op, Seen seen, const Plan& decision) {
    if (decision.run != rule::BodyRun::Run || !isPuppet(seen.partner)) return std::nullopt;
    const bool subMove = op.code == rule::op::kSubMove;
    if (!subMove && !rule::placesPartner(op.code, op.climbType)) return std::nullopt;
    return event_place::mark(character_owner::identify(seen.partner));
}

}  // namespace event_body
