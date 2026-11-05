#pragma once
// The running game's Troc catalogue: every static pa::TrocTr<Name> instance found by its RTTI type name, with the
// opcode and limits it holds (cd design, adapter.md 5).
#include <array>
#include <cstdint>
#include <string_view>

#include "troc_table.h"

namespace troc_catalogue {

// Every name the adapter's modules use. All must resolve, or the adapter stays sniff-only (CD_HELLO catalogue = 0).
// The first eight are cdmp's own check list; the rest are the world gate's load bracket.
constexpr std::array<std::string_view, 15> kRequired{
    "ActorLoadDataAck", "RemoveActorAck", "MoveActorReq", "MoveActorAck", "StartActionReq", "StartActionAck",
    "RotateActorReq", "RotateActorAck", "GameLoadingStartReq", "LoadingCompleteReq", "LoadingCompleteAck",
    "ReviveLoadingReq", "ReviveLoadingAck", "LogoutCharacterToFieldServerReq", "LogoutUserToServerAck"};

// Scans the instances (call once, after WorldSystem exists: their derived vtables are written by dynamic
// initialisers), logs the count and any missing required name, and writes coop\troc_catalogue.tsv for the capture
// tools. True when every required name resolved. The table is read-only afterwards, so any thread may read it.
bool build();

const troc_table::Table& table();

// The message name of `opcode`, or "?" when the catalogue does not hold it.
const char* nameOf(uint16_t opcode);

}  // namespace troc_catalogue
