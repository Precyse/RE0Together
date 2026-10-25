#pragma once

// World digest canary (partner_mode=state, docs/RE0_NOTES.md "State mode: world digest"): every machine tells the other
// a small digest of its replicated stores once a second (WORLD_DIGEST 0x0178, unreliable); a store that differs for 3 s
// while both machines stand at the same flag commit is reported and repaired with the existing snapshot (resync).
namespace world_digest {

// Registers the WORLD_DIGEST handler and the game tick snapshot. False when the handler could not be registered.
bool enable();

// Net thread, every tick: sends the digest about once a second.
void onNetTick();

}  // namespace world_digest
