#pragma once
// DS2-internal: a logging hook on the NPC damage component's MsgDamage handler (see ds2/npc_damage_probe.cpp), to tell
// where a hit on an enemy is lost: every call for an enemy of the directory is logged with who called it and the
// component's gate flags. Needs adapter.ini enemy_sync=1.
namespace npc_damage_probe {

void installEarly();

}  // namespace npc_damage_probe
